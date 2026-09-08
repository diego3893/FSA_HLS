/**
 * @file banked_sram.hpp
 * @brief 与FSA-main BankedSRAM端口语义一致的静态bank/sub-bank存储。
 */
#ifndef FSA_STREAM_BANKED_SRAM_HPP
#define FSA_STREAM_BANKED_SRAM_HPP

namespace fsa{
namespace streaming_v2_detail{

    template<typename T, int LOGICAL_ROWS, int ROW_SIZE,
             int BANKS, int SUB_BANKS>
    struct BankedSramStorage{
        static_assert(LOGICAL_ROWS>0, "SRAM logical rows must be positive");
        static_assert(ROW_SIZE%SUB_BANKS==0,
                      "SRAM row must split evenly into sub-banks");
        static_assert((BANKS&(BANKS-1))==0,
                      "SRAM bank count must be a power of two");

        static constexpr int SUB_BANK_SIZE = ROW_SIZE/SUB_BANKS;
        static constexpr int BANK_COUNT = BANKS;
        static constexpr int SUB_BANK_COUNT = SUB_BANKS;
        static constexpr int BANK_DEPTH =
            (LOGICAL_ROWS+BANKS-1)/BANKS;

        T data[BANKS][SUB_BANKS][BANK_DEPTH][SUB_BANK_SIZE];
    };

    template<typename Storage, typename T, int ROW_SIZE>
    void bankedSramFullRead(
        const Storage& storage,
        const unsigned address,
        T output[ROW_SIZE]
    ){
        #pragma HLS INLINE
        const unsigned bank = address&(Storage::BANK_COUNT-1U);
        const unsigned row = address/Storage::BANK_COUNT;
        for(int sub_bank=0; sub_bank<Storage::SUB_BANK_COUNT; ++sub_bank){
            #pragma HLS UNROLL
            for(int lane=0; lane<Storage::SUB_BANK_SIZE; ++lane){
                #pragma HLS UNROLL
                output[sub_bank*Storage::SUB_BANK_SIZE+lane] =
                    storage.data[bank][sub_bank][row][lane];
            }
        }
    }

    template<typename Storage, typename T, int ROW_SIZE>
    void bankedSramFullWrite(
        Storage& storage,
        const unsigned address,
        const T input[ROW_SIZE]
    ){
        #pragma HLS INLINE
        const unsigned bank = address&(Storage::BANK_COUNT-1U);
        const unsigned row = address/Storage::BANK_COUNT;
        for(int sub_bank=0; sub_bank<Storage::SUB_BANK_COUNT; ++sub_bank){
            #pragma HLS UNROLL
            for(int lane=0; lane<Storage::SUB_BANK_SIZE; ++lane){
                #pragma HLS UNROLL
                storage.data[bank][sub_bank][row][lane] =
                    input[sub_bank*Storage::SUB_BANK_SIZE+lane];
            }
        }
    }

    template<typename Storage, typename T>
    void bankedSramNarrowRead(
        const Storage& storage,
        const unsigned address,
        const unsigned sub_bank,
        T output[Storage::SUB_BANK_SIZE]
    ){
        #pragma HLS INLINE
        const unsigned bank = address&(Storage::BANK_COUNT-1U);
        const unsigned row = address/Storage::BANK_COUNT;
        for(int lane=0; lane<Storage::SUB_BANK_SIZE; ++lane){
            #pragma HLS UNROLL
            output[lane] = storage.data[bank][sub_bank][row][lane];
        }
    }

    template<typename Storage, typename T>
    void bankedSramNarrowWrite(
        Storage& storage,
        const unsigned address,
        const unsigned sub_bank,
        const T input[Storage::SUB_BANK_SIZE]
    ){
        #pragma HLS INLINE
        const unsigned bank = address&(Storage::BANK_COUNT-1U);
        const unsigned row = address/Storage::BANK_COUNT;
        for(int lane=0; lane<Storage::SUB_BANK_SIZE; ++lane){
            #pragma HLS UNROLL
            storage.data[bank][sub_bank][row][lane] = input[lane];
        }
    }

}  // namespace streaming_v2_detail
}  // namespace fsa

#endif
