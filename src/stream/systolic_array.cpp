#include "fsa/stream/common.hpp"

#include <utils/x_hls_utils.h>

namespace fsa{
namespace streaming_v2_detail{

    // 与FSA execution plan一致：8拍依次从SA左侧广播FP16 PWL斜率。
    const elem_t EXP2_SLOPES[exp2PWLPieces] = {
        (elem_t)0.664062500F,
        (elem_t)0.608886719F,
        (elem_t)0.558105469F,
        (elem_t)0.512207031F,
        (elem_t)0.469482422F,
        (elem_t)0.430419922F,
        (elem_t)0.394775391F,
        (elem_t)0.362060547F
    };

    bool laneEnabled(
        const TileMeta& meta,
        const int query,
        const int key
    ){
        #pragma HLS INLINE
        if(query>=meta.active_queries.to_int() ||
                key>=meta.active_keys.to_int()){
            return false;
        }
        return true;
    }

    /**
     * @brief 有限FP32的组合max选择
     *
     * Attention score不会产生NaN。直接比较IEEE位序可以保留
     * Scala CMP中的组合max mux，避免HLS生成一个多拍浮点
     * compare并再次形成key间反馈。CMP的浮点差值通路仍由
     * result.max_diff的四个列通路保留。
     */
    acc_t finiteAccMax(const acc_t a, const acc_t b){
        #pragma HLS INLINE
        const fp_struct<acc_t> a_view(a);
        const fp_struct<acc_t> b_view(b);
        const ap_uint<32> a_bits = a_view.data();
        const ap_uint<32> b_bits = b_view.data();
        const bool a_sign = a_bits[31];
        const bool b_sign = b_bits[31];

        if(a_sign != b_sign){
            return a_sign ? b : a;
        }
        if(a_sign){
            return a_bits<b_bits ? a : b;
        }
        return a_bits>b_bits ? a : b;
    }


    enum class PeWaveDirection : std::uint8_t{
        UP = 0,
        DOWN = 1
    };

    /**
     * 流过SA的一个控制波。partial对应Scala PE的上/下方数据，
     * index在QK时是key、PWL时是piece、PV时是feature。
     *
     * 成员刻意不设默认值：临时wave用`{}`显式生成bubble，环形存储则
     * 依靠写前不读协议，避免HLS在每个tile入口综合出整表清零循环。
     */
    struct PeWave{
        bool valid;
        PeWaveOp op;
        PeWaveDirection direction;
        PeWaveIndex index;
        acc_t partial[SA_COLS];
        elem_t element[SA_COLS];
        bool exp2_match[SA_COLS];
    };

    /** score从顶部CMP逐拍向下回流；key标签决定在哪一行写入PE.reg。 */
    struct ScoreWave{
        bool valid;
        PeWaveIndex key;
        elem_t score[SA_COLS];
    };

    /**
     * @brief 每列CMP的多周期输出通路
     *
     * oldMax/newMax/exp2_counter均按值传入，函数内部不修改CMP状态。
     * 这样FP32差值和FP32到FP16转换可以保持II=1流水，而不会把函数
     * latency错误地放到newMax的逐拍反馈环上。状态寄存器仍然只有
     * SA_COLS组，并在SpatialCmpColumns中与该输出通路一一对应。
     */
    template<int COL>
    acc_t spatialCmpOutputCell(
        const bool valid,
        const CmpWaveOp op,
        const bool input_enabled,
        const acc_t d_input,
        const acc_t old_max,
        const acc_t new_max,
        const exp2_counter_t exp2_counter
    ){
        static_assert(COL>=0 && COL<SA_COLS, "CMP col out of range");
        #pragma HLS INLINE off
        #pragma HLS PIPELINE II=1

        if(!valid || op==CmpWaveOp::HOLD){
            return accZero();
        }

        if(op==CmpWaveOp::RESET){
            return accZero();
        }

        if(op==CmpWaveOp::UPDATE){
            const acc_t masked_input = input_enabled
                ? d_input : accMinimum();
            // 与Scala CMP一致：score从CMP向下返回前先缩为elem_t。
            return viewEasA(cvtAtoE(masked_input));
        }

        if(op==CmpWaveOp::PROP_EXP2_INTERCEPTS){
            return exp2PWLIntercept(exp2_counter);
        }

        if(op==CmpWaveOp::PROP_ZERO){
            return accZero();
        }

        const acc_t lhs = op==CmpWaveOp::PROP_MAX
            ? accZero() : old_max;
        const CmpUnitOutput cmp_output = accCmp(lhs, new_max);
        return cmp_output.out_diff;
    }

