#include "fsa/hls/banked_sram_top.hpp"

void sp_ram_top(
        const fsa::SpRAMTopInput& input,
        fsa::SpRAMTopOutput& output){
    #pragma HLS INTERFACE ap_ctrl_hs port=return
    #pragma HLS AGGREGATE variable=input compact=bit
    #pragma HLS AGGREGATE variable=output compact=bit
    #pragma HLS INTERFACE ap_none port=input
    #pragma HLS INTERFACE ap_none port=output

    static fsa::SpRAMState current{};

    #pragma HLS ARRAY_PARTITION variable=current.banks type=complete dim=1
    #pragma HLS ARRAY_PARTITION variable=current.banks type=complete dim=2
    #pragma HLS ARRAY_RESHAPE variable=current.banks type=complete dim=4

    if(input.reset){
        fsa::reset_sp_ram_state(current);
        output = fsa::SpRAMTopOutput{};
        return;
    }

    fsa::SpRAMIO io{};
    io.fullRead[0].valid = input.full_read_valid;
    io.fullRead[0].addr = input.full_read_addr;
    io.fullRead[0].subBankMask = input.full_read_sub_bank_mask;

    for(int port=0; port<fsa::nMemPorts; ++port){
        #pragma HLS UNROLL
        const std::size_t index = (std::size_t)port;
        io.narrowWrite[index].valid = input.narrow_write_valid[index];
        io.narrowWrite[index].addr = input.narrow_write_addr[index];
        io.narrowWrite[index].subBankIdx =
            input.narrow_write_sub_bank_idx[index];
        io.narrowWrite[index].data = input.narrow_write_data[index];
    }

    fsa::sp_ram_step(current, io);

    output.full_read_ready = io.fullRead[0].ready;
    output.full_read_data = io.fullRead[0].data;

    for(int port=0; port<fsa::nMemPorts; ++port){
        #pragma HLS UNROLL
        const std::size_t index = (std::size_t)port;
        output.narrow_write_ready[index] = io.narrowWrite[index].ready;
    }
}

void acc_ram_top(
        const fsa::AccRAMTopInput& input,
        fsa::AccRAMTopOutput& output){
    #pragma HLS INTERFACE ap_ctrl_hs port=return
    #pragma HLS AGGREGATE variable=input compact=bit
    #pragma HLS AGGREGATE variable=output compact=bit
    #pragma HLS INTERFACE ap_none port=input
    #pragma HLS INTERFACE ap_none port=output

    static fsa::AccRAMState current{};

    #pragma HLS ARRAY_PARTITION variable=current.banks type=complete dim=1
    #pragma HLS ARRAY_PARTITION variable=current.banks type=complete dim=2
    #pragma HLS ARRAY_RESHAPE variable=current.banks type=complete dim=4

    if(input.reset){
        fsa::reset_acc_ram_state(current);
        output = fsa::AccRAMTopOutput{};
        return;
    }

    fsa::AccRAMIO io{};
    io.fullRead[0].valid = input.full_read_valid;
    io.fullRead[0].addr = input.full_read_addr;
    io.fullRead[0].subBankMask = input.full_read_sub_bank_mask;

    io.fullWrite[0].valid = input.full_write_valid;
    io.fullWrite[0].addr = input.full_write_addr;
    io.fullWrite[0].subBankMask = input.full_write_sub_bank_mask;
    io.fullWrite[0].data = input.full_write_data;

    for(int port=0; port<fsa::nMemPorts; ++port){
        #pragma HLS UNROLL
        const std::size_t index = (std::size_t)port;
        io.narrowRead[index].valid = input.narrow_read_valid[index];
        io.narrowRead[index].addr = input.narrow_read_addr[index];
        io.narrowRead[index].subBankIdx =
            input.narrow_read_sub_bank_idx[index];
    }

    fsa::acc_ram_step(current, io);

    output.full_read_ready = io.fullRead[0].ready;
    output.full_read_data = io.fullRead[0].data;
    output.full_write_ready = io.fullWrite[0].ready;

    for(int port=0; port<fsa::nMemPorts; ++port){
        #pragma HLS UNROLL
        const std::size_t index = (std::size_t)port;
        output.narrow_read_ready[index] = io.narrowRead[index].ready;
        output.narrow_read_data[index] = io.narrowRead[index].data;
    }
}
