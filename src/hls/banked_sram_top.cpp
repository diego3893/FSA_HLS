#include "fsa/hls/banked_sram_top.hpp"

void sp_ram_top(
        const fsa::SpRAMTopInput& input,
        fsa::SpRAMTopOutput& output){
    #pragma HLS INTERFACE ap_ctrl_hs port=return
    #pragma HLS AGGREGATE variable=input compact=bit
    #pragma HLS AGGREGATE variable=output compact=bit
    #pragma HLS INTERFACE ap_none port=input
    #pragma HLS INTERFACE ap_none port=output
    #pragma HLS PIPELINE II=1

    static fsa::SpRAMState current{};

    // banks的全部维度分割由内联的bankedSRAMStep统一声明。
    #pragma HLS ARRAY_PARTITION variable=current.full_read_data complete dim=1
    #pragma HLS ARRAY_PARTITION variable=current.full_read_data complete dim=2

    if(input.reset){
        fsa::reset_sp_ram_state(current);
        output.full_read_ready = false;
        for(int element=0; element<fsa::SA_ROWS; ++element){
            #pragma HLS UNROLL
            output.full_read_data[(std::size_t)element] = {};
        }
        for(int port=0; port<fsa::nMemPorts; ++port){
            #pragma HLS UNROLL
            output.narrow_write_ready[(std::size_t)port] = false;
        }
        return;
    }

    fsa::SpRAMIO io{};
    io.fullRead[0].valid = input.full_read_valid;
    io.fullRead[0].addr = input.full_read_addr;
    for(int sub_bank=0; sub_bank<fsa::SPAD_SUB_BANKS; ++sub_bank){
        #pragma HLS UNROLL
        const std::size_t index = (std::size_t)sub_bank;
        io.fullRead[0].subBankMask[index] =
            input.full_read_sub_bank_mask[index];
    }

    for(int port=0; port<fsa::nMemPorts; ++port){
        #pragma HLS UNROLL
        const std::size_t index = (std::size_t)port;
        io.narrowWrite[index].valid = input.narrow_write_valid[index];
        io.narrowWrite[index].addr = input.narrow_write_addr[index];
        io.narrowWrite[index].subBankIdx =
            input.narrow_write_sub_bank_idx[index];
        for(int element=0;
                element<fsa::SA_ROWS/fsa::SPAD_SUB_BANKS; ++element){
            #pragma HLS UNROLL
            io.narrowWrite[index].data[(std::size_t)element] =
                input.narrow_write_data[index][(std::size_t)element];
        }
    }

    fsa::sp_ram_step(current, io);

    output.full_read_ready = io.fullRead[0].ready;
    for(int element=0; element<fsa::SA_ROWS; ++element){
        #pragma HLS UNROLL
        output.full_read_data[(std::size_t)element] =
            io.fullRead[0].data[(std::size_t)element];
    }

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
    #pragma HLS PIPELINE II=1

    static fsa::AccRAMState current{};

    // banks的全部维度分割由内联的bankedSRAMStep统一声明。
    #pragma HLS ARRAY_PARTITION variable=current.full_read_data complete dim=1
    #pragma HLS ARRAY_PARTITION variable=current.full_read_data complete dim=2
    #pragma HLS ARRAY_PARTITION variable=current.narrow_read_data complete dim=1
    #pragma HLS ARRAY_PARTITION variable=current.narrow_read_data complete dim=2

    if(input.reset){
        fsa::reset_acc_ram_state(current);
        output.full_read_ready = false;
        output.full_write_ready = false;
        for(int element=0; element<fsa::SA_COLS; ++element){
            #pragma HLS UNROLL
            output.full_read_data[(std::size_t)element] = {};
        }
        for(int port=0; port<fsa::nMemPorts; ++port){
            #pragma HLS UNROLL
            const std::size_t port_index = (std::size_t)port;
            output.narrow_read_ready[port_index] = false;
            for(int element=0;
                    element<fsa::SA_COLS/fsa::ACC_SUB_BANKS; ++element){
                #pragma HLS UNROLL
                output.narrow_read_data[port_index]
                                       [(std::size_t)element] = {};
            }
        }
        return;
    }

    fsa::AccRAMIO io{};
    io.fullRead[0].valid = input.full_read_valid;
    io.fullRead[0].addr = input.full_read_addr;
    for(int sub_bank=0; sub_bank<fsa::ACC_SUB_BANKS; ++sub_bank){
        #pragma HLS UNROLL
        const std::size_t index = (std::size_t)sub_bank;
        io.fullRead[0].subBankMask[index] =
            input.full_read_sub_bank_mask[index];
    }

    io.fullWrite[0].valid = input.full_write_valid;
    io.fullWrite[0].addr = input.full_write_addr;
    for(int sub_bank=0; sub_bank<fsa::ACC_SUB_BANKS; ++sub_bank){
        #pragma HLS UNROLL
        const std::size_t index = (std::size_t)sub_bank;
        io.fullWrite[0].subBankMask[index] =
            input.full_write_sub_bank_mask[index];
    }
    for(int element=0; element<fsa::SA_COLS; ++element){
        #pragma HLS UNROLL
        io.fullWrite[0].data[(std::size_t)element] =
            input.full_write_data[(std::size_t)element];
    }

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
    for(int element=0; element<fsa::SA_COLS; ++element){
        #pragma HLS UNROLL
        output.full_read_data[(std::size_t)element] =
            io.fullRead[0].data[(std::size_t)element];
    }
    output.full_write_ready = io.fullWrite[0].ready;

    for(int port=0; port<fsa::nMemPorts; ++port){
        #pragma HLS UNROLL
        const std::size_t index = (std::size_t)port;
        output.narrow_read_ready[index] = io.narrowRead[index].ready;
        for(int element=0;
                element<fsa::SA_COLS/fsa::ACC_SUB_BANKS; ++element){
            #pragma HLS UNROLL
            output.narrow_read_data[index][(std::size_t)element] =
                io.narrowRead[index].data[(std::size_t)element];
        }
    }
}