    template<int COL>
    struct SpatialCmpColumns{
        static void run(
            const bool valid,
            const CmpWaveOp op,
            const int key,
            const int causal_counter,
            const TileMeta& meta,
            const acc_t d_input[SA_COLS],
            CMPState state[SA_COLS],
            acc_t d_output[SA_COLS]
        ){
            #pragma HLS INLINE
            const bool enabled = op!=CmpWaveOp::UPDATE || (
                laneEnabled(meta, COL, key) &&
                (!meta.causal || causal_counter==0)
            );
            const acc_t old_max = state[COL].oldMax;
            const acc_t new_max = state[COL].newMax;
            const exp2_counter_t exp2_counter = state[COL].exp2_counter;

            // 只有这个组合max位于连续score之间的真实反馈环。它不经过
            // cvtAtoE或accCmp，因而下一拍可以立即读取更新后的newMax。
            if(valid && op==CmpWaveOp::UPDATE){
                const acc_t masked_input = enabled
                    ? d_input[COL] : accMinimum();
                state[COL].newMax = finiteAccMax(
                    masked_input, new_max
                );
            }else if(valid && op==CmpWaveOp::RESET){
                state[COL].oldMax = accMinimum();
                state[COL].newMax = accMinimum();
            }else if(valid && op==CmpWaveOp::PROP_MAX_DIFF){
                state[COL].oldMax = new_max;
            }else if(valid && op==CmpWaveOp::PROP_EXP2_INTERCEPTS){
                state[COL].exp2_counter = exp2_counter+1;
            }

            d_output[COL] = spatialCmpOutputCell<COL>(
                valid, op, enabled, d_input[COL],
                old_max, new_max, exp2_counter
            );
            SpatialCmpColumns<COL+1>::run(
                valid, op, key,
                causal_counter>0 ? causal_counter-1 : 0,
                meta, d_input, state, d_output
            );
        }
    };

    template<>
    struct SpatialCmpColumns<SA_COLS>{
        static void run(
            const bool,
            const CmpWaveOp,
            const int,
            const int,
            const TileMeta&,
            const acc_t[SA_COLS],
            CMPState[SA_COLS],
            acc_t[SA_COLS]
        ){
            #pragma HLS INLINE
        }
    };

    /**
     * 每个坐标特化为一个不内联的PE层次。这不是为不同坐标
     * 实现不同算法，而是告诉HLS这些调用是同时存在的空间PE，
     * 不得把它们折叠为少量共享运算器。每个PE内部仍只有一个
     * 原始peMacUnit，MAC和exp2按控制时分复用同一条通路。
     */
    template<int ROW, int COL>
    PeMacUnitOutput spatialPeCell(
        const elem_t operand_a,
        const elem_t operand_b,
        const acc_t operand_c,
        const bool exp2_mode
    ){
        static_assert(ROW>=0 && ROW<SA_ROWS, "PE row out of range");
        static_assert(COL>=0 && COL<SA_COLS, "PE col out of range");
        #pragma HLS INLINE off
        #pragma HLS PIPELINE II=1
        #pragma HLS LATENCY min=9 max=9
        return peMacUnit(
            operand_a, operand_b, operand_c, exp2_mode
        );
    }

