#include "fsa/stream/split_d/split_d_internal.hpp"

#include <utils/x_hls_utils.h>

#include "fsa/stream/accumulator.hpp"
#include "fsa/stream/split_d/split_d_types.hpp"

namespace fsa{
namespace split_d{
namespace detail{

    static_assert(PV_INTERLEAVE<=HEAD_DIM,
                  "PV交错上下文不能超过head dimension");

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

    elem_t splitDAttentionScaleElem(){
        #pragma HLS INLINE
        const fp_struct<elem_t> view(
            (ap_uint<16>)ATTENTION_SCALE_ELEM_BITS
        );
        return view.to_ieee();
    }

    acc_t splitDAttentionScaleAcc(){
        #pragma HLS INLINE
        return (acc_t)ATTENTION_SCALE_ACC_VALUE;
    }

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

    void clearOperands(elem_t operand_b[PE_DIM][QUERY_BLOCK_COLS], acc_t operand_c[PE_DIM][QUERY_BLOCK_COLS]){
        #pragma HLS INLINE
        for(int row=0; row<PE_DIM; ++row){
            #pragma HLS UNROLL
            for(int col=0; col<QUERY_BLOCK_COLS; ++col){
                #pragma HLS UNROLL
                operand_b[row][col] = elemZero();
                operand_c[row][col] = accZero();
            }
        }
    }

}  // namespace detail

namespace detail{

