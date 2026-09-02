#include "fsa/stream_accumulator.hpp"

#include "fsa/accumulator.hpp"
#include "fsa/arithmetic.hpp"

namespace fsa{
    namespace stream_accumulator_detail{

        void stream_accumulator_lane(
            const int query,
            const bool initialize,
            const bool finalize,
            const acc_t max_difference,
            const acc_t rowsum,
            const acc_t pv[SA_ROWS],
            acc_t& online_l,
            acc_t online_o[SA_ROWS],
            acc_t& output_l,
            acc_t output_o[SA_ROWS]
        ){
            #pragma HLS INLINE off
            #pragma HLS FUNCTION_INSTANTIATE variable=query

            const acc_t old_l = initialize ? accZero() : online_l;
            const acc_t alpha = old_l==accZero()
                ? accZero()
                : accExp2PWL(max_difference*attentionScale());
            const acc_t next_l = accUnit(alpha, old_l, rowsum);
            const acc_t inverse_l = finalize && next_l!=accZero()
                ? accumulator_reciprocal(next_l) : accZero();

            online_l = next_l;
            output_l = next_l;
            for(int feature=0; feature<SA_ROWS; ++feature){
                #pragma HLS PIPELINE II=1
                const acc_t old_o = initialize
                    ? accZero() : online_o[feature];
                const acc_t next_o = accUnit(
                    alpha, old_o, pv[feature]
                );
                online_o[feature] = next_o;
                output_o[feature] = finalize
                    ? (next_l==accZero()
                        ? accZero() : next_o*inverse_l)
                    : next_o;
            }
        }

    }  // namespace stream_accumulator_detail

    void stream_accumulator_update(
        const bool initialize,
        const bool finalize,
        const acc_t max_difference[SA_COLS],
        const acc_t rowsum[SA_COLS],
        const acc_t pv[SA_COLS][SA_ROWS],
        acc_t online_l[SA_COLS],
        acc_t online_o[SA_COLS][SA_ROWS],
        FsaCoreRequestOutput& output
    ){
        #pragma HLS INLINE off
        #pragma HLS ARRAY_PARTITION variable=max_difference type=complete dim=1
        #pragma HLS ARRAY_PARTITION variable=rowsum type=complete dim=1
        #pragma HLS ARRAY_PARTITION variable=pv type=complete dim=1
        #pragma HLS ARRAY_PARTITION variable=online_l type=complete dim=1
        #pragma HLS ARRAY_PARTITION variable=online_o type=complete dim=1

        for(int query=0; query<SA_COLS; ++query){
            #pragma HLS UNROLL
            stream_accumulator_detail::stream_accumulator_lane(
                query, initialize, finalize,
                max_difference[query], rowsum[query], pv[query],
                online_l[query], online_o[query],
                output.l[query], output.o[query]
            );
        }
        output.normalized = finalize;
    }

}  // namespace fsa
