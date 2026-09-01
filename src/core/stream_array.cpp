#include "fsa/stream_array.hpp"

#include <hls_stream.h>

#include "fsa/arithmetic.hpp"

namespace fsa{
    namespace{

        using PeStream = hls::stream<StreamPeToken>;

        void macPeProcess(
            StreamPeState& state,
            const unsigned item_count,
            PeStream& left,
            PeStream& up,
            PeStream& right,
            PeStream& down
        ){
            #pragma HLS INLINE off
            for(unsigned item=0; item<item_count; ++item){
                #pragma HLS PIPELINE II=1
                const StreamPeToken horizontal = left.read();
                const StreamPeToken vertical = up.read();

                StreamPeToken operation = horizontal;
                operation.valid = horizontal.valid && vertical.valid;
                operation.vertical = vertical.vertical;

                const StreamPeOutput result =
                    stream_pe_step(state, operation);
                right.write(result.right);
                down.write(result.down);
            }
        }

        void feedMeshLeft(
            const elem_t data[SA_COLS][SA_ROWS],
            const unsigned item_count,
            const StreamPeOp op,
            PeStream horizontal[SA_ROWS][SA_COLS+1]
        ){
            #pragma HLS INLINE off
            for(unsigned item=0; item<item_count; ++item){
                #pragma HLS PIPELINE II=1
                for(int row=0; row<SA_ROWS; ++row){
                    #pragma HLS UNROLL
                    StreamPeToken token{};
                    token.valid = true;
                    token.last = item+1==item_count;
                    token.op = op;
                    token.tag = item;
                    if(op==StreamPeOp::PV_MAC){
                        token.horizontal = row<SA_COLS
                            ? data[row][item] : elemZero();
                    }else{
                        token.horizontal = data[item][row];
                    }
                    horizontal[row][0].write(token);
                }
            }
        }

        void feedMeshTop(
            const unsigned item_count,
            const StreamPeOp op,
            PeStream vertical[SA_ROWS+1][SA_COLS]
        ){
            #pragma HLS INLINE off
            for(unsigned item=0; item<item_count; ++item){
                #pragma HLS PIPELINE II=1
                for(int col=0; col<SA_COLS; ++col){
                    #pragma HLS UNROLL
                    StreamPeToken token{};
                    token.valid = true;
                    token.last = item+1==item_count;
                    token.op = op;
                    token.tag = item;
                    token.vertical = accZero();
                    vertical[0][col].write(token);
                }
            }
        }

        void collectMeshBottom(
            const unsigned item_count,
            PeStream vertical[SA_ROWS+1][SA_COLS],
            acc_t result[SA_COLS][SA_ROWS]
        ){
            #pragma HLS INLINE off
            for(unsigned item=0; item<item_count; ++item){
                #pragma HLS PIPELINE II=1
                for(int col=0; col<SA_COLS; ++col){
                    #pragma HLS UNROLL
                    const StreamPeToken token =
                        vertical[SA_ROWS][col].read();
                    result[col][item] = token.vertical;
                }
            }
        }

        void drainMeshRight(
            const unsigned item_count,
            PeStream horizontal[SA_ROWS][SA_COLS+1]
        ){
            #pragma HLS INLINE off
            for(unsigned item=0; item<item_count; ++item){
                #pragma HLS PIPELINE II=1
                for(int row=0; row<SA_ROWS; ++row){
                    #pragma HLS UNROLL
                    (void)horizontal[row][SA_COLS].read();
                }
            }
        }