    template<int ROW, int COL>
    struct SpatialPeColumns{
        static void run(
            PeWave& wave,
            const elem_t horizontal[SA_ROWS],
            elem_t pe_register[SA_ROWS][SA_COLS],
            const bool active[SA_ROWS][SA_COLS]
        ){
            #pragma HLS INLINE
            const bool reduce = wave.op==PeWaveOp::QK ||
                wave.op==PeWaveOp::ROW_SUM ||
                wave.op==PeWaveOp::PV;
            const bool use_probability = wave.op==PeWaveOp::ROW_SUM ||
                wave.op==PeWaveOp::PV;
            // A tile no longer clears every PE register before the cycle loop.
            // Invalid bubbles must therefore not observe a register before the
            // feeder has written it.  Every valid operation is launched only
            // after the corresponding Q/score/probability value is resident.
            const elem_t operand_a = !wave.valid ||
                    (use_probability && !active[ROW][COL])
                ? elemZero() : pe_register[ROW][COL];
            const bool exp2_mode = wave.op==PeWaveOp::PWL;

            const PeMacUnitOutput unit = spatialPeCell<ROW, COL>(
                operand_a,
                horizontal[ROW],
                wave.partial[COL],
                exp2_mode
            );

            if(wave.valid && reduce){
                wave.partial[COL] = unit.out_accType;
            }else if(wave.valid && active[ROW][COL] &&
                    (wave.op==PeWaveOp::SUB_MAX ||
                     wave.op==PeWaveOp::SCALE)){
                wave.element[COL] = unit.out_elemType;
            }else if(wave.valid && active[ROW][COL] &&
                    wave.op==PeWaveOp::PWL){
                wave.element[COL] = unit.out_elemType;
                wave.exp2_match[COL] = unit.out_exp2;
            }

            SpatialPeColumns<ROW, COL+1>::run(
                wave,
                horizontal,
                pe_register,
                active
            );
        }
    };

    template<int ROW>
    struct SpatialPeColumns<ROW, SA_COLS>{
        static void run(
            PeWave&,
            const elem_t[SA_ROWS],
            elem_t[SA_ROWS][SA_COLS],
            const bool[SA_ROWS][SA_COLS]
        ){
            #pragma HLS INLINE
        }
    };

    template<int ROW>
    struct SpatialPeRowsTick{
        static void run(
            PeWave wave[SA_ROWS],
            const elem_t horizontal[SA_ROWS],
            elem_t pe_register[SA_ROWS][SA_COLS],
            const bool active[SA_ROWS][SA_COLS]
        ){
            #pragma HLS INLINE
            SpatialPeColumns<ROW, 0>::run(
                wave[ROW],
                horizontal,
                pe_register,
                active
            );
            SpatialPeRowsTick<ROW+1>::run(
                wave,
                horizontal,
                pe_register,
                active
            );
        }
    };

    template<>
    struct SpatialPeRowsTick<SA_ROWS>{
        static void run(
            PeWave[SA_ROWS],
            const elem_t[SA_ROWS],
            elem_t[SA_ROWS][SA_COLS],
            const bool[SA_ROWS][SA_COLS]
        ){
            #pragma HLS INLINE
        }
    };

    /**
     * @brief 一套常驻的CMP + PE阵列完成一个KV tile
     *
     * 本函数内部只有一个CMP调用点和一个PE调用点。所有QK、softmax、
     * row-sum和PV命令进入同一个II=1调度循环，因而不会再把每个wave
     * 解释成一次ap_ctrl_hs子模块事务。结构固定为SA_COLS个列头CMP以及
     * SA_ROWS x SA_COLS个PE，规模随配置参数变化。
     */