    template<int BLOCK_ID>
    void runQueryBlock(hls::stream<QueryTileHeader>& headers, hls::stream<elem_t>& q_input, hls::stream<KeyValuePacket>& kv_input, hls::stream<acc_t>& output){
        #pragma HLS INLINE off
        #pragma HLS ALLOCATION function instances=detail::runPeArray limit=1
        #pragma HLS ALLOCATION function instances=detail::runAccumulatorColumns limit=1
        const QueryTileHeader header = headers.read();
        const unsigned query_base = header.query_base;
        const unsigned length = header.length;
        const unsigned key_tiles = header.key_tiles;
        const bool causal = header.causal;
        const unsigned offset = (unsigned)(BLOCK_ID*QUERY_BLOCK_COLS);
        const unsigned remaining = header.active_queries>offset ? header.active_queries-offset : 0U;
        const unsigned active_queries = remaining<(unsigned)QUERY_BLOCK_COLS ? remaining : (unsigned)QUERY_BLOCK_COLS;
        elem_t q_tile[QUERY_BLOCK_COLS][HEAD_DIM]{};
        #pragma HLS ARRAY_PARTITION variable=q_tile complete dim=1
        for(int col=0; col<QUERY_BLOCK_COLS; ++col){
            for(int feature=0; feature<HEAD_DIM; ++feature){
                #pragma HLS PIPELINE II=1
                q_tile[col][feature] = q_input.read();
            }
        }
        acc_t output_acc[QUERY_BLOCK_COLS][HEAD_DIM]{};
        acc_t running_max[QUERY_BLOCK_COLS]{};
        acc_t running_sum[QUERY_BLOCK_COLS]{};
        bool history_valid[QUERY_BLOCK_COLS]{};
        #pragma HLS ARRAY_PARTITION variable=q_tile complete dim=1
        #pragma HLS ARRAY_PARTITION variable=output_acc complete dim=1
        #pragma HLS ARRAY_PARTITION variable=running_max complete dim=1
        #pragma HLS ARRAY_PARTITION variable=running_sum complete dim=1
        #pragma HLS ARRAY_PARTITION variable=history_valid complete dim=1

        for(int col=0; col<QUERY_BLOCK_COLS; ++col){
            #pragma HLS UNROLL
            running_max[col] = accMinimum();
            running_sum[col] = accZero();
            history_valid[col] = false;
        }

        for(unsigned key_tile=0; key_tile<key_tiles; ++key_tile){
            #pragma HLS LOOP_TRIPCOUNT min=1 max=MAX_SEQUENCE_TILES
            const unsigned key_base = key_tile*(unsigned)PE_DIM;
            const unsigned remaining_keys = length-key_base;
            const unsigned active_keys =
                remaining_keys<(unsigned)PE_DIM
                    ? remaining_keys : (unsigned)PE_DIM;

            elem_t k_tile[PE_DIM][HEAD_DIM]{};
            elem_t v_tile[PE_DIM][HEAD_DIM]{};
            PeState pe[PE_DIM][QUERY_BLOCK_COLS]{};
            elem_t operand_b[PE_DIM][QUERY_BLOCK_COLS]{};
            acc_t operand_c[PE_DIM][QUERY_BLOCK_COLS]{};
            PeMacUnitOutput pe_result[PE_DIM][QUERY_BLOCK_COLS]{};
            bool score_valid[PE_DIM][QUERY_BLOCK_COLS]{};
            #pragma HLS ARRAY_PARTITION variable=k_tile complete dim=1
            #pragma HLS ARRAY_PARTITION variable=v_tile complete dim=1
            #pragma HLS ARRAY_PARTITION variable=pe complete dim=0
            #pragma HLS ARRAY_PARTITION variable=operand_b complete dim=0
            #pragma HLS ARRAY_PARTITION variable=operand_c complete dim=0
            #pragma HLS ARRAY_PARTITION variable=pe_result complete dim=0
            #pragma HLS ARRAY_PARTITION variable=score_valid complete dim=0

            // 即使本块无有效query，也按同一计数消费广播，不跳读causal数据。
            for(int row=0; row<PE_DIM; ++row){
                for(int feature=0; feature<HEAD_DIM; ++feature){
                    #pragma HLS PIPELINE II=1
                    const KeyValuePacket packet = kv_input.read();
                    k_tile[row][feature] = packet.key;
                    v_tile[row][feature] = packet.value;
                }
            }

            for(int row=0; row<PE_DIM; ++row){
                #pragma HLS UNROLL
                for(int col=0; col<QUERY_BLOCK_COLS; ++col){
                    #pragma HLS UNROLL
                    pe[row][col].reg = elemZero();
                    pe[row][col].score_acc = accZero();
                    const unsigned global_query =
                        query_base+(unsigned)(BLOCK_ID*QUERY_BLOCK_COLS+col);
                    const unsigned global_key = key_base+(unsigned)row;
                    score_valid[row][col] =
                        (unsigned)col<active_queries &&
                        (unsigned)row<active_keys &&
                        (!causal || global_key<=global_query);
                }
            }

            // QK的block/lane循环和operand准备都已移入runPeAccumulateTile，
            // 累加值只在函数内部的PE.score_acc上传递，这里只发起一次调用。
            detail::runPeAccumulateTile(
                q_tile, k_tile, active_queries, active_keys, pe
            );

            acc_t next_max[QUERY_BLOCK_COLS]{};
            acc_t alpha[QUERY_BLOCK_COLS]{};
            bool tile_has_value[QUERY_BLOCK_COLS]{};
            #pragma HLS ARRAY_PARTITION variable=next_max complete dim=1
            #pragma HLS ARRAY_PARTITION variable=alpha complete dim=1
            #pragma HLS ARRAY_PARTITION variable=tile_has_value complete dim=1

            for(int col=0; col<QUERY_BLOCK_COLS; ++col){
                #pragma HLS UNROLL
                acc_t tile_max = accMinimum();
                bool has_value = false;
                for(int row=0; row<PE_DIM; ++row){
                    if(score_valid[row][col]){
                        tile_max = has_value
                            ? detail::finiteAccMax(
                                tile_max, pe[row][col].score_acc
                            )
                            : pe[row][col].score_acc;
                        has_value = true;
                    }
                }
                tile_has_value[col] = has_value;
                next_max[col] = has_value
                    ? (history_valid[col]
                        ? detail::finiteAccMax(
                            running_max[col], tile_max
                        )
                        : tile_max)
                    : running_max[col];
            }

            acc_t acc_a[QUERY_BLOCK_COLS]{};
            acc_t acc_b[QUERY_BLOCK_COLS]{};
            acc_t acc_c[QUERY_BLOCK_COLS]{};
            acc_t acc_result[QUERY_BLOCK_COLS]{};
            #pragma HLS ARRAY_PARTITION variable=acc_a complete dim=1
            #pragma HLS ARRAY_PARTITION variable=acc_b complete dim=1
            #pragma HLS ARRAY_PARTITION variable=acc_c complete dim=1
            #pragma HLS ARRAY_PARTITION variable=acc_result complete dim=1

            for(int col=0; col<QUERY_BLOCK_COLS; ++col){
                #pragma HLS UNROLL
                acc_a[col] = detail::splitDAttentionScaleAcc();
                acc_b[col] = history_valid[col] && tile_has_value[col]
                    ? accSub(running_max[col], next_max[col])
                    : accZero();
            }
            detail::runAccumulatorColumns(
                false, acc_a, acc_b, acc_c, acc_result
            );
            detail::runAccumulatorColumns(
                true, acc_result, acc_b, acc_c, alpha
            );
            for(int col=0; col<QUERY_BLOCK_COLS; ++col){
                #pragma HLS UNROLL
                if(!history_valid[col]){
                    alpha[col] = accZero();
                }else if(!tile_has_value[col]){
                    alpha[col] = (acc_t)1.0F;
                }
            }

            // 完整S得到后才进入softmax。score只在这里转换一次。
            for(int row=0; row<PE_DIM; ++row){
                for(int col=0; col<QUERY_BLOCK_COLS; ++col){
                    #pragma HLS UNROLL
                    pe[row][col].reg = score_valid[row][col]
                        ? cvtAtoE(pe[row][col].score_acc)
                        : elemZero();
                }
            }

            // SUB_MAX
            detail::clearOperands(operand_b, operand_c);
            for(int row=0; row<PE_DIM; ++row){
                #pragma HLS UNROLL
                for(int col=0; col<QUERY_BLOCK_COLS; ++col){
                    #pragma HLS UNROLL
                    operand_b[row][col] = elemOne();
                    operand_c[row][col] = score_valid[row][col]
                        ? accSub(accZero(), next_max[col]) : accZero();
                }
            }
            detail::runPeArray(
                pe, operand_b, operand_c, false, pe_result
            );
            for(int row=0; row<PE_DIM; ++row){
                #pragma HLS UNROLL
                for(int col=0; col<QUERY_BLOCK_COLS; ++col){
                    #pragma HLS UNROLL
                    pe[row][col].reg = score_valid[row][col]
                        ? pe_result[row][col].out_elemType : elemZero();
                }
            }

            // SCALE
            detail::clearOperands(operand_b, operand_c);
            for(int row=0; row<PE_DIM; ++row){
                #pragma HLS UNROLL
                for(int col=0; col<QUERY_BLOCK_COLS; ++col){
                    #pragma HLS UNROLL
                    operand_b[row][col] =
                        detail::splitDAttentionScaleElem();
                }
            }
            detail::runPeArray(
                pe, operand_b, operand_c, false, pe_result
            );
            for(int row=0; row<PE_DIM; ++row){
                #pragma HLS UNROLL
                for(int col=0; col<QUERY_BLOCK_COLS; ++col){
                    #pragma HLS UNROLL
                    pe[row][col].reg = score_valid[row][col]
                        ? pe_result[row][col].out_elemType : elemZero();
                }
            }

            // 顺序扫描全部PWL分段。PE.reg在扫描期间保持原始X，命中
            // 结果暂存到已不再保存S的PE.score_acc，避免分段间依赖。
            bool probability_ready[PE_DIM][QUERY_BLOCK_COLS]{};
            #pragma HLS ARRAY_PARTITION variable=probability_ready complete dim=0
            for(int piece=0; piece<exp2PWLPieces; ++piece){
                #pragma HLS PIPELINE II=1
                detail::clearOperands(operand_b, operand_c);
                for(int row=0; row<PE_DIM; ++row){
                    #pragma HLS UNROLL
                    for(int col=0; col<QUERY_BLOCK_COLS; ++col){
                        #pragma HLS UNROLL
                        operand_b[row][col] =
                            detail::EXP2_SLOPES[piece];
                        operand_c[row][col] =
                            exp2PWLIntercept((exp2_counter_t)piece);
                    }
                }
                detail::runPeArray(
                    pe, operand_b, operand_c, true, pe_result
                );
                for(int row=0; row<PE_DIM; ++row){
                    #pragma HLS UNROLL
                    for(int col=0; col<QUERY_BLOCK_COLS; ++col){
                        #pragma HLS UNROLL
                        if(score_valid[row][col] &&
                                !probability_ready[row][col] &&
                                pe_result[row][col].out_exp2){
                            pe[row][col].score_acc = (acc_t)
                                pe_result[row][col].out_elemType;
                            probability_ready[row][col] = true;
                        }
                    }
                }
            }
            for(int row=0; row<PE_DIM; ++row){
                #pragma HLS UNROLL
                for(int col=0; col<QUERY_BLOCK_COLS; ++col){
                    #pragma HLS UNROLL
                    pe[row][col].reg = score_valid[row][col] &&
                            probability_ready[row][col]
                        ? (elem_t)pe[row][col].score_acc : elemZero();
                }
            }

            // 使用同一PE阵列逐行累加P，得到一次ROW_SUM。
            // row循环与operand准备已移入runPeRowSum，此处只调用一次；
            // 该函数与QK共用同一个runPeArray调用点，不复制阵列。
            acc_t row_sum[QUERY_BLOCK_COLS]{};
            #pragma HLS ARRAY_PARTITION variable=row_sum complete dim=1
            detail::runPeRowSum(pe, row_sum);
            detail::runAccumulatorColumns(
                false, alpha, running_sum, row_sum, acc_result
            );
            for(int col=0; col<QUERY_BLOCK_COLS; ++col){
                #pragma HLS UNROLL
                running_sum[col] = acc_result[col];
            }

            // PV每组保留8个独立feature上下文。单一扁平调度循环先
            // 交错发射D轮PE，再用同一循环的尾部操作更新output_acc。
            // 这避免工具为外层feature-group另建outline层级并复制PE。
            constexpr int pv_pe_operations =
                PE_DIM*detail::PV_INTERLEAVE;
            constexpr int pv_group_operations =
                pv_pe_operations+detail::PV_INTERLEAVE;
            constexpr int pv_groups =
                (HEAD_DIM+detail::PV_INTERLEAVE-1)/
                detail::PV_INTERLEAVE;
            acc_t pv_sum[detail::PV_INTERLEAVE][QUERY_BLOCK_COLS]{};
            #pragma HLS ARRAY_PARTITION variable=pv_sum complete dim=0
            int pv_group = 0;
            int pv_group_operation = 0;

            for(int operation=0;
                    operation<pv_groups*pv_group_operations;
                    ++operation){
                #pragma HLS PIPELINE II=1
                #pragma HLS DEPENDENCE variable=output_acc inter false
                #pragma HLS DEPENDENCE variable=pv_sum inter RAW distance=8 true
                const int feature_base =
                    pv_group*detail::PV_INTERLEAVE;
                const int remaining_features = HEAD_DIM-feature_base;
                const int active_contexts =
                    remaining_features<detail::PV_INTERLEAVE
                        ? remaining_features : detail::PV_INTERLEAVE;

                if(pv_group_operation<pv_pe_operations){
                    const int row =
                        pv_group_operation/detail::PV_INTERLEAVE;
                    const int context =
                        pv_group_operation%detail::PV_INTERLEAVE;
                    if(context<active_contexts){
                        const int feature = feature_base+context;
                        detail::clearOperands(operand_b, operand_c);
                        for(int r=0; r<PE_DIM; ++r){
                            #pragma HLS UNROLL
                            for(int col=0; col<QUERY_BLOCK_COLS; ++col){
                                #pragma HLS UNROLL
                                operand_b[r][col] = r==row
                                    ? v_tile[r][feature] : elemZero();
                                operand_c[r][col] = r==row
                                    ? pv_sum[context][col] : accZero();
                            }
                        }
                        detail::runPeArray(
                            pe, operand_b, operand_c, false, pe_result
                        );
                        for(int col=0; col<QUERY_BLOCK_COLS; ++col){
                            #pragma HLS UNROLL
                            pv_sum[context][col] =
                                pe_result[row][col].out_accType;
                        }
                    }
                }else{
                    const int context =
                        pv_group_operation-pv_pe_operations;
                    if(context<active_contexts){
                        const int feature = feature_base+context;
                        for(int col=0; col<QUERY_BLOCK_COLS; ++col){
                            #pragma HLS UNROLL
                            acc_a[col] = alpha[col];
                            acc_b[col] = output_acc[col][feature];
                            acc_c[col] = pv_sum[context][col];
                        }
                        detail::runAccumulatorColumns(
                            false, acc_a, acc_b, acc_c, acc_result
                        );
                        for(int col=0; col<QUERY_BLOCK_COLS; ++col){
                            #pragma HLS UNROLL
                            output_acc[col][feature] = acc_result[col];
                            pv_sum[context][col] = accZero();
                        }
                    }
                }
                ++pv_group_operation;
                if(pv_group_operation==pv_group_operations){
                    pv_group_operation = 0;
                    ++pv_group;
                }
            }

            for(int col=0; col<QUERY_BLOCK_COLS; ++col){
                #pragma HLS UNROLL
                if(tile_has_value[col]){
                    running_max[col] = next_max[col];
                    history_valid[col] = true;
                }
            }
        }

        acc_t inverse_sum[QUERY_BLOCK_COLS]{};
        acc_t normalized[QUERY_BLOCK_COLS][HEAD_DIM]{};
        #pragma HLS ARRAY_PARTITION variable=inverse_sum complete dim=1
        #pragma HLS ARRAY_PARTITION variable=normalized complete dim=1
        detail::reciprocalColumns(running_sum, inverse_sum);
        for(int feature=0; feature<HEAD_DIM; ++feature){
            acc_t acc_a[QUERY_BLOCK_COLS]{};
            acc_t acc_b[QUERY_BLOCK_COLS]{};
            acc_t acc_c[QUERY_BLOCK_COLS]{};
            acc_t acc_result[QUERY_BLOCK_COLS]{};
            #pragma HLS ARRAY_PARTITION variable=acc_a complete dim=1
            #pragma HLS ARRAY_PARTITION variable=acc_b complete dim=1
            #pragma HLS ARRAY_PARTITION variable=acc_c complete dim=1
            #pragma HLS ARRAY_PARTITION variable=acc_result complete dim=1
            for(int col=0; col<QUERY_BLOCK_COLS; ++col){
                #pragma HLS UNROLL
                acc_a[col] = inverse_sum[col];
                acc_b[col] = output_acc[col][feature];
            }
            detail::runAccumulatorColumns(
                false, acc_a, acc_b, acc_c, acc_result
            );
            for(int col=0; col<QUERY_BLOCK_COLS; ++col){
                #pragma HLS UNROLL
                normalized[col][feature] = acc_result[col];
            }
        }
        // 只有消耗完全部key tile后才能输出，禁止结果依赖阻断后续广播。
        for(int col=0; col<QUERY_BLOCK_COLS; ++col){
            for(int feature=0; feature<HEAD_DIM; ++feature){
                #pragma HLS PIPELINE II=1
                output.write(normalized[col][feature]);
            }
        }
    }

