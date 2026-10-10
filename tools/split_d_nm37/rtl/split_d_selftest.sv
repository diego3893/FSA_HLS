`timescale 1ns/1ps

// Finite AXI initiator: preload real HBM, program AXI-Lite, and compare the
// fixed official Vitis vectors. AW and W are accepted independently.
module split_d_selftest #(
    parameter ROM_FILE = "split_d_program.mem",
    parameter TIMEOUT_CYCLES = 32'd100000000
)(
    (* X_INTERFACE_INFO = "xilinx.com:signal:clock:1.0 clk CLK",
       X_INTERFACE_PARAMETER = "ASSOCIATED_BUSIF M_AXI:M_AXIL, ASSOCIATED_RESET reset_n, FREQ_HZ 100000000" *)
    input wire clk,
    (* X_INTERFACE_INFO = "xilinx.com:signal:reset:1.0 reset_n RST",
       X_INTERFACE_PARAMETER = "POLARITY ACTIVE_LOW" *)
    input wire reset_n,
    input wire run_test,
    input wire memory_ready,
    input wire stress_enable,

    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI AWADDR", X_INTERFACE_PARAMETER = "PROTOCOL AXI4, ADDR_WIDTH 32, DATA_WIDTH 256, FREQ_HZ 100000000" *)
    output wire [31:0] m_axi_awaddr,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI AWLEN" *)
    output wire [7:0] m_axi_awlen,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI AWSIZE" *)
    output wire [2:0] m_axi_awsize,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI AWBURST" *)
    output wire [1:0] m_axi_awburst,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI AWLOCK" *)
    output wire m_axi_awlock,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI AWCACHE" *)
    output wire [3:0] m_axi_awcache,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI AWPROT" *)
    output wire [2:0] m_axi_awprot,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI AWQOS" *)
    output wire [3:0] m_axi_awqos,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI AWVALID" *)
    output wire m_axi_awvalid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI AWREADY" *)
    input wire m_axi_awready,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI WDATA" *)
    output wire [255:0] m_axi_wdata,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI WSTRB" *)
    output wire [31:0] m_axi_wstrb,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI WLAST" *)
    output wire m_axi_wlast,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI WVALID" *)
    output wire m_axi_wvalid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI WREADY" *)
    input wire m_axi_wready,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI BRESP" *)
    input wire [1:0] m_axi_bresp,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI BVALID" *)
    input wire m_axi_bvalid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI BREADY" *)
    output wire m_axi_bready,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI ARADDR" *)
    output wire [31:0] m_axi_araddr,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI ARLEN" *)
    output wire [7:0] m_axi_arlen,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI ARSIZE" *)
    output wire [2:0] m_axi_arsize,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI ARBURST" *)
    output wire [1:0] m_axi_arburst,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI ARLOCK" *)
    output wire m_axi_arlock,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI ARCACHE" *)
    output wire [3:0] m_axi_arcache,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI ARPROT" *)
    output wire [2:0] m_axi_arprot,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI ARQOS" *)
    output wire [3:0] m_axi_arqos,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI ARVALID" *)
    output wire m_axi_arvalid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI ARREADY" *)
    input wire m_axi_arready,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI RDATA" *)
    input wire [255:0] m_axi_rdata,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI RRESP" *)
    input wire [1:0] m_axi_rresp,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI RLAST" *)
    input wire m_axi_rlast,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI RVALID" *)
    input wire m_axi_rvalid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI RREADY" *)
    output wire m_axi_rready,

    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXIL AWADDR", X_INTERFACE_PARAMETER = "PROTOCOL AXI4LITE, ADDR_WIDTH 32, DATA_WIDTH 32, FREQ_HZ 100000000" *)
    output wire [31:0] m_axil_awaddr,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXIL AWPROT" *)
    output wire [2:0] m_axil_awprot,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXIL AWVALID" *)
    output wire m_axil_awvalid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXIL AWREADY" *)
    input wire m_axil_awready,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXIL WDATA" *)
    output wire [31:0] m_axil_wdata,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXIL WSTRB" *)
    output wire [3:0] m_axil_wstrb,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXIL WVALID" *)
    output wire m_axil_wvalid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXIL WREADY" *)
    input wire m_axil_wready,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXIL BRESP" *)
    input wire [1:0] m_axil_bresp,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXIL BVALID" *)
    input wire m_axil_bvalid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXIL BREADY" *)
    output wire m_axil_bready,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXIL ARADDR" *)
    output wire [31:0] m_axil_araddr,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXIL ARPROT" *)
    output wire [2:0] m_axil_arprot,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXIL ARVALID" *)
    output wire m_axil_arvalid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXIL ARREADY" *)
    input wire m_axil_arready,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXIL RDATA" *)
    input wire [31:0] m_axil_rdata,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXIL RRESP" *)
    input wire [1:0] m_axil_rresp,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXIL RVALID" *)
    input wire m_axil_rvalid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXIL RREADY" *)
    output wire m_axil_rready,

    output reg test_busy,
    output reg test_done,
    output reg test_pass,
    output reg test_fail,
    output reg [7:0] fail_code,
    output reg [5:0] cases_done,
    output reg [31:0] last_case_cycles,
    output wire [12:0] debug_pc,
    output wire [3:0] debug_state,
    output reg [63:0] actual_word
);
    localparam IDLE=0, WAIT_MEMORY=1, FETCH=2, DECODE=3, MW=4, MB=5,
               MR=6, MD=7, CW=8, CB=9, CR=10, CD=11, NEXT=12, FINISH=13;
    (* rom_style = "block" *) reg [127:0] program_rom [0:8191];
    initial $readmemh(ROM_FILE, program_rom);
    reg [127:0] instruction;
    reg [12:0] pc;
    reg [3:0] state;
    reg [19:0] repeat_index;
    reg aw_seen, w_seen, run_d, timed_out;
    reg [31:0] timeout_count, case_cycles;
    reg [3:0] throttle;
    (* ASYNC_REG = "TRUE" *) reg [1:0] memory_sync;
    wire [3:0] opcode = instruction[127:124];
    wire [31:0] address = instruction[123:92];
    wire [63:0] data = instruction[91:28];
    wire [19:0] repeat_count = instruction[27:8];
    wire accept_response = !stress_enable || throttle[1:0]==2'b11;
    // ROM commands address 64-bit words. HBM requires aligned 256-bit
    // transfers; byte strobes preserve the other three words on writes.
    wire [31:0] word_address = address+(repeat_index<<3);
    wire [1:0] word_lane = word_address[4:3];
    wire [63:0] read_word = m_axi_rdata[word_lane*64 +: 64];

    assign debug_pc = pc;
    assign debug_state = state;
    assign m_axi_awaddr = {word_address[31:5],5'b0};
    assign m_axi_awlen = 0;
    assign m_axi_awsize = 5;
    assign m_axi_awburst = 1;
    assign m_axi_awlock = 0;
    assign m_axi_awcache = 0;
    assign m_axi_awprot = 0;
    assign m_axi_awqos = 0;
    assign m_axi_awvalid = state==MW && !aw_seen;
    assign m_axi_wdata = {4{data}};
    assign m_axi_wstrb = 32'hff<<(word_lane*8);
    assign m_axi_wlast = 1;
    assign m_axi_wvalid = state==MW && !w_seen;
    assign m_axi_bready = state==MB && accept_response;
    assign m_axi_araddr = {word_address[31:5],5'b0};
    assign m_axi_arlen = 0;
    assign m_axi_arsize = 5;
    assign m_axi_arburst = 1;
    assign m_axi_arlock = 0;
    assign m_axi_arcache = 0;
    assign m_axi_arprot = 0;
    assign m_axi_arqos = 0;
    assign m_axi_arvalid = state==MR;
    assign m_axi_rready = state==MD && accept_response;
    assign m_axil_awaddr = address;
    assign m_axil_awprot = 0;
    assign m_axil_awvalid = state==CW && !aw_seen;
    assign m_axil_wdata = data[31:0];
    assign m_axil_wstrb = 4'hf;
    assign m_axil_wvalid = state==CW && !w_seen;
    assign m_axil_bready = state==CB && accept_response;
    assign m_axil_araddr = address;
    assign m_axil_arprot = 0;
    assign m_axil_arvalid = state==CR;
    assign m_axil_rready = state==CD && accept_response;

    always @(posedge clk) begin
        if(!reset_n) begin
            state <= IDLE; pc <= 0; instruction <= 0; repeat_index <= 0;
            aw_seen <= 0; w_seen <= 0; run_d <= 0; memory_sync <= 0; timed_out <= 0;
            timeout_count <= 0; case_cycles <= 0; throttle <= 0;
            test_busy <= 0; test_done <= 0; test_pass <= 0; test_fail <= 0;
            fail_code <= 0; cases_done <= 0; last_case_cycles <= 0; actual_word <= 0;
        end else begin
            run_d <= run_test;
            memory_sync <= {memory_sync[0], memory_ready};
            throttle <= throttle+1;
            if(test_busy) begin
                timeout_count <= timeout_count+1;
                case_cycles <= case_cycles+1;
            end
            case(state)
                IDLE: if(run_test && !run_d) begin
                    pc <= 0; cases_done <= 0; case_cycles <= 0; timeout_count <= 0;
                    test_busy <= 1; test_done <= 0; test_pass <= 0; test_fail <= 0;
                    fail_code <= 0; timed_out <= 0; state <= WAIT_MEMORY;
                end
                WAIT_MEMORY: if(memory_sync[1]) begin state <= FETCH; timeout_count <= 0; end
                FETCH: begin instruction <= program_rom[pc]; state <= DECODE; end
                DECODE: begin
                    repeat_index <= 0; aw_seen <= 0; w_seen <= 0;
                    case(opcode)
                        1,6: state <= MW;
                        2: state <= CW;
                        3,7: state <= MR;
                        4,5: state <= CR;
                        8: begin
                            cases_done <= cases_done+1;
                            last_case_cycles <= case_cycles;
                            case_cycles <= 0; state <= NEXT;
                        end
                        15: begin
                            if(cases_done!=26) begin test_fail <= 1; fail_code <= 8'h06; end
                            state <= FINISH;
                        end
                        default: begin test_fail <= 1; fail_code <= 8'h07; state <= FINISH; end
                    endcase
                end
                MW: begin
                    if(m_axi_awready) aw_seen <= 1;
                    if(m_axi_wready) w_seen <= 1;
                    if((aw_seen || m_axi_awready) && (w_seen || m_axi_wready)) state <= MB;
                end
                MB: if(m_axi_bvalid && m_axi_bready) begin
                    if(m_axi_bresp!=0) begin test_fail <= 1; fail_code <= 8'h01; state <= FINISH; end
                    else if(!timed_out && repeat_index+1 < repeat_count) begin
                        repeat_index <= repeat_index+1; aw_seen <= 0; w_seen <= 0;
                        timeout_count <= 0; state <= MW;
                    end else state <= NEXT;
                end
                MR: if(m_axi_arready) state <= MD;
                MD: if(m_axi_rvalid && m_axi_rready) begin
                    actual_word <= read_word;
                    if(m_axi_rresp!=0 || !m_axi_rlast || read_word!==data) begin
                        test_fail <= 1; fail_code <= 8'h02; state <= FINISH;
                    end else if(!timed_out && repeat_index+1 < repeat_count) begin
                        repeat_index <= repeat_index+1; timeout_count <= 0; state <= MR;
                    end else state <= NEXT;
                end
                CW: begin
                    if(m_axil_awready) aw_seen <= 1;
                    if(m_axil_wready) w_seen <= 1;
                    if((aw_seen || m_axil_awready) && (w_seen || m_axil_wready)) state <= CB;
                end
                CB: if(m_axil_bvalid && m_axil_bready) begin
                    if(m_axil_bresp!=0) begin test_fail <= 1; fail_code <= 8'h03; state <= FINISH; end
                    else state <= NEXT;
                end
                CR: if(m_axil_arready) state <= CD;
                CD: if(m_axil_rvalid && m_axil_rready) begin
                    actual_word <= {32'b0,m_axil_rdata};
                    if(m_axil_rresp!=0) begin test_fail <= 1; fail_code <= 8'h04; state <= FINISH; end
                    else if(timed_out) state <= FINISH;
                    else if((m_axil_rdata & data[63:32])!==data[31:0]) begin
                        if(opcode==5) state <= CR;
                        else begin test_fail <= 1; fail_code <= 8'h05; state <= FINISH; end
                    end else state <= NEXT;
                end
                NEXT: begin
                    timeout_count <= 0;
                    if(timed_out) state <= FINISH;
                    else begin pc <= pc+1; state <= FETCH; end
                end
                FINISH: begin
                    test_busy <= 0; test_done <= 1; test_pass <= !test_fail; state <= IDLE;
                end
                default: begin test_fail <= 1; fail_code <= 8'h08; state <= FINISH; end
            endcase
            if(test_busy && state!=FINISH && timeout_count>=TIMEOUT_CYCLES) begin
                // Report failure immediately, but preserve any pending AXI
                // VALID/payload until the peer accepts it. A timeout is never
                // permission to cancel an outstanding channel. Reset the
                // complete system if the peer cannot drain the transaction.
                test_fail <= 1; fail_code <= 8'hff; timed_out <= 1;
                test_done <= 1; test_busy <= 0; test_pass <= 0;
                if(state==WAIT_MEMORY || state==FETCH || state==DECODE || state==NEXT) state <= FINISH;
            end
        end
    end
endmodule
