#include "fsa/stream_array.hpp"

#include <hls_stream.h>

#include "fsa/arithmetic.hpp"
#include "fsa/stream_types.hpp"

namespace fsa{
    namespace{

        using PeStream = hls::stream<StreamPeToken>;

        struct PeLaneResult{
            bool valid = false;
            bool segment_match = false;
            elem_t element{};
        };

        using PeLaneStream = hls::stream<PeLaneResult>;

        constexpr int MESH_FIFO_DEPTH = 5;
        static_assert(
            SA_ROWS>=SA_COLS,
            "当前FSA mesh要求head dimension不小于token tile宽度"
        );

        bool isReductionPhase(const StreamPeOp op){
            #pragma HLS INLINE
            return op==StreamPeOp::QK_MAC ||
                op==StreamPeOp::ROWSUM_MAC ||
                op==StreamPeOp::PV_MAC;
        }

        int phaseWaveCount(const StreamPeOp op){
            #pragma HLS INLINE
            if(op==StreamPeOp::QK_MAC){
                return SA_COLS;
            }
            if(op==StreamPeOp::PV_MAC){
                return SA_ROWS;
            }
            if(op==StreamPeOp::EXP2_PWL){
                return exp2PWLPieces;
            }
            return 1;
        }

        bool softmaxLaneEnabled(
            const int row,
            const int col,
            const std::uint16_t active_keys,
            const bool causal,
            const std::uint32_t query_base,
            const std::uint32_t key_base
        ){
            #pragma HLS INLINE
            if(row>=SA_COLS || row>=(int)active_keys){
                return false;
            }
            return !causal ||
                key_base+(unsigned)row<=query_base+(unsigned)col;
        }

        /**
         * @brief 一个物理PE在一个完整phase中处理定长token波。
         *
         * resident在本phase内只读，计算结果经stream送给收集进程，因此
         * FMA流水线不再与StreamPeState写回形成loop-carried dependence。
         */
        void macPeProcess(
            const elem_t resident,
            const int row,
            const int col,
            const std::uint16_t active_keys,
            const bool causal,
            const std::uint32_t query_base,
            const std::uint32_t key_base,
            const StreamPeOp op,
            PeStream& left,
            PeStream& up,
            PeStream& right,
            PeStream& down,
            PeLaneStream& lane
        ){
            #pragma HLS INLINE off

            const int wave_count = phaseWaveCount(op);
            const bool reduction = isReductionPhase(op);
            const bool lane_enabled = softmaxLaneEnabled(
                row, col, active_keys, causal, query_base, key_base
            );

            for(int wave=0; wave<wave_count; ++wave){
                #pragma HLS PIPELINE II=1
                const StreamPeToken horizontal = left.read();
                const StreamPeToken vertical = up.read();

                const bool operation_valid = horizontal.valid &&
                    vertical.valid && (reduction || lane_enabled);
                const bool exp2 = op==StreamPeOp::EXP2_PWL;

                // 该函数是每个PE唯一的FMA调用点。QK、softmax、rowsum和
                // PV的phase按顺序调用同一个mesh模块，不再产生第二组PE。
                const PeMacUnitOutput arithmetic = peMacUnit(
                    resident,
                    horizontal.horizontal,
                    vertical.vertical,
                    exp2
                );

                right.write(horizontal);

                StreamPeToken down_token = vertical;
                if(reduction && operation_valid){
                    down_token.vertical = arithmetic.out_accType;
                }
                down.write(down_token);

                PeLaneResult lane_result{};
                lane_result.valid = !reduction && operation_valid;
                lane_result.segment_match = arithmetic.out_exp2;
                lane_result.element = arithmetic.out_elemType;
                lane.write(lane_result);
            }
        }