    void distributeQueryTile(const dma_word_t q_address[MAX_QKV_WORDS], const dma_word_t k_address[MAX_QKV_WORDS], const dma_word_t v_address[MAX_QKV_WORDS], const QueryTileHeader header, hls::stream<QueryTileHeader>& header0, hls::stream<QueryTileHeader>& header1, hls::stream<elem_t>& q0, hls::stream<elem_t>& q1, hls::stream<KeyValuePacket>& kv0, hls::stream<KeyValuePacket>& kv1){
        #pragma HLS INLINE off
        header0.write(header);
        if(QUERY_BLOCKS==2){
            header1.write(header);
        }
        elem_t q_tile[PE_DIM][HEAD_DIM]{};
        #pragma HLS ARRAY_PARTITION variable=q_tile complete dim=1
        loadElemTile(q_address, header.query_base, header.active_queries, q_tile);
        for(int col=0; col<PE_DIM; ++col){
            for(int feature=0; feature<HEAD_DIM; ++feature){
                #pragma HLS PIPELINE II=1
                if(col < QUERY_BLOCK_COLS){
                    q0.write(q_tile[col][feature]);
                }else{
                    q1.write(q_tile[col][feature]);
                }
            }
        }
        for(unsigned key_tile=0; key_tile<header.key_tiles; ++key_tile){
            #pragma HLS LOOP_TRIPCOUNT min=1 max=MAX_SEQUENCE_TILES
            const unsigned key_base = key_tile*(unsigned)PE_DIM;
            const unsigned remaining = header.length-key_base;
            const unsigned active_keys = remaining<(unsigned)PE_DIM ? remaining : (unsigned)PE_DIM;
            elem_t k_tile[PE_DIM][HEAD_DIM]{};
            elem_t v_tile[PE_DIM][HEAD_DIM]{};
            #pragma HLS ARRAY_PARTITION variable=k_tile complete dim=1
            #pragma HLS ARRAY_PARTITION variable=v_tile complete dim=1
            loadElemTile(k_address, key_base, active_keys, k_tile);
            loadValueTile(v_address, key_base, active_keys, v_tile);
            for(int row=0; row<PE_DIM; ++row){
                for(int feature=0; feature<HEAD_DIM; ++feature){
                    #pragma HLS PIPELINE II=1
                    KeyValuePacket packet;
                    packet.key = k_tile[row][feature];
                    packet.value = v_tile[row][feature];
                    kv0.write(packet);
                    if(QUERY_BLOCKS==2){
                        kv1.write(packet);
                    }
                }
            }
        }
    }

