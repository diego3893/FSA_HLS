#include "fsa/hls/fsa_core_top.hpp"

#include "fsa/accumulator.hpp"
#include "fsa/banked_sram.hpp"
#include "fsa/delayer.hpp"
#include "fsa/systolic_array.hpp"

namespace{

    /** @brief 系统顶层跨调用保存的全部寄存器和片上存储状态。 */
    struct FsaCoreState{
        fsa::SpRAMState sp_ram{};
        fsa::ElemInputDelayerState input_delayer{};
        fsa::SystolicArrayState sa{};
        fsa::OutputDelayerState output_delayer{};
        fsa::AccumulatorState accumulator{};
        fsa::AccRAMState acc_ram{};

        // Scratchpad同步读响应对应的布局和常量元数据。
        bool sp_response_valid = false;
        bool sp_response_is_constant = false;
        bool sp_rev_input = false;
        bool sp_delay_output = false;
        bool sp_rev_output = false;
        fsa::elem_t sp_constant_value{};

        // Accumulator SRAM同步读响应对应的常量选择和RMW元数据。
        bool acc_response_valid = false;
        bool acc_response_is_constant = false;
        fsa::acc_t acc_constant_value{};
        bool acc_write_valid = false;
        fsa::sram_address_t acc_write_addr = 0;

        // BankedSRAM本身不提供response-valid，单独为testbench延迟一拍。
        bool acc_dma_response_valid[fsa::nMemPorts]{};
    };

    void resetCoreState(FsaCoreState& state){
        #pragma HLS INLINE

        fsa::reset_sp_ram_state(state.sp_ram);
        fsa::reset_input_delayer_state(state.input_delayer);
        fsa::reset_systolic_array_state(state.sa);
        fsa::reset_output_delayer_state(state.output_delayer);
        fsa::reset_accumulator_state(state.accumulator);
        fsa::reset_acc_ram_state(state.acc_ram);

        state.sp_response_valid = false;
        state.sp_response_is_constant = false;
        state.sp_rev_input = false;
        state.sp_delay_output = false;
        state.sp_rev_output = false;
        state.sp_constant_value = fsa::elem_t{};

        state.acc_response_valid = false;
        state.acc_response_is_constant = false;
        state.acc_constant_value = fsa::acc_t{};
        state.acc_write_valid = false;
        state.acc_write_addr = 0;

        for(int port=0; port<fsa::nMemPorts; ++port){
            #pragma HLS UNROLL
            state.acc_dma_response_valid[port] = false;
        }
    }

    /**
     * @brief 保留SA的独立层次和原顶层II约束，避免跨PE共享MAC资源。
     *
     * 该函数没有外部接口pragma，不是第二个ap_ctrl_hs顶层。
     */
    void fsa_core_sa_stage(
        const fsa::SystolicArrayState& current,
        fsa::SystolicArrayState& next,
        fsa::SystolicArrayIO& io
    ){
        #pragma HLS INLINE off
        #pragma HLS PIPELINE II=16
        #pragma HLS LATENCY max=17

        #pragma HLS ARRAY_PARTITION variable=current.mesh type=complete dim=0
        #pragma HLS ARRAY_PARTITION variable=current.cmp_array type=complete dim=1
        #pragma HLS ARRAY_PARTITION variable=current.cmp_ctrl_pipe type=complete dim=1
        #pragma HLS ARRAY_PARTITION variable=current.pe_ctrl_pipe type=complete dim=0
        #pragma HLS ARRAY_PARTITION variable=current.cmp_d_output_pipe type=complete dim=1
        #pragma HLS ARRAY_PARTITION variable=current.r_output_pipe type=complete dim=0
        #pragma HLS ARRAY_PARTITION variable=current.d_output_pipe type=complete dim=0
        #pragma HLS ARRAY_PARTITION variable=current.u_output_pipe type=complete dim=0

        #pragma HLS ARRAY_PARTITION variable=next.mesh type=complete dim=0
        #pragma HLS ARRAY_PARTITION variable=next.cmp_array type=complete dim=1
        #pragma HLS ARRAY_PARTITION variable=next.cmp_ctrl_pipe type=complete dim=1
        #pragma HLS ARRAY_PARTITION variable=next.pe_ctrl_pipe type=complete dim=0
        #pragma HLS ARRAY_PARTITION variable=next.cmp_d_output_pipe type=complete dim=1
        #pragma HLS ARRAY_PARTITION variable=next.r_output_pipe type=complete dim=0
        #pragma HLS ARRAY_PARTITION variable=next.d_output_pipe type=complete dim=0
        #pragma HLS ARRAY_PARTITION variable=next.u_output_pipe type=complete dim=0

        #pragma HLS ARRAY_PARTITION variable=io.pe_ctrl type=complete dim=1
        #pragma HLS ARRAY_PARTITION variable=io.pe_data type=complete dim=1
        #pragma HLS ARRAY_PARTITION variable=io.acc_out type=complete dim=1

        fsa::systolic_array_step(current, next, io);
    }

}  // namespace