        void feedMeshLeft(
            const elem_t data[SA_COLS][SA_ROWS],
            const std::uint16_t active_keys,
            const StreamPeOp op,
            PeStream horizontal[SA_ROWS][SA_COLS+1]
        ){
            #pragma HLS INLINE off

            const int wave_count = phaseWaveCount(op);
            for(int wave=0; wave<wave_count; ++wave){
                #pragma HLS PIPELINE II=1
                for(int row=0; row<SA_ROWS; ++row){
                    #pragma HLS UNROLL
                    StreamPeToken token{};
                    token.valid = true;
                    token.last = wave+1==wave_count;
                    token.op = op;
                    token.tag = wave;

                    if(op==StreamPeOp::QK_MAC){
                        // 始终发送SA_COLS个key token；tail key变为bubble。
                        token.valid = wave<(int)active_keys;
                        token.horizontal = token.valid
                            ? data[wave][row] : elemZero();
                    }else if(op==StreamPeOp::PV_MAC){
                        // 始终发送SA_ROWS个feature token；无效key行在PE中
                        // 旁路竖直部分和，避免少写任何FIFO。
                        token.valid = row<SA_COLS &&
                            row<(int)active_keys;
                        const int key_row = row<SA_COLS ? row : 0;
                        token.horizontal = token.valid
                            ? data[key_row][wave] : elemZero();
                    }else if(op==StreamPeOp::EXP2_PWL){
                        token.horizontal = peExp2Slope(
                            (exp2_counter_t)wave
                        );
                    }else if(op==StreamPeOp::SCALE_SCORE){
                        token.horizontal = elemAttentionScale();
                    }else{
                        token.horizontal = elemOne();
                    }
                    horizontal[row][0].write(token);
                }
            }
        }

        void feedMeshTop(
            const acc_t column_operand[SA_COLS],
            const StreamPeOp op,
            PeStream vertical[SA_ROWS+1][SA_COLS]
        ){
            #pragma HLS INLINE off

            const int wave_count = phaseWaveCount(op);
            for(int wave=0; wave<wave_count; ++wave){
                #pragma HLS PIPELINE II=1
                for(int col=0; col<SA_COLS; ++col){
                    #pragma HLS UNROLL
                    StreamPeToken token{};
                    token.valid = true;
                    token.last = wave+1==wave_count;
                    token.op = op;
                    token.tag = wave;
                    if(op==StreamPeOp::SUB_MAX){
                        token.vertical = column_operand[col];
                    }else if(op==StreamPeOp::EXP2_PWL){
                        token.vertical = exp2PWLIntercept(
                            (exp2_counter_t)wave
                        );
                    }else{
                        token.vertical = accZero();
                    }
                    vertical[0][col].write(token);
                }
            }
        }

        void collectMeshBottom(
            const StreamPeOp op,
            PeStream vertical[SA_ROWS+1][SA_COLS],
            acc_t result[SA_COLS][SA_ROWS]
        ){
            #pragma HLS INLINE off

            const int wave_count = phaseWaveCount(op);
            const bool reduction = isReductionPhase(op);
            for(int wave=0; wave<wave_count; ++wave){
                #pragma HLS PIPELINE II=1
                for(int col=0; col<SA_COLS; ++col){
                    #pragma HLS UNROLL
                    const StreamPeToken token =
                        vertical[SA_ROWS][col].read();
                    if(reduction && wave<SA_ROWS){
                        result[col][wave] = token.vertical;
                    }
                }
            }
        }

        void collectMeshLanes(
            const StreamPeOp op,
            PeLaneStream lane[SA_ROWS][SA_COLS],
            elem_t result[SA_ROWS][SA_COLS]
        ){
            #pragma HLS INLINE off

            const int wave_count = phaseWaveCount(op);
            for(int wave=0; wave<wave_count; ++wave){
                #pragma HLS PIPELINE II=1
                for(int row=0; row<SA_ROWS; ++row){
                    #pragma HLS UNROLL
                    for(int col=0; col<SA_COLS; ++col){
                        #pragma HLS UNROLL
                        const PeLaneResult item = lane[row][col].read();
                        const bool selected = op==StreamPeOp::EXP2_PWL
                            ? item.segment_match : true;
                        if(item.valid && selected){
                            result[row][col] = item.element;
                        }
                    }
                }
            }
        }

