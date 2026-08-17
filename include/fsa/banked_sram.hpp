/**
 * @file banked_sram.hpp
 * @author diego3893 (diegozcx@foxmail.com)
 * @brief 声明支持整行访问和DMA窄访问的banked SRAM
 * @date 2026-08-13
 * 
 * 
 */
#ifndef BANKED_SRAM_HPP
#define BANKED_SRAM_HPP

#include <array>
#include <cstddef>

#include "fsa/config.hpp"
#include "fsa/state.hpp"
#include "fsa/types.hpp"

namespace fsa{

    /**
     * @brief BankedSRAM的整行读端口
     *
     * @tparam T 单个元素类型
     * @tparam RowSize 一整行的元素数量
     * @tparam NSubBanks 一整行被拆成的sub-bank数量
     */
    template <typename T, int RowSize, int NSubBanks>
    struct SRAMFullRead{
        bool valid = false;
        bool ready = false;

        /// @brief 逻辑行号
        sram_address_t addr = 0;

        /// @brief 本次实际使用的sub-bank
        std::array<bool, (std::size_t)NSubBanks> subBankMask{};

        /// @brief 返回的完整逻辑行
        std::array<T, (std::size_t)RowSize> data{};

        /// @brief 选中一行中的全部sub-bank
        void setFullMask(){
            for(int sub_bank=0; sub_bank<NSubBanks; ++sub_bank){
                subBankMask[(std::size_t)sub_bank] = true;
            }
        }
    };

    /**
     * @brief BankedSRAM的整行写端口
     *
     * @tparam T 单个元素类型
     * @tparam RowSize 一整行的元素数量
     * @tparam NSubBanks 一整行被拆成的sub-bank数量
     */
    template <typename T, int RowSize, int NSubBanks>
    struct SRAMFullWrite{
        bool valid = false;
        bool ready = false;

        /// @brief 逻辑行号
        sram_address_t addr = 0;

        /// @brief 选择本次写入的sub-bank
        std::array<bool, (std::size_t)NSubBanks> subBankMask{};

        /// @brief 要写入的完整逻辑行
        std::array<T, (std::size_t)RowSize> data{};

        /// @brief 选中一行中的全部sub-bank
        void setFullMask(){
            for(int sub_bank=0; sub_bank<NSubBanks; ++sub_bank){
                subBankMask[(std::size_t)sub_bank] = true;
            }
        }
    };

    /**
     * @brief BankedSRAM的窄读端口
     *
     * @tparam T 单个元素类型
     * @tparam RowSize 一整行的元素数量
     * @tparam NSubBanks 一整行被拆成的sub-bank数量
     */
    template <typename T, int RowSize, int NSubBanks>
    struct SRAMNarrowRead{
        static_assert(RowSize%NSubBanks==0,
                      "RowSize必须能被NSubBanks整除");

        /// @brief 一个sub-bank包含的元素数量
        static constexpr int DataSize = RowSize/NSubBanks;

        bool valid = false;
        bool ready = false;

        /// @brief 逻辑行号
        sram_address_t addr = 0;

        /// @brief 本次访问一行中的哪个sub-bank
        sub_bank_index_t<NSubBanks> subBankIdx = 0;

        /// @brief 上一拍窄读请求返回的一个memory beat
        std::array<T, (std::size_t)DataSize> data{};
    };

    /**
     * @brief BankedSRAM的窄写端口
     *
     * @tparam T 单个元素类型
     * @tparam RowSize 一整行的元素数量
     * @tparam NSubBanks 一整行被拆成的sub-bank数量
     */
    template <typename T, int RowSize, int NSubBanks>
    struct SRAMNarrowWrite{
        static_assert(RowSize%NSubBanks==0,
                      "RowSize必须能被NSubBanks整除");

        /// @brief 一个sub-bank包含的元素数量
        static constexpr int DataSize = RowSize/NSubBanks;

        bool valid = false;
        bool ready = false;

        /// @brief 逻辑行号
        sram_address_t addr = 0;

        /// @brief 本次访问一行中的哪个sub-bank
        sub_bank_index_t<NSubBanks> subBankIdx = 0;

        /// @brief 要写入的一个memory beat
        std::array<T, (std::size_t)DataSize> data{};
    };

    /**
     * @brief BankedSRAM全部端口的集合
     * 
     * @tparam T 元素类型
     * @tparam RowSize 一行的元素个数
     * @tparam NSubBanks sub-bank个数
     * @tparam NFullRead 整行读端口数量
     * @tparam NFullWrite 整行写端口数量
     * @tparam NNarrowRead 窄读端口数量
     * @tparam NNarrowWrite 窄写端口数量
     */
    template <typename T, int RowSize, int NSubBanks,
              int NFullRead, int NFullWrite,
              int NNarrowRead, int NNarrowWrite>
    struct BankedSRAMIO{
        static_assert(NFullRead>=0 && NFullWrite>=0 &&
                      NNarrowRead>=0 && NNarrowWrite>=0,
                      "SRAM端口数量不能为负数");

        // 消除array<T, 0>造成的越界问题
        static constexpr int FullReadStorage =
            NFullRead>0 ? NFullRead : 1;
        static constexpr int FullWriteStorage =
            NFullWrite>0 ? NFullWrite : 1;
        static constexpr int NarrowReadStorage =
            NNarrowRead>0 ? NNarrowRead : 1;
        static constexpr int NarrowWriteStorage =
            NNarrowWrite>0 ? NNarrowWrite : 1;

        std::array<SRAMFullRead<T, RowSize, NSubBanks>,
                   (std::size_t)FullReadStorage> fullRead{};

        std::array<SRAMFullWrite<T, RowSize, NSubBanks>,
                   (std::size_t)FullWriteStorage> fullWrite{};

        std::array<SRAMNarrowRead<T, RowSize, NSubBanks>,
                   (std::size_t)NarrowReadStorage> narrowRead{};

        std::array<SRAMNarrowWrite<T, RowSize, NSubBanks>,
                   (std::size_t)NarrowWriteStorage> narrowWrite{};
    };

    /// @brief Scratchpad SRAM全部读写端口
    using SpRAMIO = BankedSRAMIO<
        elem_t, SA_ROWS, SPAD_SUB_BANKS,
        1, 0, 0, nMemPorts>;

    /// @brief Accumulator SRAM全部读写端口
    using AccRAMIO = BankedSRAMIO<
        acc_t, SA_COLS, ACC_SUB_BANKS,
        1, 1, nMemPorts, 0>;

    /**
     * @brief 复位Scratchpad的读响应状态
     * 
     * @param state SRAM内容以及同步读响应寄存器
     */
    void reset_sp_ram_state(SpRAMState& state);

    /**
     * @brief 计算Scratchpad的一个时钟步骤
     * 
     * @param state SRAM内容以及同步读响应寄存器
     * @param io 本拍请求、ready和上一拍读返回值
     */
    void sp_ram_step(SpRAMState& state, SpRAMIO& io);

    /**
     * @brief 复位Accumulator SRAM的读响应状态
     * 
     * @param state SRAM内容以及同步读响应寄存器
     */
    void reset_acc_ram_state(AccRAMState& state);

    /**
     * @brief 计算Accumulator SRAM的一个时钟步骤
     * 
     * @param state SRAM内容以及同步读响应寄存器
     * @param io 本拍请求、ready和上一拍读返回值
     */
    void acc_ram_step(AccRAMState& state, AccRAMIO& io);

}  // namespace fsa

#endif // !BANKED_SRAM_HPP