        void runMacMesh(
            StreamPeState pe[SA_ROWS][SA_COLS],
            const elem_t data[SA_COLS][SA_ROWS],
            const unsigned item_count,
            const StreamPeOp op,
            acc_t result[SA_COLS][SA_ROWS]
        ){
            #pragma HLS INLINE off
            #pragma HLS DATAFLOW
            #pragma HLS ARRAY_PARTITION variable=pe type=complete dim=0
            #pragma HLS ARRAY_PARTITION variable=data type=complete dim=0
            #pragma HLS ARRAY_PARTITION variable=result type=complete dim=1

            PeStream horizontal[SA_ROWS][SA_COLS+1];
            PeStream vertical[SA_ROWS+1][SA_COLS];
            #pragma HLS STREAM variable=horizontal depth=4
            #pragma HLS STREAM variable=vertical depth=4

            feedMeshLeft(data, item_count, op, horizontal);
            feedMeshTop(item_count, op, vertical);

            for(int row=0; row<SA_ROWS; ++row){
                #pragma HLS UNROLL
                for(int col=0; col<SA_COLS; ++col){
                    #pragma HLS UNROLL
                    macPeProcess(
                        pe[row][col], item_count,
                        horizontal[row][col], vertical[row][col],
                        horizontal[row][col+1], vertical[row+1][col]
                    );
                }
            }

            collectMeshBottom(item_count, vertical, result);
            drainMeshRight(item_count, horizontal);
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
        #pragma HLS ALLOCATION function instances=stream_pe_step limit=FSA_SA_ROWS*FSA_SA_COLS

        StreamPeState pe[SA_ROWS][SA_COLS]{};
        acc_t score[SA_COLS][SA_ROWS]{};
        acc_t pv[SA_COLS][SA_ROWS]{};
        elem_t k_tile[SA_COLS][SA_ROWS]{};
        elem_t v_tile[SA_COLS][SA_ROWS]{};
        #pragma HLS ARRAY_PARTITION variable=pe type=complete dim=0
        #pragma HLS ARRAY_PARTITION variable=score type=complete dim=1
        #pragma HLS ARRAY_PARTITION variable=pv type=complete dim=1
        #pragma HLS ARRAY_PARTITION variable=k_tile type=complete dim=0
        #pragma HLS ARRAY_PARTITION variable=v_tile type=complete dim=0

        for(int col=0; col<SA_COLS; ++col){
            #pragma HLS PIPELINE II=1
            for(int row=0; row<SA_ROWS; ++row){
                #pragma HLS UNROLL
                StreamPeToken load{};
                load.valid = true;
                load.op = StreamPeOp::LOAD_Q;
                load.horizontal = input.q[col][row];
                stream_pe_step(pe[row][col], load);
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

        // Q驻留在PE中，K按key token流过二维阵列；底部直接得到score。
        runMacMesh(
            pe, k_tile, (unsigned)input.active_keys,
            StreamPeOp::QK_MAC, score
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

        // CMP热路径只做compare/mux；score保留在本地restream缓冲，随后
        // 回灌原PE位置，不进入Scratchpad/Accumulator RAM。
        for(int key=0; key<SA_COLS; ++key){
            #pragma HLS PIPELINE II=1
            for(int query=0; query<SA_COLS; ++query){
                #pragma HLS UNROLL
                const bool padding = key>=(int)input.active_keys;
                const bool causal_mask = input.causal &&
                    input.key_base+(unsigned)key>
                    input.query_base+(unsigned)query;
                if(!padding && !causal_mask){
                    tile_max[query] = accMax(
                        tile_max[query], score[query][key]
                    );
                }
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
        }

        // score从CMP restream缓冲回灌原PE。减max、缩放和随后8拍
        // PWL全部复用该PE的同一条FMA，N/P不移出阵列。
        for(int row=0; row<SA_ROWS; ++row){
            #pragma HLS UNROLL
            for(int query=0; query<SA_COLS; ++query){
                #pragma HLS UNROLL
                const bool padding = row>=(int)input.active_keys;
                const bool causal_mask = input.causal &&
                    input.key_base+(unsigned)row>
                    input.query_base+(unsigned)query;
                StreamPeToken load_score{};
                load_score.valid = true;
                load_score.op = StreamPeOp::LOAD_SCORE;
                load_score.masked = padding || causal_mask;
                load_score.horizontal = load_score.masked
                    ? elemZero()
                    : cvtAtoE(score[query][row]);
                stream_pe_step(pe[row][query], load_score);
            }
        }

        for(int row=0; row<SA_ROWS; ++row){
            #pragma HLS UNROLL
            for(int query=0; query<SA_COLS; ++query){
                #pragma HLS UNROLL
                const bool padding = row>=(int)input.active_keys;
                const bool causal_mask = input.causal &&
                    input.key_base+(unsigned)row>
                    input.query_base+(unsigned)query;
                StreamPeToken subtract{};
                subtract.valid = !padding && !causal_mask;
                subtract.op = StreamPeOp::SUB_MAX;
                subtract.horizontal = elemOne();
                subtract.vertical = -new_max[query];
                stream_pe_step(pe[row][query], subtract);
            }
        }

        for(int row=0; row<SA_ROWS; ++row){
            #pragma HLS UNROLL
            for(int query=0; query<SA_COLS; ++query){
                #pragma HLS UNROLL
                const bool padding = row>=(int)input.active_keys;
                const bool causal_mask = input.causal &&
                    input.key_base+(unsigned)row>
                    input.query_base+(unsigned)query;
                StreamPeToken scale{};
                scale.valid = !padding && !causal_mask;
                scale.op = StreamPeOp::SCALE_SCORE;
                scale.horizontal = elemAttentionScale();
                scale.vertical = accZero();
                stream_pe_step(pe[row][query], scale);
            }
        }

        for(int piece=0; piece<exp2PWLPieces; ++piece){
            #pragma HLS PIPELINE II=1
            for(int row=0; row<SA_ROWS; ++row){
                #pragma HLS UNROLL
                for(int query=0; query<SA_COLS; ++query){
                    #pragma HLS UNROLL
                    const bool padding = row>=(int)input.active_keys;
                    const bool causal_mask = input.causal &&
                        input.key_base+(unsigned)row>
                        input.query_base+(unsigned)query;
                    StreamPeToken coefficient{};
                    coefficient.valid = !padding && !causal_mask;
                    coefficient.last = piece==exp2PWLPieces-1;
                    coefficient.op = StreamPeOp::EXP2_PWL;
                    coefficient.horizontal = peExp2Slope(
                        (exp2_counter_t)piece
                    );
                    coefficient.vertical = exp2PWLIntercept(
                        (exp2_counter_t)piece
                    );
                    stream_pe_step(pe[row][query], coefficient);
                }
            }
        }

        // rowsum直接读取PE resident P。每列自顶向下形成一条FMA链，
        // 不把P写出阵列。
        for(int row=0; row<SA_ROWS; ++row){
            #pragma HLS PIPELINE II=1
            for(int query=0; query<SA_COLS; ++query){
                #pragma HLS UNROLL
                StreamPeToken sum{};
                sum.valid = true;
                sum.op = StreamPeOp::ROWSUM_MAC;
                sum.horizontal = elemOne();
                sum.vertical = rowsum[query];
                rowsum[query] =
                    stream_pe_step(pe[row][query], sum).down.vertical;
            }
        }

        // V按value feature流过仍保存P的阵列，底部产生PV。
        runMacMesh(
            pe, v_tile, (unsigned)SA_ROWS,
            StreamPeOp::PV_MAC, pv
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
                    alpha[query], old_o, pv[query][feature]
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