    void gatherQueryTile(dma_word_t o_address[MAX_O_WORDS], const unsigned query_base, const unsigned active_queries, hls::stream<acc_t>& output0, hls::stream<acc_t>& output1){
        #pragma HLS INLINE off
        acc_t normalized[PE_DIM][HEAD_DIM]{};
        #pragma HLS ARRAY_PARTITION variable=normalized complete dim=1
        for(int col=0; col<PE_DIM; ++col){
            for(int feature=0; feature<HEAD_DIM; ++feature){
                #pragma HLS PIPELINE II=1
                normalized[col][feature] = col<QUERY_BLOCK_COLS ? output0.read() : output1.read();
            }
        }
        storeOutputTile(o_address, query_base, active_queries, normalized);
    }

    void runQueryTile(const dma_word_t q_address[MAX_QKV_WORDS], const dma_word_t k_address[MAX_QKV_WORDS], const dma_word_t v_address[MAX_QKV_WORDS], dma_word_t o_address[MAX_O_WORDS], const QueryTileHeader header){
        #pragma HLS INLINE off
        #pragma HLS DATAFLOW
        hls::stream<QueryTileHeader> header0("header0"), header1("header1");
        hls::stream<elem_t> q0("q0"), q1("q1");
        hls::stream<KeyValuePacket> kv0("kv0"), kv1("kv1");
        hls::stream<acc_t> output0("output0"), output1("output1");
        #pragma HLS STREAM variable=header0 depth=2
        #pragma HLS STREAM variable=header1 depth=2
        #pragma HLS STREAM variable=q0 depth=8
        #pragma HLS STREAM variable=q1 depth=8
        #pragma HLS STREAM variable=kv0 depth=8
        #pragma HLS STREAM variable=kv1 depth=8
        #pragma HLS STREAM variable=output0 depth=8
        #pragma HLS STREAM variable=output1 depth=8
        distributeQueryTile(q_address, k_address, v_address, header, header0, header1, q0, q1, kv0, kv1);
        runQueryBlock<0>(header0, q0, kv0, output0);
        if(QUERY_BLOCKS==2){
            runQueryBlock<1>(header1, q1, kv1, output1);
        }
        gatherQueryTile(o_address, header.query_base, header.active_queries, output0, output1);
    }

}  // namespace detail

