#ifndef FSA_CORE_EXECUTE_TOP_HPP
#define FSA_CORE_EXECUTE_TOP_HPP

#include "fsa/fsa_core_datapath.hpp"
#include "fsa/instruction.hpp"

namespace fsa{
    struct FsaCoreExecuteInput{
        bool reset = false;
        bool instruction_valid = false;
        MatrixInstruction instruction{};

        bool spad_write_valid[nMemPorts]{};
        sram_address_t spad_write_addr[nMemPorts]{};
        sub_bank_index_t<SPAD_SUB_BANKS>
            spad_write_sub_bank[nMemPorts]{};
        elem_t spad_write_data
            [nMemPorts][SA_ROWS/SPAD_SUB_BANKS]{};

        bool acc_dma_read_valid[nMemPorts]{};
        sram_address_t acc_dma_read_addr[nMemPorts]{};
        sub_bank_index_t<ACC_SUB_BANKS>
            acc_dma_read_sub_bank[nMemPorts]{};
    };

    struct FsaCoreExecuteOutput{
        bool instruction_done = false;
        bool busy = false;
        ap_uint<16> executed_steps = 0;

        bool spad_write_ready[nMemPorts]{};
        bool acc_dma_read_ready[nMemPorts]{};
        bool acc_dma_response_valid[nMemPorts]{};
        acc_t acc_dma_read_data
            [nMemPorts][SA_COLS/ACC_SUB_BANKS]{};

        bool acc_write_valid = false;
        sram_address_t acc_write_addr = 0;
    };

}  // namespace fsa

void fsa_core_execute_top(
    const fsa::FsaCoreExecuteInput& input,
    fsa::FsaCoreExecuteOutput& output
);

#endif  // FSA_CORE_EXECUTE_TOP_HPP