        void drainMeshRight(
            const StreamPeOp op,
            PeStream horizontal[SA_ROWS][SA_COLS+1]
        ){
            #pragma HLS INLINE off

            const int wave_count = phaseWaveCount(op);
            for(int wave=0; wave<wave_count; ++wave){
                #pragma HLS PIPELINE II=1
                for(int row=0; row<SA_ROWS; ++row){
                    #pragma HLS UNROLL
                    (void)horizontal[row][SA_COLS].read();
                }
            }
        }

        /**
         * @brief 全部计算phase复用的唯一FMA mesh。
         *
         * 每个调用的token数只由编译期阵列参数和opcode决定：QK固定
         * SA_COLS，PV固定SA_ROWS，PWL固定8，其余固定1。active_keys只
         * 生成bubble，不再控制循环上界。
         */
        void runFmaMesh(
            const elem_t resident[SA_ROWS][SA_COLS],
            const elem_t data[SA_COLS][SA_ROWS],
            const acc_t column_operand[SA_COLS],
            const std::uint16_t active_keys,
            const bool causal,
            const std::uint32_t query_base,
            const std::uint32_t key_base,
            const StreamPeOp op,
            acc_t reduction_result[SA_COLS][SA_ROWS],
            elem_t lane_result[SA_ROWS][SA_COLS]
        ){
            #pragma HLS INLINE off
            #pragma HLS DATAFLOW
            #pragma HLS ARRAY_PARTITION variable=resident type=complete dim=0
            #pragma HLS ARRAY_PARTITION variable=data type=complete dim=0
            #pragma HLS ARRAY_PARTITION variable=column_operand type=complete dim=1
            #pragma HLS ARRAY_PARTITION variable=reduction_result type=complete dim=1
            #pragma HLS ARRAY_PARTITION variable=lane_result type=complete dim=0

            PeStream horizontal[SA_ROWS][SA_COLS+1];
            PeStream vertical[SA_ROWS+1][SA_COLS];
            PeLaneStream lane[SA_ROWS][SA_COLS];
            #pragma HLS STREAM variable=horizontal depth=MESH_FIFO_DEPTH
            #pragma HLS STREAM variable=vertical depth=MESH_FIFO_DEPTH
            #pragma HLS STREAM variable=lane depth=MESH_FIFO_DEPTH

            feedMeshLeft(data, active_keys, op, horizontal);
            feedMeshTop(column_operand, op, vertical);

            for(int row=0; row<SA_ROWS; ++row){
                #pragma HLS UNROLL
                for(int col=0; col<SA_COLS; ++col){
                    #pragma HLS UNROLL
                    macPeProcess(
                        resident[row][col], row, col,
                        active_keys, causal, query_base, key_base, op,
                        horizontal[row][col], vertical[row][col],
                        horizontal[row][col+1], vertical[row+1][col],
                        lane[row][col]
                    );
                }
            }

            collectMeshBottom(op, vertical, reduction_result);
            collectMeshLanes(op, lane, lane_result);
            drainMeshRight(op, horizontal);
        }

        unsigned legacyLogicalStepCount(
            const bool initialize,
            const bool finalize
        ){
            const unsigned reset_steps = initialize ? SA_COLS : 0;
            const unsigned preload_steps =
                (SA_COLS+2*SA_ROWS)*SPAD_SUB_BANKS;
            const unsigned base_instruction_steps =
                (SA_COLS+1)+
                ((2*SA_ROWS+4+exp2PWLPieces-1)+SA_ROWS+SA_COLS+1)+
                (SA_ROWS+SA_COLS+SA_ROWS);
            const unsigned normalization_steps = finalize
                ? (2+reciprocalLatency)+(SA_ROWS+1)
                : 0;
            const unsigned readback_steps = 2*(1+SA_ROWS);
            return reset_steps+preload_steps+base_instruction_steps+
                normalization_steps+readback_steps;
        }

    }  // namespace

    void reset_stream_online_state(StreamOnlineState& state){
        #pragma HLS INLINE
        state.active = false;
        for(int query=0; query<SA_COLS; ++query){
            #pragma HLS UNROLL
            state.m[query] = accMinimum();
            state.l[query] = accZero();
            for(int feature=0; feature<SA_ROWS; ++feature){
                #pragma HLS UNROLL
                state.o[query][feature] = accZero();
            }
        }
    }

