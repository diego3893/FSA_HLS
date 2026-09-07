#include "fsa/stream/common.hpp"

#include <utils/x_hls_utils.h>

namespace fsa{
namespace streaming_v2_detail{

    /**
     * @brief 一列Accumulator的物理FP32 MAC流水线
     *
     * 模板列号保证综合后得到SA_COLS条并行lane。每条lane在不同L/O行
     * 之间时分复用，QK和PV仍只使用上方同一套SA。固定九拍只增加
     * fill/drain，不降低连续L/O token的发射率。
     */
    template<int COL>
    acc_t accumulatorMacLane(
        const acc_t scale,
        const acc_t old_value,
        const acc_t contribution
    ){
        static_assert(COL>=0 && COL<SA_COLS,
                      "Accumulator col out of range");
        #pragma HLS INLINE off
        #pragma HLS PIPELINE II=1
        #pragma HLS LATENCY min=9 max=9
        return accUnit(scale, old_value, contribution);
    }

    template<int COL>
    struct AccumulatorMacColumns{
        static void run(
            const acc_t scale[SA_COLS],
            const acc_t old_value[SA_COLS],
            const acc_t contribution[SA_COLS],
            acc_t result[SA_COLS]
        ){
            #pragma HLS INLINE
            result[COL] = accumulatorMacLane<COL>(
                scale[COL], old_value[COL], contribution[COL]
            );
            AccumulatorMacColumns<COL+1>::run(
                scale, old_value, contribution, result
            );
        }
    };

    template<>
    struct AccumulatorMacColumns<SA_COLS>{
        static void run(
            const acc_t[SA_COLS],
            const acc_t[SA_COLS],
            const acc_t[SA_COLS],
            acc_t[SA_COLS]
        ){
            #pragma HLS INLINE
        }
    };

    /** @brief AccRAM整行同步读边界。 */
    void accumulatorSramReadRow(
        const acc_t accumulator_sram[ACC_ROWS][SA_COLS],
        const int address,
        acc_t data[SA_COLS]
    ){
        #pragma HLS INLINE off
        #pragma HLS PIPELINE II=1
        #pragma HLS LATENCY min=1 max=1
        for(int col=0; col<SA_COLS; ++col){
            #pragma HLS UNROLL
            data[col] = accumulator_sram[address][col];
        }
    }

    /** @brief AccRAM整行同步写边界。 */
    void accumulatorSramWriteRow(
        acc_t accumulator_sram[ACC_ROWS][SA_COLS],
        const int address,
        const acc_t data[SA_COLS]
    ){
        #pragma HLS INLINE off
        #pragma HLS PIPELINE II=1
        #pragma HLS LATENCY min=1 max=1
        for(int col=0; col<SA_COLS; ++col){
            #pragma HLS UNROLL
            accumulator_sram[address][col] = data[col];
        }
    }

    /**
     * @brief FSA Accumulator算术与显式AccRAM端口。
     *
     * RAM仍由本DATAFLOW进程独占，避免形成C仿真和RTL都可能死锁的
     * 双向进程环；所有访问必须经过两个非内联的一拍行端口，算术逻辑
     * 不再直接索引L/O数组。
     */
    void accumulatorProcess(
        const unsigned length,
        SaResultStream& sa_result_stream,
        AccRowStream& output_stream
    ){
        #pragma HLS INLINE off

        acc_t accumulator_sram[ACC_ROWS][SA_COLS]{};
        #pragma HLS BIND_STORAGE \
            variable=accumulator_sram type=ram_t2p impl=bram
        #pragma HLS ARRAY_PARTITION \
            variable=accumulator_sram type=complete dim=2

        const unsigned tiles = tileCount(length);
        for(unsigned query_tile=0; query_tile<tiles; ++query_tile){
            #pragma HLS LOOP_TRIPCOUNT min=1 max=DMA_MAX_SEQUENCE_TILES
            for(unsigned key_tile=0; key_tile<tiles; ++key_tile){
                #pragma HLS LOOP_TRIPCOUNT min=1 max=DMA_MAX_SEQUENCE_TILES
                const SaResultToken max_token = sa_result_stream.read();
                acc_t alpha[SA_COLS]{};
                #pragma HLS ARRAY_PARTITION variable=alpha type=complete dim=1

                // 与FSA一致：oldMax-newMax的exp2属于Accumulator，而非SA。
                for(int query=0; query<SA_COLS; ++query){
                    #pragma HLS UNROLL
                    alpha[query] = max_token.initialize
                        ? accZero()
                        : accExp2PWL(
                            max_token.data[query]*attentionScale()
                        );
                }

                // rowsum后紧跟SA_ROWS个PV token。协议固定映射为
                // event=0更新L，event=1..SA_ROWS更新对应O行。
                for(int event=0; event<SA_ROWS+1; ++event){
                    #pragma HLS PIPELINE II=1
                    const SaResultToken value_token =
                        sa_result_stream.read();
                    acc_t old_value[SA_COLS]{};
                    acc_t contribution[SA_COLS]{};
                    acc_t updated_value[SA_COLS]{};
                    #pragma HLS ARRAY_PARTITION \
                        variable=old_value complete dim=1
                    #pragma HLS ARRAY_PARTITION \
                        variable=contribution complete dim=1
                    #pragma HLS ARRAY_PARTITION \
                        variable=updated_value complete dim=1
                    accumulatorSramReadRow(
                        accumulator_sram, event, old_value
                    );
                    for(int query=0; query<SA_COLS; ++query){
                        #pragma HLS UNROLL
                        old_value[query] = max_token.initialize
                            ? accZero()
                            : old_value[query];
                        contribution[query] = value_token.data[query];
                    }
                    AccumulatorMacColumns<0>::run(
                        alpha, old_value, contribution, updated_value
                    );
                    accumulatorSramWriteRow(
                        accumulator_sram, event, updated_value
                    );
                }

                if(max_token.finalize){
                    acc_t final_rows[ACC_ROWS][SA_COLS]{};
                    #pragma HLS ARRAY_PARTITION \
                        variable=final_rows complete dim=2
                    for(int row=0; row<ACC_ROWS; ++row){
                        #pragma HLS PIPELINE II=1
                        accumulatorSramReadRow(
                            accumulator_sram, row, final_rows[row]
                        );
                    }

                    acc_t inverse_l[SA_COLS]{};
                    #pragma HLS ARRAY_PARTITION \
                        variable=inverse_l type=complete dim=1
                    for(int query=0; query<SA_COLS; ++query){
                        #pragma HLS UNROLL
                        inverse_l[query] = final_rows[0][query]!=accZero()
                            ? accumulator_reciprocal(
                                final_rows[0][query]
                            )
                            : accZero();
                    }

                    for(int query=0;
                            query<max_token.active_queries.to_int(); ++query){
                        #pragma HLS PIPELINE II=1
                        AccRowPacket packet{};
                        for(int feature=0; feature<SA_ROWS; ++feature){
                            #pragma HLS UNROLL
                            packet.data[feature] =
                                final_rows[feature+1][query]*
                                inverse_l[query];
                        }
                        output_stream.write(packet);
                    }
                }
            }
        }
    }

}  // namespace streaming_v2_detail
}  // namespace fsa