    /**
     * @brief 一套常驻CMP + PE阵列的逐拍token引擎
     *
     * QK token从底行向上，score经顶部CMP后每拍向下一行restream；
     * softmax、row-sum和PV token从顶部向下。每个(row,col)只有一个
     * spatialPeCell调用点，多个token可同时驻留在各PE的FMA流水级中。
     * S/N/P始终只保存在pe_register，不再物化外部score/probability阵列。
     */
    void spatialSystolicArrayTileTick(
        const TileMeta& meta,
        const elem_t q_tile[SA_COLS][SA_ROWS],
        const elem_t k_tile[SA_COLS][SA_ROWS],
        const elem_t v_tile[SA_COLS][SA_ROWS],
        CMPState cmp_state[SA_COLS],
        SaCycleControlStream& cycle_control_stream,
        SaResultStream& result_stream
    ){
        #pragma HLS INLINE off
        #pragma HLS ARRAY_PARTITION variable=q_tile type=complete dim=0
        #pragma HLS ARRAY_PARTITION variable=k_tile type=complete dim=0
        #pragma HLS ARRAY_PARTITION variable=v_tile type=complete dim=2
        #pragma HLS ARRAY_PARTITION variable=cmp_state type=complete dim=1

        // These arrays are completely written before their first meaningful
        // read.  Avoid aggregate initialization here: in HLS it becomes a
        // serialized per-tile clear loop and hides the II=1 wavefront gain.
        elem_t pe_register[SA_ROWS][SA_COLS];
        bool active[SA_ROWS][SA_COLS];
        PeWave pe_pipeline[SA_ROWS][PE_HOP_CYCLES];
        ScoreWave score_pipeline[SA_ROWS];
        #pragma HLS ARRAY_PARTITION variable=pe_register complete dim=0
        #pragma HLS ARRAY_PARTITION variable=active complete dim=0
        #pragma HLS ARRAY_PARTITION variable=pe_pipeline complete dim=0
        #pragma HLS ARRAY_PARTITION variable=score_pipeline complete dim=1

        for(int row=0; row<SA_ROWS; ++row){
            #pragma HLS UNROLL
            for(int query=0; query<SA_COLS; ++query){
                #pragma HLS UNROLL
                active[row][query] = row<SA_COLS &&
                    laneEnabled(meta, query, row);
            }
        }

        for(int cycle=0; cycle<SA_TILE_CYCLES; ++cycle){
            #pragma HLS PIPELINE II=1
            #pragma HLS LOOP_FLATTEN off
            // 调度器保证下一次读取发生在对应commit后；同拍RAW仍保留。
            #pragma HLS DEPENDENCE variable=pe_register inter false

            const SaCycleControl cycle_control =
                cycle_control_stream.read();

            const int pipeline_slot = cycle%PE_HOP_CYCLES;
            PeWave row_input[SA_ROWS]{};
            PeWave row_result[SA_ROWS]{};
            #pragma HLS ARRAY_PARTITION variable=row_input complete dim=0
            #pragma HLS ARRAY_PARTITION variable=row_result complete dim=0

            PeWave qk_at_cmp{};
            PeWave bottom_result{};

            // 环形槽在前PE_HOP_CYCLES拍被逐槽写满；之后每次读取的槽都已
            // 由同一tile写过，因此不需要在tile入口清空整个环形缓冲。
            for(int row=0; row<SA_ROWS; ++row){
                #pragma HLS UNROLL
                if(cycle>=PE_HOP_CYCLES){
                    const PeWave completed =
                        pe_pipeline[row][pipeline_slot];
                    if(completed.valid){
                        for(int query=0; query<SA_COLS; ++query){
                            #pragma HLS UNROLL
                            if(active[row][query] &&
                                    (completed.op==PeWaveOp::SUB_MAX ||
                                     completed.op==PeWaveOp::SCALE)){
                                pe_register[row][query] =
                                    completed.element[query];
                            }else if(active[row][query] &&
                                    completed.op==PeWaveOp::PWL &&
                                    completed.exp2_match[query]){
                                pe_register[row][query] =
                                    completed.element[query];
                            }
                        }

                        if(completed.direction==PeWaveDirection::UP){
                            if(row==0){
                                qk_at_cmp = completed;
                            }else{
                                row_input[row-1] = completed;
                            }
                        }else if(row+1==SA_ROWS){
                            bottom_result = completed;
                        }else{
                            row_input[row+1] = completed;
                        }
                    }
                }
            }

            // ExecutionPlan每拍允许装入一个query列；数据已经依次经过
            // Scratchpad的一拍整行读边界和InputDelayer。
            if(cycle_control.load_query){
                const int query_index =
                    cycle_control.query_index.to_int();
                for(int row=0; row<SA_ROWS; ++row){
                    #pragma HLS UNROLL
                    pe_register[row][query_index] =
                        q_tile[query_index][row];
                }
            }

            // 连续SA_COLS拍从阵列底部启动QK wave。
            if(cycle_control.launch_qk){
                PeWave source{};
                #pragma HLS ARRAY_PARTITION variable=source.partial complete dim=1
                source.valid = true;
                source.op = PeWaveOp::QK;
                source.direction = PeWaveDirection::UP;
                source.index = cycle_control.qk_index;
                row_input[SA_ROWS-1] = source;
            }

            CmpWaveOp cmp_op = cycle_control.cmp_op;
            bool cmp_valid = cycle_control.cmp_valid;
            int cmp_item = cycle_control.cmp_item.to_int();

            acc_t cmp_input[SA_COLS]{};
            acc_t cmp_output[SA_COLS]{};
            #pragma HLS ARRAY_PARTITION variable=cmp_input complete dim=1
            #pragma HLS ARRAY_PARTITION variable=cmp_output complete dim=1
            for(int query=0; query<SA_COLS; ++query){
                #pragma HLS UNROLL
                cmp_input[query] = qk_at_cmp.partial[query];
            }
            SpatialCmpColumns<0>::run(
                cmp_valid, cmp_op, cmp_item, cmp_item, meta,
                cmp_input, cmp_state, cmp_output
            );

            ScoreWave injected_score{};
            #pragma HLS ARRAY_PARTITION variable=injected_score.score complete dim=1
            if(cmp_op==CmpWaveOp::UPDATE){
                injected_score.valid = true;
                injected_score.key = (PeWaveIndex)cmp_item;
                for(int query=0; query<SA_COLS; ++query){
                    #pragma HLS UNROLL
                    injected_score.score[query] =
                        viewAasE(cmp_output[query]);
                }
            }

            // Score从CMP每拍向下一行移动；到tag对应行时原地覆盖Q。
            ScoreWave next_score_pipeline[SA_ROWS]{};
            #pragma HLS ARRAY_PARTITION variable=next_score_pipeline complete dim=1
            for(int row=0; row<SA_ROWS; ++row){
                #pragma HLS UNROLL
                ScoreWave score_wave{};
                if(row==0 && injected_score.valid){
                    score_wave = injected_score;
                }else if(cycle>0){
                    // cycle 0 writes every score slot before any later read.
                    score_wave = score_pipeline[row];
                }
                if(score_wave.valid){
                    if(score_wave.key.to_int()==row){
                        for(int query=0; query<SA_COLS; ++query){
                            #pragma HLS UNROLL
                            pe_register[row][query] = active[row][query]
                                ? score_wave.score[query] : elemZero();
                        }
                    }
                    if(row+1<SA_ROWS){
                        next_score_pipeline[row+1] = score_wave;
                    }
                }
            }
            for(int row=0; row<SA_ROWS; ++row){
                #pragma HLS UNROLL
                score_pipeline[row] = next_score_pipeline[row];
            }

            PeWave down_source{};
            #pragma HLS ARRAY_PARTITION variable=down_source.partial complete dim=1
            down_source.valid = cycle_control.launch_down;
            down_source.op = cycle_control.down_op;
            down_source.index = cycle_control.down_item;
            down_source.direction = PeWaveDirection::DOWN;
            for(int query=0; query<SA_COLS; ++query){
                #pragma HLS UNROLL
                if(down_source.op==PeWaveOp::SUB_MAX ||
                        down_source.op==PeWaveOp::PWL ||
                        down_source.op==PeWaveOp::ROW_SUM){
                    down_source.partial[query] = cmp_output[query];
                }
            }
            if(down_source.valid){
                row_input[0] = down_source;
            }

            elem_t horizontal[SA_ROWS]{};
            #pragma HLS ARRAY_PARTITION variable=horizontal complete dim=1
            for(int row=0; row<SA_ROWS; ++row){
                #pragma HLS UNROLL
                const PeWaveOp op = row_input[row].op;
                const int item = row_input[row].index.to_int();
                if(op==PeWaveOp::QK){
                    horizontal[row] = k_tile[item][row];
                }else if(op==PeWaveOp::SCALE){
                    horizontal[row] = elemAttentionScale();
                }else if(op==PeWaveOp::PWL){
                    horizontal[row] = EXP2_SLOPES[item];
                }else if(op==PeWaveOp::PV){
                    horizontal[row] = row<SA_COLS
                        ? v_tile[row][item] : elemZero();
                }else{
                    horizontal[row] = elemOne();
                }
                row_result[row] = row_input[row];
            }

            SpatialPeRowsTick<0>::run(
                row_result, horizontal, pe_register, active
            );
            for(int row=0; row<SA_ROWS; ++row){
                #pragma HLS UNROLL
                pe_pipeline[row][pipeline_slot] = row_result[row];
            }

            if(cmp_op==CmpWaveOp::PROP_MAX_DIFF){
                SaResultToken token{};
                #pragma HLS ARRAY_PARTITION variable=token.data complete dim=1
                token.kind = SaResultKind::MAX_DIFF;
                token.initialize = meta.initialize;
                token.finalize = meta.finalize;
                token.active_queries = meta.active_queries;
                for(int query=0; query<SA_COLS; ++query){
                    #pragma HLS UNROLL
                    token.data[query] = cmp_output[query];
                }
                result_stream.write(token);
            }else if(bottom_result.valid &&
                    bottom_result.op==PeWaveOp::ROW_SUM){
                SaResultToken token{};
                #pragma HLS ARRAY_PARTITION variable=token.data complete dim=1
                token.kind = SaResultKind::ROW_SUM;
                for(int query=0; query<SA_COLS; ++query){
                    #pragma HLS UNROLL
                    token.data[query] = bottom_result.partial[query];
                }
                result_stream.write(token);
            }else if(bottom_result.valid &&
                    bottom_result.op==PeWaveOp::PV){
                SaResultToken token{};
                #pragma HLS ARRAY_PARTITION variable=token.data complete dim=1
                token.kind = SaResultKind::PV;
                token.index = bottom_result.index;
                for(int query=0; query<SA_COLS; ++query){
                    #pragma HLS UNROLL
                    token.data[query] = bottom_result.partial[query];
                }
                result_stream.write(token);
            }

        }
    }

