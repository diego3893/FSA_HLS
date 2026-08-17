/**
 * @file banked_sram_top.hpp
 * @author diego3893 (diegozcx@foxmail.com)
 * @brief Scratchpad SRAM和Accumulator SRAM的顶层接口
 * @date 2026-08-17
 * 
 * 
 */
#ifndef BANKED_SRAM_TOP_HPP
#define BANKED_SRAM_TOP_HPP

#include <array>
#include <cstddef>

#include "fsa/banked_sram.hpp"

namespace fsa{

    using SpRAMSubBankMask =
        std::array<bool, (std::size_t)SPAD_SUB_BANKS>;
    using SpRAMNarrowData = std::array<
        elem_t,
        (std::size_t)SRAMNarrowWrite<
            elem_t, SA_ROWS, SPAD_SUB_BANKS>::DataSize>;

    /**
     * @brief Scratchpad SRAM顶层输入
     *
     */
    struct SpRAMTopInput{
        /// @brief 清除同步读响应寄存器；SRAM存储内容保持不变
        bool reset = false;

        bool full_read_valid = false;
        sram_address_t full_read_addr = 0;
        SpRAMSubBankMask full_read_sub_bank_mask{};

        std::array<bool, (std::size_t)nMemPorts> narrow_write_valid{};
        std::array<sram_address_t, (std::size_t)nMemPorts>
            narrow_write_addr{};
        std::array<sub_bank_index_t<SPAD_SUB_BANKS>,
                   (std::size_t)nMemPorts> narrow_write_sub_bank_idx{};
        std::array<SpRAMNarrowData, (std::size_t)nMemPorts>
            narrow_write_data{};
    };

    /**
     * @brief Scratchpad SRAM顶层输出
     * 
     */
    struct SpRAMTopOutput{
        bool full_read_ready = false;

        /// @brief 上一次被接受的整行读请求所返回的数据
        ElemVector full_read_data{};

        std::array<bool, (std::size_t)nMemPorts> narrow_write_ready{};
    };

    using AccRAMSubBankMask =
        std::array<bool, (std::size_t)ACC_SUB_BANKS>;
    using AccRAMNarrowData = std::array<
        acc_t,
        (std::size_t)SRAMNarrowRead<
            acc_t, SA_COLS, ACC_SUB_BANKS>::DataSize>;

    /**
     * @brief Accumulator SRAM顶层输入
     *
     */
    struct AccRAMTopInput{
        /// @brief 清除同步读响应寄存器；SRAM存储内容保持不变
        bool reset = false;

        bool full_read_valid = false;
        sram_address_t full_read_addr = 0;
        AccRAMSubBankMask full_read_sub_bank_mask{};

        bool full_write_valid = false;
        sram_address_t full_write_addr = 0;
        AccRAMSubBankMask full_write_sub_bank_mask{};
        AccVector full_write_data{};

        std::array<bool, (std::size_t)nMemPorts> narrow_read_valid{};
        std::array<sram_address_t, (std::size_t)nMemPorts>
            narrow_read_addr{};
        std::array<sub_bank_index_t<ACC_SUB_BANKS>,
                   (std::size_t)nMemPorts> narrow_read_sub_bank_idx{};
    };

    /**
     * @brief Accumulator SRAM顶层输出
     * 
     */
    struct AccRAMTopOutput{
        bool full_read_ready = false;

        /// @brief 上一次被接受的整行读请求所返回的数据
        AccVector full_read_data{};

        bool full_write_ready = false;

        std::array<bool, (std::size_t)nMemPorts> narrow_read_ready{};

        /// @brief 各端口上一次被接受的窄读请求所返回的数据
        std::array<AccRAMNarrowData, (std::size_t)nMemPorts>
            narrow_read_data{};
    };

}  // namespace fsa

void sp_ram_top(const fsa::SpRAMTopInput& input, fsa::SpRAMTopOutput& output);

void acc_ram_top(const fsa::AccRAMTopInput& input, fsa::AccRAMTopOutput& output);

#endif  // BANKED_SRAM_TOP_HPP
