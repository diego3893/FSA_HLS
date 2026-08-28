/**
 * @file fsa_core_datapath.hpp
 * @author diego3893 (diegozcx@foxmail.com)
 * @brief 声明FSA_Core的数据通路
 * @date 2026-08-20
 * 
 * 
 */
#ifndef FSA_CORE_DATAPATH_HPP
#define FSA_CORE_DATAPATH_HPP

#include "fsa/accumulator.hpp"
#include "fsa/banked_sram.hpp"
#include "fsa/delayer.hpp"
#include "fsa/fsa_core_interface.hpp"
#include "fsa/state.hpp"
#include "fsa/systolic_array.hpp"

namespace fsa{
    /// @brief 每个accRAM窄读端口返回的元素数
    constexpr int ACC_DMA_READ_ELEMENTS_PER_PORT =
        SA_COLS/ACC_SUB_BANKS;

    /// @brief 将二维(port, element)映射到2020.2可稳定处理的一维下标
    constexpr int accDmaReadDataIndex(const int port, const int element){
        return port*ACC_DMA_READ_ELEMENTS_PER_PORT+element;
    }

    /// @brief 一个logical step的数据通路输入
    struct FsaCoreStepInput{
        // Core内部信号
        SpReadRequest sp_read{};
        AccReadRequest acc_read{};
        ValidData<CmpControl> cmp_ctrl{};
        ValidData<PECtrl> pe_ctrl[SA_ROWS]{};
        ValidData<AccumulatorControl> acc_ctrl{};
        elem_t sp_constant_value{};
        acc_t acc_constant_value{};

        // DMA与Core的信号
        bool spad_write_valid[nMemPorts]{};
        sram_address_t spad_write_addr[nMemPorts]{}; // 行地址
        sub_bank_index_t<SPAD_SUB_BANKS>
            spad_write_sub_bank[nMemPorts]{}; // sub-bank地址
        elem_t spad_write_data
            [nMemPorts][SA_ROWS/SPAD_SUB_BANKS]{};

        bool acc_dma_read_valid[nMemPorts]{};
        sram_address_t acc_dma_read_addr[nMemPorts]{};
        sub_bank_index_t<ACC_SUB_BANKS>
            acc_dma_read_sub_bank[nMemPorts]{};
    };

    /// @brief 一个logical step的数据通路输出和调试内容
    struct FsaCoreStepOutput{
        bool sp_read_ready = false;
        bool acc_read_ready = false;
        bool acc_write_ready = false;
        bool spad_write_ready[nMemPorts]{};
        bool acc_dma_read_ready[nMemPorts]{};
        bool acc_dma_response_valid[nMemPorts]{};
        // 物理含义仍是[nMemPorts][ACC_DMA_READ_ELEMENTS_PER_PORT]。
        // 展平可规避Vitis HLS 2020.2在非内联函数引用参数中为二维float
        // 数组生成非法LLVM IR的问题。
        acc_t acc_dma_read_data
            [nMemPorts*ACC_DMA_READ_ELEMENTS_PER_PORT]{};

        ElemVector delayer_out{};
        AccVector aligned_sa_out{};
        AccVector accumulator_out{};
        bool acc_write_valid = false;
        sram_address_t acc_write_addr = 0;
    };

    /**
     * @brief Core复位
     * 
     * @param state Core状态
     */
    void reset_fsa_core_datapath_state(FsaCoreDatapathState& state);

    /**
     * @brief FSA_Core的一个logical step
     * 
     * @param state 状态
     * @param input 输入
     * @param output 输出
     */
    void fsa_core_datapath_step(
        FsaCoreDatapathState& state,
        const FsaCoreStepInput& input,
        FsaCoreStepOutput& output
    );

}  // namespace fsa

#endif  // FSA_CORE_DATAPATH_HPP