    void systolicArrayProcess(
        const unsigned length,
        const bool causal,
        CoreControlStream& control_stream,
        SaCycleControlStream& cycle_control_stream,
        DelayedElemStream& delayed_sa_stream,
        SaResultStream& sa_result_stream
    ){
        #pragma HLS INLINE off

        elem_t q_tile[SA_COLS][SA_ROWS]{};
        elem_t k_tile[SA_COLS][SA_ROWS]{};
        elem_t v_tile[SA_COLS][SA_ROWS]{};
        CMPState cmp_state[SA_COLS]{};
        #pragma HLS ARRAY_PARTITION variable=q_tile type=complete dim=0
        #pragma HLS ARRAY_PARTITION variable=k_tile type=complete dim=0
        #pragma HLS ARRAY_PARTITION variable=v_tile type=complete dim=2
        #pragma HLS ARRAY_PARTITION variable=cmp_state type=complete dim=1

        const unsigned tiles = tileCount(length);
        for(unsigned query_tile=0; query_tile<tiles; ++query_tile){
            #pragma HLS LOOP_TRIPCOUNT min=1 max=DMA_MAX_SEQUENCE_TILES
            const unsigned key_tiles = keyTileCountForQuery(
                query_tile, tiles, causal
            );
            for(unsigned key_tile=0; key_tile<key_tiles; ++key_tile){
                #pragma HLS LOOP_TRIPCOUNT min=1 max=DMA_MAX_SEQUENCE_TILES
                const CoreTileControl control = control_stream.read();
                const TileMeta meta = control.meta;

                // InputDelayer输出保持逐拍波前协议。当前SA微程序仍使用
                // tile寄存器作发射源，因此只在SA边界恢复坐标，不再在
                // Delayer actor中缓存/复制三套完整tile。
                const int phase_count = key_tile==0 ? 3 : 2;
                for(int beat_index=0;
                        beat_index<phase_count*(SA_COLS+SA_ROWS-1);
                        ++beat_index){
                    #pragma HLS PIPELINE II=1
                    const DelayedElemBeat beat = delayed_sa_stream.read();
                    const int cycle = beat.cycle.to_int();
                    for(int output_lane=0;
                            output_lane<SA_ROWS; ++output_lane){
                        #pragma HLS UNROLL
                        const int internal_lane = beat.layout.rev_output
                            ? SA_ROWS-1-output_lane : output_lane;
                        const int feature = beat.layout.rev_input
                            ? SA_ROWS-1-internal_lane : internal_lane;
                        const int source_lane = cycle-
                            (beat.layout.delay_output ? internal_lane : 0);
                        for(int lane=0; lane<SA_COLS; ++lane){
                            #pragma HLS UNROLL
                            if(source_lane==lane){
                                if(beat.phase==DelayerPhase::LOAD_Q){
                                    q_tile[lane][feature] =
                                        beat.data[output_lane];
                                }else if(beat.phase==DelayerPhase::SCORE_K){
                                    k_tile[lane][feature] =
                                        beat.data[output_lane];
                                }else{
                                    v_tile[lane][feature] =
                                        beat.data[output_lane];
                                }
                            }
                        }
                    }
                }

                spatialSystolicArrayTileTick(
                    meta, q_tile, k_tile, v_tile,
                    cmp_state, cycle_control_stream, sa_result_stream
                );
            }
        }
    }

}  // namespace streaming_v2_detail
}  // namespace fsa
