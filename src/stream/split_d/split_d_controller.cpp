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
    void runQueryBlock(hls::stream<QueryTileHeader>& headers, hls::stream<dma_word_t>& q_input, hls::stream<KeyValuePacket>& kv_input, hls::stream<acc_t>& output){
        #pragma HLS INLINE off
        #pragma HLS ALLOCATION function instances=detail::runSharedEngine limit=1
        const QueryTileHeader header = headers.read();
        const unsigned offset = (unsigned)(BLOCK_ID*QUERY_BLOCK_COLS);
        SharedEngineState state{};
        #pragma HLS ARRAY_PARTITION variable=state.q complete dim=2
        #pragma HLS ARRAY_PARTITION variable=state.q cyclic factor=DMA_ELEMS_PER_WORD dim=3
        #pragma HLS ARRAY_PARTITION variable=state.k complete dim=1
        #pragma HLS ARRAY_PARTITION variable=state.v complete dim=1
        #pragma HLS ARRAY_PARTITION variable=state.k cyclic factor=DMA_ELEMS_PER_WORD dim=2
        #pragma HLS ARRAY_PARTITION variable=state.v cyclic factor=DMA_ELEMS_PER_WORD dim=2
        #pragma HLS ARRAY_PARTITION variable=state.reg complete dim=2
        #pragma HLS ARRAY_PARTITION variable=state.reg complete dim=3
        #pragma HLS ARRAY_PARTITION variable=state.score complete dim=2
        #pragma HLS ARRAY_PARTITION variable=state.score complete dim=3
        #pragma HLS ARRAY_PARTITION variable=state.output_acc complete dim=2
        for(unsigned ctx=0; ctx<header.context_count; ++ctx){
            for(int col=0; col<QUERY_BLOCK_COLS; ++col){
                state.running_max[ctx][col] = accMinimum();
                for(int word=0; word<QKV_WORDS_PER_TOKEN; ++word){
                    #pragma HLS PIPELINE II=1
                    const dma_word_t packed = q_input.read();
                    for(int lane=0; lane<DMA_ELEMS_PER_WORD; ++lane){
                        #pragma HLS UNROLL
                        state.q[ctx][col][word*DMA_ELEMS_PER_WORD+lane] = dma_unpack_elem(packed, lane);
                    }
                }
            }
        }
        for(unsigned key_tile=0; key_tile<header.key_tiles; ++key_tile){
            #pragma HLS LOOP_TRIPCOUNT min=1 max=MAX_SEQUENCE_TILES
            const unsigned key_base = key_tile*(unsigned)PE_DIM;
            const unsigned remaining_keys = header.length-key_base;
            const unsigned active_keys = remaining_keys<(unsigned)PE_DIM ? remaining_keys : (unsigned)PE_DIM;
            for(int row=0; row<PE_DIM; ++row){
                for(int word=0; word<QKV_WORDS_PER_TOKEN; ++word){
                    #pragma HLS PIPELINE II=1
                    const KeyValuePacket packet = kv_input.read();
                    for(int lane=0; lane<DMA_ELEMS_PER_WORD; ++lane){
                        #pragma HLS UNROLL
                        const int feature = word*DMA_ELEMS_PER_WORD+lane;
                        state.k[row][feature] = dma_unpack_elem(packet.key, lane);
                        state.v[row][feature] = dma_unpack_elem(packet.value, lane);
                    }
                }
            }
            // causal只处理该key tile确实存在的query tile，禁止未来tile零更新。
            const unsigned base_tile = header.query_base/(unsigned)PE_DIM;
            const unsigned first_context = header.causal && key_tile>base_tile ? key_tile-base_tile : 0U;
            const unsigned active_contexts = header.context_count-first_context;
            for(unsigned ctx=first_context; ctx<header.context_count; ++ctx){
                const unsigned query_base = header.query_base+ctx*(unsigned)PE_DIM;
                const unsigned remaining = header.length-query_base;
                const unsigned active_tile = remaining<(unsigned)PE_DIM ? remaining : (unsigned)PE_DIM;
                const unsigned active_queries = active_tile>offset
                    ? ((active_tile-offset)<(unsigned)QUERY_BLOCK_COLS ? active_tile-offset : (unsigned)QUERY_BLOCK_COLS) : 0U;
                for(int row=0; row<PE_DIM; ++row){
                    #pragma HLS UNROLL
                    for(int col=0; col<QUERY_BLOCK_COLS; ++col){
                        #pragma HLS UNROLL
                        const unsigned global_query = query_base+offset+(unsigned)col;
                        const unsigned global_key = key_base+(unsigned)row;
                        state.score[ctx][row][col] = accZero();
                        state.score_valid[ctx][row][col] = (unsigned)col<active_queries && (unsigned)row<active_keys && (!header.causal || global_key<=global_query);
                        state.probability_ready[ctx][row][col] = false;
                    }
                }
            }
            runSharedEngine(SharedOperation::QK, first_context, active_contexts, state);
            for(unsigned ctx=first_context; ctx<header.context_count; ++ctx){
                for(int col=0; col<QUERY_BLOCK_COLS; ++col){
                    #pragma HLS UNROLL
                    acc_t tile_max = accMinimum();
                    bool has_value = false;
                    for(int row=0; row<PE_DIM; ++row){
                        if(state.score_valid[ctx][row][col]){
                            tile_max = has_value ? finiteAccMax(tile_max, state.score[ctx][row][col]) : state.score[ctx][row][col];
                            has_value = true;
                        }
                    }
                    state.tile_has_value[ctx][col] = has_value;
                    const acc_t previous_max = state.running_max[ctx][col];
                    const acc_t next_max = has_value
                        ? (state.history_valid[ctx][col] ? finiteAccMax(previous_max, tile_max) : tile_max)
                        : previous_max;
                    state.next_max[ctx][col] = next_max;
                    state.alpha_delta[ctx][col] = state.history_valid[ctx][col] && has_value
                        ? accSub(previous_max, next_max) : accZero();
                }
            }
            runSharedEngine(SharedOperation::ALPHA_SCALE, first_context, active_contexts, state);
            runSharedEngine(SharedOperation::ALPHA_PWL, first_context, active_contexts, state);
            for(unsigned ctx=first_context; ctx<header.context_count; ++ctx){
                for(int col=0; col<QUERY_BLOCK_COLS; ++col){
                    #pragma HLS UNROLL
                    if(!state.history_valid[ctx][col]){
                        state.alpha[ctx][col] = accZero();
                    }else if(!state.tile_has_value[ctx][col]){
                        state.alpha[ctx][col] = (acc_t)1.0F;
                    }
                }
                // 完整S得到后仅在此处执行一次FP32到FP16量化。
                for(int row=0; row<PE_DIM; ++row){
                    for(int col=0; col<QUERY_BLOCK_COLS; ++col){
                        #pragma HLS UNROLL
                        state.reg[ctx][row][col] = state.score_valid[ctx][row][col] ? cvtAtoE(state.score[ctx][row][col]) : elemZero();
                    }
                }
            }
            runSharedEngine(SharedOperation::SUB_MAX, first_context, active_contexts, state);
            runSharedEngine(SharedOperation::SCALE, first_context, active_contexts, state);
            runSharedEngine(SharedOperation::PWL, first_context, active_contexts, state);
            for(unsigned ctx=first_context; ctx<header.context_count; ++ctx){
                for(int row=0; row<PE_DIM; ++row){
                    #pragma HLS UNROLL
                    for(int col=0; col<QUERY_BLOCK_COLS; ++col){
                        #pragma HLS UNROLL
                        state.reg[ctx][row][col] = state.score_valid[ctx][row][col] && state.probability_ready[ctx][row][col]
                            ? viewAasE(state.score[ctx][row][col]) : elemZero();
                    }
                }
            }
            // P完整、ROW_SUM/L完整两道屏障都保留，PV随后使用稳定P。
            runSharedEngine(SharedOperation::ROW_SUM, first_context, active_contexts, state);
            runSharedEngine(SharedOperation::PV, first_context, active_contexts, state);
            for(unsigned ctx=first_context; ctx<header.context_count; ++ctx){
                for(int col=0; col<QUERY_BLOCK_COLS; ++col){
                    #pragma HLS UNROLL
                    if(state.tile_has_value[ctx][col]){
                        state.running_max[ctx][col] = state.next_max[ctx][col];
                        state.history_valid[ctx][col] = true;
                    }
                }
            }
        }
        for(unsigned ctx=0; ctx<header.context_count; ++ctx){
            reciprocalColumns(state.running_sum[ctx], state.inverse_sum[ctx]);
        }
        runSharedEngine(SharedOperation::NORMALIZE, 0, header.context_count, state);
        for(unsigned ctx=0; ctx<header.context_count; ++ctx){
            for(int col=0; col<QUERY_BLOCK_COLS; ++col){
                for(int feature=0; feature<HEAD_DIM; ++feature){
                    #pragma HLS PIPELINE II=1
                    output.write(state.output_acc[ctx][col][feature]);
                }
            }
        }
    }

    void distributeQueryTile(const dma_word_t q_address[MAX_QKV_WORDS], const dma_word_t k_address[MAX_QKV_WORDS], const dma_word_t v_address[MAX_QKV_WORDS], const QueryTileHeader header, hls::stream<QueryTileHeader>& header0, hls::stream<QueryTileHeader>& header1, hls::stream<dma_word_t>& q0, hls::stream<dma_word_t>& q1, hls::stream<KeyValuePacket>& kv0, hls::stream<KeyValuePacket>& kv1){
        #pragma HLS INLINE off
        header0.write(header);
        if(QUERY_BLOCKS==2){
            header1.write(header);
        }
        // 每个外存字只读一次；保留原始位模式，到worker内才解包。
        for(unsigned ctx=0; ctx<header.context_count; ++ctx){
            for(int col=0; col<PE_DIM; ++col){
                for(int word=0; word<QKV_WORDS_PER_TOKEN; ++word){
                    #pragma HLS PIPELINE II=1
                    const unsigned address = (header.query_base+ctx*(unsigned)PE_DIM+(unsigned)col)*(unsigned)QKV_WORDS_PER_TOKEN+(unsigned)word;
                    const dma_word_t packed = ctx*(unsigned)PE_DIM+(unsigned)col<header.active_queries ? q_address[address] : dma_word_t(0);
                    if(col < QUERY_BLOCK_COLS){
                        q0.write(packed);
                    }else{
                        q1.write(packed);
                    }
                }
            }
        }
        for(unsigned key_tile=0; key_tile<header.key_tiles; ++key_tile){
            #pragma HLS LOOP_TRIPCOUNT min=1 max=MAX_SEQUENCE_TILES
            const unsigned key_base = key_tile*(unsigned)PE_DIM;
            const unsigned remaining = header.length-key_base;
            const unsigned active_keys = remaining<(unsigned)PE_DIM ? remaining : (unsigned)PE_DIM;
            for(int row=0; row<PE_DIM; ++row){
                for(int word=0; word<QKV_WORDS_PER_TOKEN; ++word){
                    #pragma HLS PIPELINE II=1
                    const unsigned address = (key_base+(unsigned)row)*(unsigned)QKV_WORDS_PER_TOKEN+(unsigned)word;
                    KeyValuePacket packet;
                    packet.key = (unsigned)row<active_keys ? k_address[address] : dma_word_t(0);
                    packet.value = (unsigned)row<active_keys ? v_address[address] : dma_word_t(0);
                    kv0.write(packet);
                    if(QUERY_BLOCKS==2){
                        kv1.write(packet);
                    }
                }
            }
        }
    }

    void gatherQueryTile(dma_word_t o_address[MAX_O_WORDS], const unsigned query_base, const unsigned active_queries, const unsigned context_count, hls::stream<acc_t>& output0, hls::stream<acc_t>& output1){
        #pragma HLS INLINE off
        acc_t normalized[PE_DIM][HEAD_DIM]{};
        #pragma HLS ARRAY_PARTITION variable=normalized complete dim=1
        for(unsigned ctx=0; ctx<context_count; ++ctx){
            for(int col=0; col<PE_DIM; ++col){
                for(int feature=0; feature<HEAD_DIM; ++feature){
                    #pragma HLS PIPELINE II=1
                    normalized[col][feature] = col<QUERY_BLOCK_COLS ? output0.read() : output1.read();
                }
            }
            const unsigned consumed = ctx*(unsigned)PE_DIM;
            const unsigned remaining = active_queries-consumed;
            const unsigned active_tile = remaining<(unsigned)PE_DIM ? remaining : (unsigned)PE_DIM;
            storeOutputTile(o_address, query_base+consumed, active_tile, normalized);
        }
    }

    void runQueryTile(const dma_word_t q_address[MAX_QKV_WORDS], const dma_word_t k_address[MAX_QKV_WORDS], const dma_word_t v_address[MAX_QKV_WORDS], dma_word_t o_address[MAX_O_WORDS], const QueryTileHeader header){
        #pragma HLS INLINE off
        #pragma HLS DATAFLOW
        hls::stream<QueryTileHeader> header0("header0"), header1("header1");
        hls::stream<dma_word_t> q0("q0"), q1("q1");
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
#if FSA_SPLIT_D_QUERY_BLOCK_COLS<FSA_SPLIT_D_PE_DIM
        runQueryBlock<1>(header1, q1, kv1, output1);
#endif
        gatherQueryTile(o_address, header.query_base, header.active_queries, header.context_count, output0, output1);
    }

}  // namespace detail

    void runController(const dma_word_t q_address[MAX_QKV_WORDS], const dma_word_t k_address[MAX_QKV_WORDS], const dma_word_t v_address[MAX_QKV_WORDS], dma_word_t o_address[MAX_O_WORDS], const unsigned length, const bool causal){
        #pragma HLS INLINE off
        const unsigned query_tiles = (length+(unsigned)PE_DIM-1U)/(unsigned)PE_DIM;
        // 最少batch数不变，小batch前置：causal模式减少重复KV前缀装载，
        // 所有query仍按原顺序输出；每个query内部的key/feature顺序不变。
        const unsigned remainder = query_tiles%(unsigned)QUERY_CONTEXTS;
        const unsigned first_batch = remainder==0U ? (unsigned)QUERY_CONTEXTS : remainder;
        for(unsigned query_tile=0; query_tile<query_tiles;){
            #pragma HLS LOOP_TRIPCOUNT min=1 max=MAX_SEQUENCE_TILES
            QueryTileHeader header;
            header.query_base = query_tile*(unsigned)PE_DIM;
            header.context_count = query_tile==0U ? first_batch : (unsigned)QUERY_CONTEXTS;
            const unsigned remaining = length-header.query_base;
            header.active_queries = remaining<header.context_count*(unsigned)PE_DIM ? remaining : header.context_count*(unsigned)PE_DIM;
            header.key_tiles = causal ? query_tile+header.context_count : query_tiles;
            header.length = length;
            header.causal = causal;
            detail::runQueryTile(q_address, k_address, v_address, o_address, header);
            query_tile += header.context_count;
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
