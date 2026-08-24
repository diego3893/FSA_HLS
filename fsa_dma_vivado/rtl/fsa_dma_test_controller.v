`timescale 1ns / 1ps

// Pure-hardware fixed-vector test controller for the AXI interfaces of the
// exported fsa_dma_top IP. VIO only supplies run_test; this module preloads a
// small AXI RAM, programs the HLS AXI4-Lite register bank, polls completion,
// and checks the complete 9x4 FP32 O matrix plus two canary words.
module fsa_dma_test_controller #(
    parameter POLL_TIMEOUT_CYCLES = 32'd1000000
) (
    input  wire         clk,
    input  wire         rst,
    input  wire         run_test,

    output reg  [6:0]   m_axil_awaddr,
    output reg          m_axil_awvalid,
    input  wire         m_axil_awready,
    output reg  [31:0]  m_axil_wdata,
    output reg  [3:0]   m_axil_wstrb,
    output reg          m_axil_wvalid,
    input  wire         m_axil_wready,
    input  wire [1:0]   m_axil_bresp,
    input  wire         m_axil_bvalid,
    output reg          m_axil_bready,

    output reg  [6:0]   m_axil_araddr,
    output reg          m_axil_arvalid,
    input  wire         m_axil_arready,
    input  wire [31:0]  m_axil_rdata,
    input  wire [1:0]   m_axil_rresp,
    input  wire         m_axil_rvalid,
    output reg          m_axil_rready,

    output reg          ram_dbg_we,
    output reg  [8:0]   ram_dbg_addr,
    output reg  [63:0]  ram_dbg_wdata,
    input  wire [63:0]  ram_dbg_rdata,
    input  wire         ram_protocol_error,

    output reg          test_busy,
    output reg          test_done,
    output reg          test_pass,
    output reg          test_fail,
    output reg  [7:0]   fail_code,
    output wire [5:0]   debug_state,
    output reg  [5:0]   debug_check_index,
    output reg  [7:0]   debug_status,
    output reg  [31:0]  debug_timeout
);

    localparam [8:0] Q_BASE_WORD = 9'd0;
    localparam [8:0] K_BASE_WORD = 9'd32;  // byte address 0x100
    localparam [8:0] V_BASE_WORD = 9'd64;  // byte address 0x200
    localparam [8:0] O_BASE_WORD = 9'd96;  // byte address 0x300
    localparam [63:0] CANARY = 64'h5A5AA5A55A5AA5A5;

    localparam [5:0] ST_IDLE              = 6'd0;
    localparam [5:0] ST_INIT_RAM          = 6'd1;
    localparam [5:0] ST_CFG_ISSUE         = 6'd2;
    localparam [5:0] ST_CFG_WAIT          = 6'd3;
    localparam [5:0] ST_POLL_ISSUE        = 6'd4;
    localparam [5:0] ST_POLL_WAIT         = 6'd5;
    localparam [5:0] ST_VALID_ISSUE       = 6'd6;
    localparam [5:0] ST_VALID_WAIT        = 6'd7;
    localparam [5:0] ST_STATUS_ISSUE      = 6'd8;
    localparam [5:0] ST_STATUS_WAIT       = 6'd9;
    localparam [5:0] ST_CHECK_SET         = 6'd10;
    localparam [5:0] ST_CHECK_EVAL        = 6'd11;
    localparam [5:0] ST_FINISH            = 6'd12;

    reg [5:0] state;
    reg [5:0] init_index;
    reg [3:0] cfg_index;
    reg run_test_d;

    reg axil_write_start;
    reg axil_write_busy;
    reg axil_write_done;
    reg [1:0] axil_write_resp;

    reg axil_read_start;
    reg axil_read_busy;
    reg axil_read_done;
    reg [31:0] axil_read_data;
    reg [1:0] axil_read_resp;

    wire run_test_rise = run_test && !run_test_d;
    assign debug_state = state;

    function [8:0] init_word_address;
        input [5:0] index;
        begin
            if (index < 9)
                init_word_address = Q_BASE_WORD + index;
            else if (index < 18)
                init_word_address = K_BASE_WORD + index - 9;
            else if (index < 27)
                init_word_address = V_BASE_WORD + index - 18;
            else
                init_word_address = O_BASE_WORD + index - 27;
        end
    endfunction

    function [63:0] init_word_data;
        input [5:0] index;
        begin
            case (index)
                // Q, FP16 lane 0 in bits 15:0.
                6'd0:  init_word_data = 64'hB40038000000B800;
                6'd1:  init_word_data = 64'h0000B8003400B400;
                6'd2:  init_word_data = 64'h3400B40038000000;
                6'd3:  init_word_data = 64'h38000000B8003400;
                6'd4:  init_word_data = 64'hB8003400B4003800;
                6'd5:  init_word_data = 64'hB40038000000B800;
                6'd6:  init_word_data = 64'h0000B8003400B400;
                6'd7:  init_word_data = 64'h3400B40038000000;
                6'd8:  init_word_data = 64'h38000000B8003400;

                // K.
                6'd9:  init_word_data = 64'h0000B400B800BA00;
                6'd10: init_word_data = 64'h380034000000B400;
                6'd11: init_word_data = 64'hBA003A0038003400;
                6'd12: init_word_data = 64'hB400B800BA003A00;
                6'd13: init_word_data = 64'h34000000B400B800;
                6'd14: init_word_data = 64'h3A00380034000000;
                6'd15: init_word_data = 64'hB800BA003A003800;
                6'd16: init_word_data = 64'h0000B400B800BA00;
                6'd17: init_word_data = 64'h380034000000B400;

                // V.
                6'd18: init_word_data = 64'h38000000B800BC00;
                6'd19: init_word_data = 64'h3C0038000000B800;
                6'd20: init_word_data = 64'h3E003C0038000000;
                6'd21: init_word_data = 64'hBC003E003C003800;
                6'd22: init_word_data = 64'hB800BC003E003C00;
                6'd23: init_word_data = 64'h0000B800BC003E00;
                6'd24: init_word_data = 64'h38000000B800BC00;
                6'd25: init_word_data = 64'h3C0038000000B800;
                6'd26: init_word_data = 64'h3E003C0038000000;
                default: init_word_data = CANARY;
            endcase
        end
    endfunction

    function [6:0] config_address;
        input [3:0] index;
        begin
            case (index)
                4'd0:  config_address = 7'h10; // q low
                4'd1:  config_address = 7'h14; // q high
                4'd2:  config_address = 7'h1C; // k low
                4'd3:  config_address = 7'h20; // k high
                4'd4:  config_address = 7'h28; // v low
                4'd5:  config_address = 7'h2C; // v high
                4'd6:  config_address = 7'h34; // o low
                4'd7:  config_address = 7'h38; // o high
                4'd8:  config_address = 7'h40; // sequence_length
                4'd9:  config_address = 7'h48; // causal
                default: config_address = 7'h00; // ap_start
            endcase
        end
    endfunction

    function [31:0] config_data;
        input [3:0] index;
        begin
            case (index)
                4'd0: config_data = 32'h00000000;
                4'd1: config_data = 32'h00000000;
                4'd2: config_data = 32'h00000100;
                4'd3: config_data = 32'h00000000;
                4'd4: config_data = 32'h00000200;
                4'd5: config_data = 32'h00000000;
                4'd6: config_data = 32'h00000300;
                4'd7: config_data = 32'h00000000;
                4'd8: config_data = 32'd9;
                4'd9: config_data = 32'd0;
                default: config_data = 32'h00000001;
            endcase
        end
    endfunction

    // Bounds are the independent CPU golden value +/- 0.18, matching the
    // current C++ testbench tolerance. Values are IEEE-754 binary32 bits.
    function [31:0] golden_low;
        input [5:0] index;
        begin
            case (index)
                0: golden_low=32'hBE36F051; 1: golden_low=32'hBC39E9D7;
                2: golden_low=32'h3E09BA3F; 3: golden_low=32'h3EC83D74;
                4: golden_low=32'hBE718816; 5: golden_low=32'hBD116185;
                6: golden_low=32'h3DFFFA8C; 7: golden_low=32'h3EA1B7DC;
                8: golden_low=32'hBE41A384; 9: golden_low=32'hBD6387CA;
                10: golden_low=32'h3DF7EE81; 11: golden_low=32'h3EABDCE1;
                12: golden_low=32'hBDFF58BE; 13: golden_low=32'h3BED872E;
                14: golden_low=32'h3E255AB2; 15: golden_low=32'h3E83B402;
                16: golden_low=32'hBE355C2B; 17: golden_low=32'h3D066A76;
                18: golden_low=32'h3E789166; 19: golden_low=32'h3E96413B;
                20: golden_low=32'hBE36F051; 21: golden_low=32'hBC39E9D7;
                22: golden_low=32'h3E09BA3F; 23: golden_low=32'h3EC83D74;
                24: golden_low=32'hBE718816; 25: golden_low=32'hBD116185;
                26: golden_low=32'h3DFFFA8C; 27: golden_low=32'h3EA1B7DC;
                28: golden_low=32'hBE41A384; 29: golden_low=32'hBD6387CA;
                30: golden_low=32'h3DF7EE81; 31: golden_low=32'h3EABDCE1;
                32: golden_low=32'hBDFF58BE; 33: golden_low=32'h3BED872E;
                34: golden_low=32'h3E255AB2; default: golden_low=32'h3E83B402;
            endcase
        end
    endfunction

    function [31:0] golden_high;
        input [5:0] index;
        begin
            case (index)
                0: golden_high=32'h3E39B386; 1: golden_high=32'h3EB2829D;
                2: golden_high=32'h3EFD2F0B; 3: golden_high=32'h3F4047B0;
                4: golden_high=32'h3DFE3783; 5: golden_high=32'h3EA625BB;
                6: golden_high=32'h3EF8508F; 7: golden_high=32'h3F2D04E4;
                8: golden_high=32'h3E2F0053; 9: golden_high=32'h3E9BE0F2;
                10: golden_high=32'h3EF64D8C; 11: golden_high=32'h3F321766;
                12: golden_high=32'h3E70F778; 13: golden_high=32'h3EBC0808;
                14: golden_high=32'h3F057FA2; 15: golden_high=32'h3F1E02F7;
                16: golden_high=32'h3E3B47AC; 17: golden_high=32'h3EC91F3A;
                18: golden_high=32'h3F1A4D4F; 19: golden_high=32'h3F274993;
                20: golden_high=32'h3E39B386; 21: golden_high=32'h3EB2829D;
                22: golden_high=32'h3EFD2F0B; 23: golden_high=32'h3F4047B0;
                24: golden_high=32'h3DFE3783; 25: golden_high=32'h3EA625BB;
                26: golden_high=32'h3EF8508F; 27: golden_high=32'h3F2D04E4;
                28: golden_high=32'h3E2F0053; 29: golden_high=32'h3E9BE0F2;
                30: golden_high=32'h3EF64D8C; 31: golden_high=32'h3F321766;
                32: golden_high=32'h3E70F778; 33: golden_high=32'h3EBC0808;
                34: golden_high=32'h3F057FA2; default: golden_high=32'h3F1E02F7;
            endcase
        end
    endfunction

    function [31:0] float_order_key;
        input [31:0] bits;
        begin
            float_order_key = bits[31] ? ~bits : (bits ^ 32'h80000000);
        end
    endfunction

    function float_in_range;
        input [31:0] actual;
        input [31:0] low;
        input [31:0] high;
        reg [31:0] actual_key;
        reg [31:0] low_key;
        reg [31:0] high_key;
        begin
            actual_key = float_order_key(actual);
            low_key = float_order_key(low);
            high_key = float_order_key(high);
            float_in_range = (actual[30:23] != 8'hFF) &&
                (actual_key >= low_key) && (actual_key <= high_key);
        end
    endfunction

    wire [5:0] low_element_index = {debug_check_index[4:0], 1'b0};
    wire [5:0] high_element_index = low_element_index + 1'b1;
    wire low_lane_ok = float_in_range(
        ram_dbg_rdata[31:0],
        golden_low(low_element_index),
        golden_high(low_element_index)
    );
    wire high_lane_ok = float_in_range(
        ram_dbg_rdata[63:32],
        golden_low(high_element_index),
        golden_high(high_element_index)
    );

    // AXI4-Lite single-transaction write engine.
    always @(posedge clk) begin
        if (rst) begin
            m_axil_awvalid  <= 1'b0;
            m_axil_wvalid   <= 1'b0;
            m_axil_bready   <= 1'b0;
            axil_write_busy <= 1'b0;
            axil_write_done <= 1'b0;
            axil_write_resp <= 2'b00;
        end else begin
            axil_write_done <= 1'b0;
            if (axil_write_start && !axil_write_busy) begin
                axil_write_busy <= 1'b1;
                m_axil_awvalid <= 1'b1;
                m_axil_wvalid <= 1'b1;
                m_axil_bready <= 1'b0;
            end else if (axil_write_busy) begin
                if (m_axil_awvalid && m_axil_awready)
                    m_axil_awvalid <= 1'b0;
                if (m_axil_wvalid && m_axil_wready)
                    m_axil_wvalid <= 1'b0;

                if ((m_axil_awvalid ? m_axil_awready : 1'b1) &&
                    (m_axil_wvalid ? m_axil_wready : 1'b1)) begin
                    m_axil_bready <= 1'b1;
                end

                if (m_axil_bready && m_axil_bvalid) begin
                    axil_write_resp <= m_axil_bresp;
                    axil_write_busy <= 1'b0;
                    axil_write_done <= 1'b1;
                    m_axil_bready <= 1'b0;
                end
            end
        end
    end

    // AXI4-Lite single-transaction read engine.
    always @(posedge clk) begin
        if (rst) begin
            m_axil_arvalid  <= 1'b0;
            m_axil_rready   <= 1'b0;
            axil_read_busy  <= 1'b0;
            axil_read_done  <= 1'b0;
            axil_read_data  <= 32'd0;
            axil_read_resp  <= 2'b00;
        end else begin
            axil_read_done <= 1'b0;
            if (axil_read_start && !axil_read_busy) begin
                axil_read_busy <= 1'b1;
                m_axil_arvalid <= 1'b1;
                m_axil_rready <= 1'b0;
            end else if (axil_read_busy) begin
                if (m_axil_arvalid && m_axil_arready) begin
                    m_axil_arvalid <= 1'b0;
                    m_axil_rready <= 1'b1;
                end
                if (m_axil_rready && m_axil_rvalid) begin
                    axil_read_data <= m_axil_rdata;
                    axil_read_resp <= m_axil_rresp;
                    axil_read_busy <= 1'b0;
                    axil_read_done <= 1'b1;
                    m_axil_rready <= 1'b0;
                end
            end
        end
    end

    // High-level fixed-vector sequence.
    always @(posedge clk) begin
        if (rst) begin
            state <= ST_IDLE;
            init_index <= 6'd0;
            cfg_index <= 4'd0;
            run_test_d <= 1'b0;
            axil_write_start <= 1'b0;
            axil_read_start <= 1'b0;
            m_axil_awaddr <= 7'd0;
            m_axil_wdata <= 32'd0;
            m_axil_wstrb <= 4'hF;
            m_axil_araddr <= 7'd0;
            ram_dbg_we <= 1'b0;
            ram_dbg_addr <= 9'd0;
            ram_dbg_wdata <= 64'd0;
            test_busy <= 1'b0;
            test_done <= 1'b0;
            test_pass <= 1'b0;
            test_fail <= 1'b0;
            fail_code <= 8'd0;
            debug_check_index <= 6'd0;
            debug_status <= 8'hFF;
            debug_timeout <= 32'd0;
        end else begin
            run_test_d <= run_test;
            axil_write_start <= 1'b0;
            axil_read_start <= 1'b0;
            ram_dbg_we <= 1'b0;

            case (state)
                ST_IDLE: begin
                    if (run_test_rise) begin
                        init_index <= 6'd0;
                        cfg_index <= 4'd0;
                        test_busy <= 1'b1;
                        test_done <= 1'b0;
                        test_pass <= 1'b0;
                        test_fail <= 1'b0;
                        fail_code <= 8'd0;
                        debug_status <= 8'hFF;
                        debug_timeout <= 32'd0;
                        state <= ST_INIT_RAM;
                    end
                end

                ST_INIT_RAM: begin
                    ram_dbg_we <= 1'b1;
                    ram_dbg_addr <= init_word_address(init_index);
                    ram_dbg_wdata <= init_word_data(init_index);
                    if (init_index == 6'd46) begin
                        cfg_index <= 4'd0;
                        state <= ST_CFG_ISSUE;
                    end else begin
                        init_index <= init_index + 1'b1;
                    end
                end

                ST_CFG_ISSUE: begin
                    if (!axil_write_busy) begin
                        m_axil_awaddr <= config_address(cfg_index);
                        m_axil_wdata <= config_data(cfg_index);
                        m_axil_wstrb <= 4'hF;
                        axil_write_start <= 1'b1;
                        state <= ST_CFG_WAIT;
                    end
                end

                ST_CFG_WAIT: begin
                    if (axil_write_done) begin
                        if (axil_write_resp != 2'b00) begin
                            test_fail <= 1'b1;
                            if (!test_fail) fail_code <= 8'h01;
                            state <= ST_FINISH;
                        end else if (cfg_index == 4'd10) begin
                            state <= ST_POLL_ISSUE;
                        end else begin
                            cfg_index <= cfg_index + 1'b1;
                            state <= ST_CFG_ISSUE;
                        end
                    end
                end

                ST_POLL_ISSUE: begin
                    if (!axil_read_busy) begin
                        m_axil_araddr <= 7'h00;
                        axil_read_start <= 1'b1;
                        state <= ST_POLL_WAIT;
                    end
                end

                ST_POLL_WAIT: begin
                    if (axil_read_done) begin
                        if (axil_read_resp != 2'b00) begin
                            test_fail <= 1'b1;
                            if (!test_fail) fail_code <= 8'h02;
                            state <= ST_FINISH;
                        end else if (axil_read_data[1]) begin
                            state <= ST_VALID_ISSUE;
                        end else if (debug_timeout >= POLL_TIMEOUT_CYCLES-1) begin
                            test_fail <= 1'b1;
                            if (!test_fail) fail_code <= 8'h03;
                            state <= ST_FINISH;
                        end else begin
                            debug_timeout <= debug_timeout + 1'b1;
                            state <= ST_POLL_ISSUE;
                        end
                    end
                end

                ST_VALID_ISSUE: begin
                    if (!axil_read_busy) begin
                        m_axil_araddr <= 7'h54;
                        axil_read_start <= 1'b1;
                        state <= ST_VALID_WAIT;
                    end
                end

                ST_VALID_WAIT: begin
                    if (axil_read_done) begin
                        if (axil_read_resp != 2'b00 || !axil_read_data[0]) begin
                            test_fail <= 1'b1;
                            if (!test_fail) fail_code <= 8'h04;
                            state <= ST_FINISH;
                        end else begin
                            state <= ST_STATUS_ISSUE;
                        end
                    end
                end

                ST_STATUS_ISSUE: begin
                    if (!axil_read_busy) begin
                        m_axil_araddr <= 7'h50;
                        axil_read_start <= 1'b1;
                        state <= ST_STATUS_WAIT;
                    end
                end

                ST_STATUS_WAIT: begin
                    if (axil_read_done) begin
                        debug_status <= axil_read_data[7:0];
                        if (axil_read_resp != 2'b00 || axil_read_data[7:0] != 0) begin
                            test_fail <= 1'b1;
                            if (!test_fail) fail_code <= 8'h05;
                            state <= ST_FINISH;
                        end else begin
                            debug_check_index <= 6'd0;
                            state <= ST_CHECK_SET;
                        end
                    end
                end

                ST_CHECK_SET: begin
                    ram_dbg_addr <= O_BASE_WORD + debug_check_index;
                    state <= ST_CHECK_EVAL;
                end

                ST_CHECK_EVAL: begin
                    if (debug_check_index < 6'd18) begin
                        if (!low_lane_ok || !high_lane_ok) begin
                            test_fail <= 1'b1;
                            if (!test_fail) fail_code <= 8'h10 + debug_check_index;
                        end
                    end else if (ram_dbg_rdata != CANARY) begin
                        test_fail <= 1'b1;
                        if (!test_fail) fail_code <= 8'h30 + debug_check_index;
                    end

                    if (debug_check_index == 6'd19) begin
                        state <= ST_FINISH;
                    end else begin
                        debug_check_index <= debug_check_index + 1'b1;
                        state <= ST_CHECK_SET;
                    end
                end

                ST_FINISH: begin
                    test_busy <= 1'b0;
                    test_done <= 1'b1;
                    test_fail <= test_fail | ram_protocol_error;
                    test_pass <= ~(test_fail | ram_protocol_error);
                    if (ram_protocol_error && !test_fail)
                        fail_code <= 8'h40;
                    if (run_test_rise) begin
                        init_index <= 6'd0;
                        cfg_index <= 4'd0;
                        test_busy <= 1'b1;
                        test_done <= 1'b0;
                        test_pass <= 1'b0;
                        test_fail <= 1'b0;
                        fail_code <= 8'd0;
                        debug_status <= 8'hFF;
                        debug_timeout <= 32'd0;
                        state <= ST_INIT_RAM;
                    end
                end

                default: state <= ST_IDLE;
            endcase
        end
    end

endmodule
