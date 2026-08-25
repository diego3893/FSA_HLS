/**
 * @file fsa_core_top.hpp
 * @author diego3893 (diegozcx@foxmail.com)
 * @brief FSA核心数据通路的系统级Vitis HLS顶层接口
 * @date 2026-08-18
 * 
 * 
 */

#ifndef FSA_CORE_TOP_HPP
#define FSA_CORE_TOP_HPP

#include "fsa/banked_sram.hpp"
#include "fsa/control.hpp"
#include "fsa/delayer.hpp"
#include "fsa/fsa_core_interface.hpp"
#include "fsa/types.hpp"

namespace fsa{

    /** @brief 无Controller阶段由testbench逐逻辑step提供的系统输入。 */
    struct FsaCoreTopInput{
        bool reset = false;

        SpReadRequest sp_read{};
        AccReadRequest acc_read{};

        ValidData<CmpControl> cmp_ctrl{};
        ValidData<PECtrl> pe_ctrl[SA_ROWS]{};
        ValidData<AccumulatorControl> acc_ctrl{};

        elem_t sp_constant_value{};
        acc_t acc_constant_value{};

        // 通过正式Scratchpad窄写端口预装Q、K、V测试数据。
        bool spad_write_valid[nMemPorts]{};
        sram_address_t spad_write_addr[nMemPorts]{};
        sub_bank_index_t<SPAD_SUB_BANKS>
            spad_write_sub_bank[nMemPorts]{};
        elem_t spad_write_data
            [nMemPorts][SA_ROWS/SPAD_SUB_BANKS]{};

        // 通过正式Accumulator SRAM窄读端口观察RMW结果。
        bool acc_dma_read_valid[nMemPorts]{};
        sram_address_t acc_dma_read_addr[nMemPorts]{};
        sub_bank_index_t<ACC_SUB_BANKS>
            acc_dma_read_sub_bank[nMemPorts]{};
    };

    /** @brief 系统握手、测试读回和集成调试输出。 */
    struct FsaCoreTopOutput{
        bool sp_read_ready = false;
        bool acc_read_ready = false;
        bool acc_write_ready = false;

        bool spad_write_ready[nMemPorts]{};
        bool acc_dma_read_ready[nMemPorts]{};
        bool acc_dma_response_valid[nMemPorts]{};
        acc_t acc_dma_read_data
            [nMemPorts][SA_COLS/ACC_SUB_BANKS]{};

        // 集成初期保留的逐段观测点。
        ElemVector delayer_out{};
        AccVector aligned_sa_out{};
        AccVector accumulator_out{};
        bool acc_write_valid = false;
        sram_address_t acc_write_addr = 0;
    };

}  // namespace fsa

/**
 * @brief 推进FSA核心数据通路一个Chisel逻辑step
 *
 * 一次ap_ctrl_hs事务可能占用多个物理时钟周期；这里的一次调用仅表示
 * Scratchpad、Delayer、SA、Accumulator和accRAM统一推进一次行为状态。
 */
void fsa_core_top(
    const fsa::FsaCoreTopInput& input,
    fsa::FsaCoreTopOutput& output
);

#endif  // FSA_CORE_TOP_HPP
