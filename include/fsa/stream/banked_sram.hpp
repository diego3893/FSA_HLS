/**
 * @file banked_sram.hpp
 * @brief 与FSA-main BankedSRAM端口语义一致的静态bank/sub-bank存储。
 */
#ifndef FSA_STREAM_BANKED_SRAM_HPP
#define FSA_STREAM_BANKED_SRAM_HPP

#include <ap_int.h>
#include <utils/x_hls_utils.h>

namespace fsa{
namespace streaming_v2_detail{

    template<typename T, int ELEMENT_WIDTH, int LOGICAL_ROWS, int ROW_SIZE,
             int BANKS, int SUB_BANKS>
    struct BankedSramStorage{
        static_assert(LOGICAL_ROWS>0, "SRAM logical rows must be positive");
        static_assert(ROW_SIZE%SUB_BANKS==0,
                      "SRAM row must split evenly into sub-banks");
        static_assert((BANKS&(BANKS-1))==0,
                      "SRAM bank count must be a power of two");

        static constexpr int SUB_BANK_SIZE = ROW_SIZE/SUB_BANKS;
        static constexpr int ELEMENT_BITS = ELEMENT_WIDTH;
        static constexpr int SUB_BANK_BITS = SUB_BANK_SIZE*ELEMENT_WIDTH;
        static constexpr int BANK_COUNT = BANKS;
        static constexpr int SUB_BANK_COUNT = SUB_BANKS;
        static constexpr int BANK_DEPTH =
            (LOGICAL_ROWS+BANKS-1)/BANKS;

        // Scala BankedSRAM中的每个sub-bank是一块宽度为一个DMA beat的
        // SRAM，而不是每个元素各自一块RAM。保持物理字打包可避免HLS
        // 把FP16/FP32 lane拆成多块独立BRAM。
        ap_uint<SUB_BANK_BITS> data[BANKS][SUB_BANKS][BANK_DEPTH];
    };

    template<typename T, int WIDTH>
    ap_uint<WIDTH> bankedSramElementBits(const T value){
        #pragma HLS INLINE
        const fp_struct<T> view(value);
        return (ap_uint<WIDTH>)view.data();
    }

    template<typename T, int WIDTH>
    T bankedSramElementFromBits(const ap_uint<WIDTH> bits){
        #pragma HLS INLINE
        const fp_struct<T> view(bits);
        return view.to_ieee();
    }

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
            const ap_uint<Storage::SUB_BANK_BITS> word =
                storage.data[bank][sub_bank][row];
            for(int lane=0; lane<Storage::SUB_BANK_SIZE; ++lane){
                #pragma HLS UNROLL
                output[sub_bank*Storage::SUB_BANK_SIZE+lane] =
                    bankedSramElementFromBits<T, Storage::ELEMENT_BITS>(
                        word.range(
                            (lane+1)*Storage::ELEMENT_BITS-1,
                            lane*Storage::ELEMENT_BITS
                        )
                    );
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
            ap_uint<Storage::SUB_BANK_BITS> word = 0;
            for(int lane=0; lane<Storage::SUB_BANK_SIZE; ++lane){
                #pragma HLS UNROLL
                word.range(
                    (lane+1)*Storage::ELEMENT_BITS-1,
                    lane*Storage::ELEMENT_BITS
                ) = bankedSramElementBits<T, Storage::ELEMENT_BITS>(
                    input[sub_bank*Storage::SUB_BANK_SIZE+lane]
                );
            }
            storage.data[bank][sub_bank][row] = word;
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
        const ap_uint<Storage::SUB_BANK_BITS> word =
            storage.data[bank][sub_bank][row];
        for(int lane=0; lane<Storage::SUB_BANK_SIZE; ++lane){
            #pragma HLS UNROLL
            output[lane] =
                bankedSramElementFromBits<T, Storage::ELEMENT_BITS>(
                    word.range(
                        (lane+1)*Storage::ELEMENT_BITS-1,
                        lane*Storage::ELEMENT_BITS
                    )
                );
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
        ap_uint<Storage::SUB_BANK_BITS> word = 0;
        for(int lane=0; lane<Storage::SUB_BANK_SIZE; ++lane){
            #pragma HLS UNROLL
            word.range(
                (lane+1)*Storage::ELEMENT_BITS-1,
                lane*Storage::ELEMENT_BITS
            ) = bankedSramElementBits<T, Storage::ELEMENT_BITS>(input[lane]);
        }
        storage.data[bank][sub_bank][row] = word;
    }

}  // namespace streaming_v2_detail
}  // namespace fsa

#endif
