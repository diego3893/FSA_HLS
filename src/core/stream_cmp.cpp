#include "fsa/stream_cmp.hpp"

#include "fsa/arithmetic.hpp"

namespace fsa{
    namespace stream_cmp_detail{

        bool cmp_lane_enabled(
            const int key,
            const int query,
            const std::uint16_t active_keys,
            const bool causal,
            const std::uint32_t query_base,
            const std::uint32_t key_base
        ){
            #pragma HLS INLINE
            if(key>=SA_COLS || key>=(int)active_keys){
                return false;
            }
            return !causal || key_base+(unsigned)key<=
                query_base+(unsigned)query;
        }

        void stream_cmp_lane(
            const int query,
            const acc_t scores[SA_COLS][SA_ROWS],
            const std::uint16_t active_keys,
            const bool causal,
            const std::uint32_t query_base,
            const std::uint32_t key_base,
            const acc_t old_max,
            elem_t score_resident[SA_ROWS][SA_COLS],
            acc_t& new_max,
            acc_t& max_difference
        ){
            #pragma HLS INLINE off
            #pragma HLS FUNCTION_INSTANTIATE variable=query

            acc_t maximum = old_max;
            for(int key=0; key<SA_ROWS; ++key){
                #pragma HLS PIPELINE II=1
                const bool enabled = cmp_lane_enabled(
                    key, query, active_keys, causal,
                    query_base, key_base
                );
                if(enabled){
                    const acc_t score = scores[query][key];
                    maximum = accMax(maximum, score);
                    score_resident[key][query] = cvtAtoE(score);
                }else{
                    score_resident[key][query] = elemZero();
                }
            }
            new_max = maximum;
            max_difference = accDiff(old_max, maximum);
        }

    }  // namespace stream_cmp_detail

    void stream_cmp_update(
        const acc_t scores[SA_COLS][SA_ROWS],
        const std::uint16_t active_keys,
        const bool causal,
        const std::uint32_t query_base,
        const std::uint32_t key_base,
        const bool initialize,
        const acc_t old_max[SA_COLS],
        elem_t score_resident[SA_ROWS][SA_COLS],
        acc_t new_max[SA_COLS],
        acc_t max_difference[SA_COLS]
    ){
        #pragma HLS INLINE off
        #pragma HLS ARRAY_PARTITION variable=scores type=complete dim=1
        #pragma HLS ARRAY_PARTITION variable=score_resident type=complete dim=0
        #pragma HLS ARRAY_PARTITION variable=old_max type=complete dim=1
        #pragma HLS ARRAY_PARTITION variable=new_max type=complete dim=1
        #pragma HLS ARRAY_PARTITION variable=max_difference type=complete dim=1

        for(int query=0; query<SA_COLS; ++query){
            #pragma HLS UNROLL
            const acc_t previous = initialize
                ? accMinimum() : old_max[query];
            stream_cmp_detail::stream_cmp_lane(
                query, scores, active_keys, causal,
                query_base, key_base, previous,
                score_resident, new_max[query], max_difference[query]
            );
        }
    }

    void stream_cmp_request(
        const FsaCoreRequestInput& input,
        const acc_t scores[SA_COLS][SA_ROWS],
        elem_t score_feedback[SA_COLS][SA_ROWS],
        acc_t new_max[SA_COLS],
        acc_t max_difference[SA_COLS]
    ){
        #pragma HLS INLINE off
        #pragma HLS ARRAY_PARTITION variable=scores type=complete dim=1
        #pragma HLS ARRAY_PARTITION variable=score_feedback type=complete dim=1
        #pragma HLS ARRAY_PARTITION variable=new_max type=complete dim=1
        #pragma HLS ARRAY_PARTITION variable=max_difference type=complete dim=1

        static acc_t old_max[SA_COLS]{};
        #pragma HLS RESET variable=old_max
        #pragma HLS ARRAY_PARTITION variable=old_max type=complete dim=1

        elem_t feedback_by_row[SA_ROWS][SA_COLS]{};
        #pragma HLS ARRAY_PARTITION variable=feedback_by_row type=complete dim=0

        stream_cmp_update(
            scores, input.active_keys, input.causal,
            input.query_base, input.key_base, input.initialize,
            old_max, feedback_by_row, new_max, max_difference
        );

        for(int query=0; query<SA_COLS; ++query){
            #pragma HLS UNROLL
            old_max[query] = new_max[query];
            for(int key=0; key<SA_ROWS; ++key){
                #pragma HLS UNROLL
                score_feedback[query][key] =
                    feedback_by_row[key][query];
            }
        }
    }

}  // namespace fsa