    void runController(const dma_word_t q_address[MAX_QKV_WORDS], const dma_word_t k_address[MAX_QKV_WORDS], const dma_word_t v_address[MAX_QKV_WORDS], dma_word_t o_address[MAX_O_WORDS], const unsigned length, const bool causal){
        #pragma HLS INLINE off
        const unsigned query_tiles = (length+(unsigned)PE_DIM-1U)/(unsigned)PE_DIM;
        for(unsigned query_tile=0; query_tile<query_tiles; ++query_tile){
            #pragma HLS LOOP_TRIPCOUNT min=1 max=MAX_SEQUENCE_TILES
            QueryTileHeader header;
            header.query_base = query_tile*(unsigned)PE_DIM;
            const unsigned remaining = length-header.query_base;
            header.active_queries = remaining<(unsigned)PE_DIM ? remaining : (unsigned)PE_DIM;
            header.key_tiles = causal ? query_tile+1U : query_tiles;
            header.length = length;
            header.causal = causal;
            detail::runQueryTile(q_address, k_address, v_address, o_address, header);
        }
    }

    void run(
        const dma_word_t q_address[MAX_QKV_WORDS],
        const dma_word_t k_address[MAX_QKV_WORDS],
        const dma_word_t v_address[MAX_QKV_WORDS],
        dma_word_t o_address[MAX_O_WORDS],
        const unsigned length,
        const bool causal
    ){
        #pragma HLS INLINE off

        runController(
            q_address, k_address, v_address, o_address,
            length, causal
        );
    }

}  // namespace split_d
}  // namespace fsa