void fsa_core_top(
    const fsa::FsaCoreTopInput& input,
    fsa::FsaCoreTopOutput& output
){
    #pragma HLS INTERFACE ap_ctrl_hs port=return
    #pragma HLS AGGREGATE variable=input compact=bit
    #pragma HLS AGGREGATE variable=output compact=bit
    #pragma HLS INTERFACE ap_none port=input
    #pragma HLS INTERFACE ap_none port=output

    static FsaCoreState current{};

    // 系统顶层重新声明所有需要同一逻辑step并行访问的状态维度。
    #pragma HLS ARRAY_PARTITION variable=current.input_delayer.out_delay_pipe type=complete dim=0
    #pragma HLS ARRAY_PARTITION variable=current.output_delayer.out_delay_pipe type=complete dim=0
    #pragma HLS ARRAY_PARTITION variable=current.sa.mesh type=complete dim=0
    #pragma HLS ARRAY_PARTITION variable=current.sa.cmp_array type=complete dim=1
    #pragma HLS ARRAY_PARTITION variable=current.sa.cmp_ctrl_pipe type=complete dim=1
    #pragma HLS ARRAY_PARTITION variable=current.sa.pe_ctrl_pipe type=complete dim=0
    #pragma HLS ARRAY_PARTITION variable=current.sa.cmp_d_output_pipe type=complete dim=1
    #pragma HLS ARRAY_PARTITION variable=current.sa.r_output_pipe type=complete dim=0
    #pragma HLS ARRAY_PARTITION variable=current.sa.d_output_pipe type=complete dim=0
    #pragma HLS ARRAY_PARTITION variable=current.sa.u_output_pipe type=complete dim=0
    #pragma HLS ARRAY_PARTITION variable=current.accumulator.scale type=complete dim=1
    #pragma HLS ARRAY_PARTITION variable=current.accumulator.reciprocal type=complete dim=1

    #pragma HLS ARRAY_PARTITION variable=current.sp_ram.banks type=complete dim=1
    #pragma HLS ARRAY_PARTITION variable=current.sp_ram.banks type=complete dim=2
    #pragma HLS ARRAY_RESHAPE variable=current.sp_ram.banks type=complete dim=4
    #pragma HLS ARRAY_PARTITION variable=current.sp_ram.full_read_data type=complete dim=1
    #pragma HLS ARRAY_PARTITION variable=current.sp_ram.full_read_data type=complete dim=2

    #pragma HLS ARRAY_PARTITION variable=current.acc_ram.banks type=complete dim=1
    #pragma HLS ARRAY_PARTITION variable=current.acc_ram.banks type=complete dim=2
    #pragma HLS ARRAY_RESHAPE variable=current.acc_ram.banks type=complete dim=4
    #pragma HLS ARRAY_PARTITION variable=current.acc_ram.full_read_data type=complete dim=1
    #pragma HLS ARRAY_PARTITION variable=current.acc_ram.full_read_data type=complete dim=2
    #pragma HLS ARRAY_PARTITION variable=current.acc_ram.narrow_read_data type=complete dim=1
    #pragma HLS ARRAY_PARTITION variable=current.acc_ram.narrow_read_data type=complete dim=2
    #pragma HLS ARRAY_PARTITION variable=current.acc_dma_response_valid type=complete dim=1

    if(input.reset){
        resetCoreState(current);
        output = fsa::FsaCoreTopOutput{};
        return;
    }

    // 1. Scratchpad接收本拍请求，并把上一拍整行响应送到io.data。
    fsa::SpRAMIO sp_ram_io{};
    sp_ram_io.fullRead[0].valid =
        input.sp_read.valid && !input.sp_read.is_constant;
    sp_ram_io.fullRead[0].addr = input.sp_read.addr;
    sp_ram_io.fullRead[0].setFullMask();

    for(int port=0; port<fsa::nMemPorts; ++port){
        #pragma HLS UNROLL
        sp_ram_io.narrowWrite[port].valid = input.spad_write_valid[port];
        sp_ram_io.narrowWrite[port].addr = input.spad_write_addr[port];
        sp_ram_io.narrowWrite[port].subBankIdx =
            input.spad_write_sub_bank[port];
        for(int element=0;
                element<fsa::SA_ROWS/fsa::SPAD_SUB_BANKS; ++element){
            #pragma HLS UNROLL
            sp_ram_io.narrowWrite[port].data[(std::size_t)element] =
                input.spad_write_data[port][element];
        }
    }

    fsa::sp_ram_step(current.sp_ram, sp_ram_io);

    output.sp_read_ready = input.sp_read.is_constant
        ? true
        : sp_ram_io.fullRead[0].ready;
    for(int port=0; port<fsa::nMemPorts; ++port){
        #pragma HLS UNROLL
        output.spad_write_ready[port] =
            sp_ram_io.narrowWrite[port].ready;
    }

    // 2. 上一拍Scratchpad响应及其布局元数据进入InputDelayer。
    fsa::InputDelayerIO input_delayer_io{};
    input_delayer_io.in.valid = current.sp_response_valid;
    input_delayer_io.in.bits.rev_input = current.sp_rev_input;
    input_delayer_io.in.bits.delay_output = current.sp_delay_output;
    input_delayer_io.in.bits.rev_output = current.sp_rev_output;
    for(int row=0; row<fsa::SA_ROWS; ++row){
        #pragma HLS UNROLL
        input_delayer_io.in.bits.data[(std::size_t)row] =
            current.sp_response_is_constant
                ? current.sp_constant_value
                : sp_ram_io.fullRead[0].data[(std::size_t)row];
    }

    fsa::ElemInputDelayerState next_input_delayer{};
    #pragma HLS ARRAY_PARTITION variable=next_input_delayer.out_delay_pipe type=complete dim=0
    fsa::input_delayer_step(
        current.input_delayer,
        next_input_delayer,
        input_delayer_io
    );
    output.delayer_out = input_delayer_io.out;

    // 3. Delayer输出、PE控制和CMP控制共同推进完整SystolicArray。
    fsa::SystolicArrayIO sa_io{};
    #pragma HLS ARRAY_PARTITION variable=sa_io.pe_ctrl type=complete dim=1
    #pragma HLS ARRAY_PARTITION variable=sa_io.pe_data type=complete dim=1
    #pragma HLS ARRAY_PARTITION variable=sa_io.acc_out type=complete dim=1
    sa_io.pe_data = input_delayer_io.out;
    sa_io.cmp_ctrl = input.cmp_ctrl;
    for(int row=0; row<fsa::SA_ROWS; ++row){
        #pragma HLS UNROLL
        sa_io.pe_ctrl[row] = input.pe_ctrl[row];
    }

    fsa::SystolicArrayState next_sa{};
    fsa_core_sa_stage(current.sa, next_sa, sa_io);

    // 4. 与Chisel FSA.scala一致，OutputDelayer直接连接SA输出bits。
    fsa::OutputDelayerIO output_delayer_io{};
    for(int col=0; col<fsa::SA_COLS; ++col){
        #pragma HLS UNROLL
        output_delayer_io.in[(std::size_t)col] = sa_io.acc_out[col].bits;
    }

    fsa::OutputDelayerState next_output_delayer{};
    #pragma HLS ARRAY_PARTITION variable=next_output_delayer.out_delay_pipe type=complete dim=0
    fsa::output_delayer_step(
        current.output_delayer,
        next_output_delayer,
        output_delayer_io
    );
    output.aligned_sa_out = output_delayer_io.out;

    // 5. Accumulator消费对齐的SA输出和上一拍accRAM/常量响应。
    fsa::AccumulatorIO accumulator_io{};
    #pragma HLS ARRAY_PARTITION variable=accumulator_io.sa_in type=complete dim=1
    #pragma HLS ARRAY_PARTITION variable=accumulator_io.sram_in type=complete dim=1
    #pragma HLS ARRAY_PARTITION variable=accumulator_io.sram_out type=complete dim=1
    accumulator_io.ctrl_in = input.acc_ctrl;
    for(int col=0; col<fsa::SA_COLS; ++col){
        #pragma HLS UNROLL
        accumulator_io.sa_in[(std::size_t)col] =
            output_delayer_io.out[(std::size_t)col];
        accumulator_io.sram_in[(std::size_t)col] =
            current.acc_response_is_constant
                ? current.acc_constant_value
                : current.acc_ram.full_read_data[0][(std::size_t)col];
    }

    fsa::AccumulatorState next_accumulator{};
    #pragma HLS ARRAY_PARTITION variable=next_accumulator.scale type=complete dim=1
    #pragma HLS ARRAY_PARTITION variable=next_accumulator.reciprocal type=complete dim=1
    fsa::accumulator_step(
        current.accumulator,
        next_accumulator,
        accumulator_io
    );
    output.accumulator_out = accumulator_io.sram_out;

    // 6. Accumulator本拍结果写回上一拍RMW地址，同时接收新的读请求。
    fsa::AccRAMIO acc_ram_io{};
    acc_ram_io.fullRead[0].valid =
        input.acc_read.valid && !input.acc_read.is_constant;
    acc_ram_io.fullRead[0].addr = input.acc_read.addr;
    acc_ram_io.fullRead[0].setFullMask();

    acc_ram_io.fullWrite[0].valid = current.acc_write_valid;
    acc_ram_io.fullWrite[0].addr = current.acc_write_addr;
    acc_ram_io.fullWrite[0].setFullMask();
    for(int col=0; col<fsa::SA_COLS; ++col){
        #pragma HLS UNROLL
        acc_ram_io.fullWrite[0].data[(std::size_t)col] =
            accumulator_io.sram_out[(std::size_t)col];
    }

    for(int port=0; port<fsa::nMemPorts; ++port){
        #pragma HLS UNROLL
        acc_ram_io.narrowRead[port].valid = input.acc_dma_read_valid[port];
        acc_ram_io.narrowRead[port].addr = input.acc_dma_read_addr[port];
        acc_ram_io.narrowRead[port].subBankIdx =
            input.acc_dma_read_sub_bank[port];
    }

    fsa::acc_ram_step(current.acc_ram, acc_ram_io);

    output.acc_read_ready = input.acc_read.is_constant
        ? true
        : acc_ram_io.fullRead[0].ready;
    output.acc_write_ready = acc_ram_io.fullWrite[0].ready;
    output.acc_write_valid = current.acc_write_valid;
    output.acc_write_addr = current.acc_write_addr;

    for(int port=0; port<fsa::nMemPorts; ++port){
        #pragma HLS UNROLL
        output.acc_dma_read_ready[port] =
            acc_ram_io.narrowRead[port].ready;
        output.acc_dma_response_valid[port] =
            current.acc_dma_response_valid[port];
        for(int element=0;
                element<fsa::SA_COLS/fsa::ACC_SUB_BANKS; ++element){
            #pragma HLS UNROLL
            output.acc_dma_read_data[port][element] =
                acc_ram_io.narrowRead[port].data[(std::size_t)element];
        }
    }

    // 7. 非SRAM模块统一提交next；SRAM step已原地提交读响应和写入。
    current.input_delayer = next_input_delayer;
    current.sa = next_sa;
    current.output_delayer = next_output_delayer;
    current.accumulator = next_accumulator;

    // 8. 本拍请求握手结果及其元数据统一锁存到下一逻辑step。
    const bool sp_request_accepted =
        input.sp_read.valid && output.sp_read_ready;
    const bool acc_request_accepted =
        input.acc_read.valid && output.acc_read_ready;

    current.sp_response_valid = sp_request_accepted;
    current.sp_response_is_constant = input.sp_read.is_constant;
    current.sp_rev_input = input.sp_read.rev_sram_out;
    current.sp_delay_output = input.sp_read.delay_sram_out;
    current.sp_rev_output = input.sp_read.rev_delayer_out;
    current.sp_constant_value = input.sp_constant_value;

    current.acc_response_valid = acc_request_accepted;
    current.acc_response_is_constant = input.acc_read.is_constant;
    current.acc_constant_value = input.acc_constant_value;
    current.acc_write_valid = acc_request_accepted && input.acc_read.rmw;
    current.acc_write_addr = input.acc_read.addr;

    for(int port=0; port<fsa::nMemPorts; ++port){
        #pragma HLS UNROLL
        current.acc_dma_response_valid[port] =
            input.acc_dma_read_valid[port] &&
            output.acc_dma_read_ready[port];
    }
}