    void stream_fsa_tile(
        StreamOnlineState& online,
        const FsaCoreRequestInput& input,
        FsaCoreRequestOutput& output
    ){
        #pragma HLS INLINE off
        #pragma HLS ALLOCATION function instances=runFmaMesh limit=1

        elem_t resident_a[SA_ROWS][SA_COLS]{};
        elem_t resident_b[SA_ROWS][SA_COLS]{};
        elem_t k_tile[SA_COLS][SA_ROWS]{};
        elem_t v_tile[SA_COLS][SA_ROWS]{};
        acc_t mesh_reduction[SA_COLS][SA_ROWS]{};
        acc_t column_operand[SA_COLS]{};
        #pragma HLS ARRAY_PARTITION variable=resident_a type=complete dim=0
        #pragma HLS ARRAY_PARTITION variable=resident_b type=complete dim=0
        #pragma HLS ARRAY_PARTITION variable=k_tile type=complete dim=0
        #pragma HLS ARRAY_PARTITION variable=v_tile type=complete dim=0
        #pragma HLS ARRAY_PARTITION variable=mesh_reduction type=complete dim=1
        #pragma HLS ARRAY_PARTITION variable=column_operand type=complete dim=1

        // LOAD_Q/LOAD_SCORE只写寄存器，不占用FMA。所有需要乘加的phase
        // 都通过下面同一个runFmaMesh层级执行。
        for(int col=0; col<SA_COLS; ++col){
            #pragma HLS PIPELINE II=1
            for(int row=0; row<SA_ROWS; ++row){
                #pragma HLS UNROLL
                resident_a[row][col] = input.q[col][row];
            }
        }
        for(int key=0; key<SA_COLS; ++key){
            #pragma HLS PIPELINE II=1
            for(int feature=0; feature<SA_ROWS; ++feature){
                #pragma HLS UNROLL
                k_tile[key][feature] = input.k[key][feature];
                v_tile[key][feature] = input.v[key][feature];
            }
        }

        runFmaMesh(
            resident_a, k_tile, column_operand,
            input.active_keys, input.causal,
            input.query_base, input.key_base,
            StreamPeOp::QK_MAC, mesh_reduction, resident_b
        );

        acc_t tile_max[SA_COLS]{};
        acc_t new_max[SA_COLS]{};
        acc_t alpha[SA_COLS]{};
        acc_t rowsum[SA_COLS]{};
        #pragma HLS ARRAY_PARTITION variable=tile_max type=complete dim=1
        #pragma HLS ARRAY_PARTITION variable=new_max type=complete dim=1
        #pragma HLS ARRAY_PARTITION variable=alpha type=complete dim=1
        #pragma HLS ARRAY_PARTITION variable=rowsum type=complete dim=1

        for(int query=0; query<SA_COLS; ++query){
            #pragma HLS UNROLL
            tile_max[query] = accMinimum();
        }

        // 只保留SA_COLS×SA_COLS个有效score。resident_a的其余行清零，
        // 随后同一物理行在PV阶段表示padding key。
        for(int row=0; row<SA_ROWS; ++row){
            #pragma HLS PIPELINE II=1
            for(int query=0; query<SA_COLS; ++query){
                #pragma HLS UNROLL
                const bool enabled = softmaxLaneEnabled(
                    row, query, input.active_keys, input.causal,
                    input.query_base, input.key_base
                );
                if(row<SA_COLS && enabled){
                    const acc_t score = mesh_reduction[query][row];
                    tile_max[query] = accMax(tile_max[query], score);
                    resident_a[row][query] = cvtAtoE(score);
                }else{
                    resident_a[row][query] = elemZero();
                }
                resident_b[row][query] = elemZero();
            }
        }

        for(int query=0; query<SA_COLS; ++query){
            #pragma HLS UNROLL
            const acc_t old_l = input.initialize
                ? accZero() : online.l[query];
            const acc_t old_m = input.initialize
                ? accMinimum() : online.m[query];
            new_max[query] = accMax(old_m, tile_max[query]);
            alpha[query] = old_l==accZero()
                ? accZero()
                : accExp2PWL(
                    accDiff(old_m, new_max[query])*attentionScale()
                );
            column_operand[query] = -new_max[query];
        }

        // 两个phase保留旧实现的FP16舍入边界：先(score-max)写回FP16，
        // 再乘attention scale写回FP16。
        runFmaMesh(
            resident_a, k_tile, column_operand,
            input.active_keys, input.causal,
            input.query_base, input.key_base,
            StreamPeOp::SUB_MAX, mesh_reduction, resident_b
        );

        for(int query=0; query<SA_COLS; ++query){
            #pragma HLS UNROLL
            column_operand[query] = accZero();
        }
        runFmaMesh(
            resident_b, k_tile, column_operand,
            input.active_keys, input.causal,
            input.query_base, input.key_base,
            StreamPeOp::SCALE_SCORE, mesh_reduction, resident_a
        );

        // PWL collector只提交匹配分段；先清零保证masked lane严格为0。
        for(int row=0; row<SA_ROWS; ++row){
            #pragma HLS PIPELINE II=1
            for(int query=0; query<SA_COLS; ++query){
                #pragma HLS UNROLL
                resident_b[row][query] = elemZero();
            }
        }
        runFmaMesh(
            resident_a, k_tile, column_operand,
            input.active_keys, input.causal,
            input.query_base, input.key_base,
            StreamPeOp::EXP2_PWL, mesh_reduction, resident_b
        );

        runFmaMesh(
            resident_b, k_tile, column_operand,
            input.active_keys, input.causal,
            input.query_base, input.key_base,
            StreamPeOp::ROWSUM_MAC, mesh_reduction, resident_a
        );
        for(int query=0; query<SA_COLS; ++query){
            #pragma HLS UNROLL
            rowsum[query] = mesh_reduction[query][0];
        }

        runFmaMesh(
            resident_b, v_tile, column_operand,
            input.active_keys, input.causal,
            input.query_base, input.key_base,
            StreamPeOp::PV_MAC, mesh_reduction, resident_a
        );

        for(int query=0; query<SA_COLS; ++query){
            #pragma HLS UNROLL
            const acc_t old_l = input.initialize
                ? accZero() : online.l[query];
            online.m[query] = new_max[query];
            online.l[query] = accUnit(
                alpha[query], old_l, rowsum[query]
            );
            output.l[query] = online.l[query];

            for(int feature=0; feature<SA_ROWS; ++feature){
                #pragma HLS PIPELINE II=1
                const acc_t old_o = input.initialize
                    ? accZero() : online.o[query][feature];
                online.o[query][feature] = accUnit(
                    alpha[query], old_o,
                    mesh_reduction[query][feature]
                );
                output.o[query][feature] = input.finalize
                    ? (online.l[query]==accZero()
                        ? accZero()
                        : online.o[query][feature]/online.l[query])
                    : online.o[query][feature];
            }
        }

        online.active = !input.finalize;
        output.normalized = input.finalize;
        output.executed_steps = legacyLogicalStepCount(
            input.initialize, input.finalize
        );
        output.request_done = true;
    }

    void fsa_stream_request_run(
        const FsaCoreRequestInput& input,
        FsaCoreRequestOutput& output
    ){
        #pragma HLS INLINE off
        static StreamOnlineState online{};

        output = FsaCoreRequestOutput{};
        output.request_ready = true;

        if(input.reset){
            reset_stream_online_state(online);
            if(!input.request_valid){
                return;
            }
        }
        if(!input.request_valid){
            return;
        }
        if(input.active_keys==0 || input.active_keys>SA_COLS){
            output.protocol_error = true;
            return;
        }
        if(!input.initialize && !online.active){
            output.protocol_error = true;
            return;
        }
        if(input.initialize){
            reset_stream_online_state(online);
            online.active = true;
        }

        stream_fsa_tile(online, input, output);
    }

}  // namespace fsa
