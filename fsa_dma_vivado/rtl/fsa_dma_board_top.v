`timescale 1ns / 1ps

module fsa_dma_board_top (
    input wire board_clk_p,
    input wire board_clk_n,
    input wire reset_n
);

    wire clk_100m;
    wire clk_locked;

    fsa_dma_clk_wiz_0 u_clk_wiz (
        .clk_in1_p (board_clk_p),
        .clk_in1_n (board_clk_n),
        .reset     (~reset_n),
        .clk_out1  (clk_100m),
        .locked    (clk_locked)
    );

    // External reset asserts the shift register asynchronously. Clock lock is
    // sampled only in this sequential process; no LUT-combined async reset.
    (* ASYNC_REG = "TRUE" *) reg [7:0] reset_shift = 8'b0;
    always @(posedge clk_100m or negedge reset_n) begin
        if (!reset_n)
            reset_shift <= 8'b0;
        else if (!clk_locked)
            reset_shift <= 8'b0;
        else
            reset_shift <= {reset_shift[6:0], 1'b1};
    end
    wire rst_100m = ~reset_shift[7];

    wire run_test;
    wire test_busy;
    wire test_done;
    wire test_pass;
    wire test_fail;
    wire [5:0] debug_state;
    wire [7:0] debug_fail_code;
    wire [7:0] debug_status;
    wire [5:0] debug_check_index;
    wire [31:0] debug_timeout;
    wire [6:0] debug_axil_awaddr;
    wire [5:0] debug_axil_write_flags;
    wire [6:0] debug_axil_araddr;
    wire [3:0] debug_axil_read_flags;
    wire [31:0] debug_axil_rdata;
    wire [63:0] debug_m_axi_araddr;
    wire [3:0] debug_m_axi_read_flags;
    wire [63:0] debug_m_axi_awaddr;
    wire [63:0] debug_m_axi_wdata;
    wire [5:0] debug_m_axi_write_flags;

    fsa_dma_control_system u_control_system (
        .clk                      (clk_100m),
        .rst                      (rst_100m),
        .run_test                 (run_test),
        .test_busy                (test_busy),
        .test_done                (test_done),
        .test_pass                (test_pass),
        .test_fail                (test_fail),
        .debug_state              (debug_state),
        .debug_fail_code          (debug_fail_code),
        .debug_status             (debug_status),
        .debug_check_index        (debug_check_index),
        .debug_timeout            (debug_timeout),
        .debug_axil_awaddr        (debug_axil_awaddr),
        .debug_axil_write_flags   (debug_axil_write_flags),
        .debug_axil_araddr        (debug_axil_araddr),
        .debug_axil_read_flags    (debug_axil_read_flags),
        .debug_axil_rdata         (debug_axil_rdata),
        .debug_m_axi_araddr       (debug_m_axi_araddr),
        .debug_m_axi_read_flags   (debug_m_axi_read_flags),
        .debug_m_axi_awaddr       (debug_m_axi_awaddr),
        .debug_m_axi_wdata        (debug_m_axi_wdata),
        .debug_m_axi_write_flags  (debug_m_axi_write_flags)
    );

    fsa_dma_vio_0 u_vio (
        .clk        (clk_100m),
        .probe_in0  (test_busy),
        .probe_in1  (test_done),
        .probe_in2  (test_pass),
        .probe_in3  (test_fail),
        .probe_in4  (debug_state),
        .probe_in5  (debug_fail_code),
        .probe_in6  (debug_status),
        .probe_in7  (debug_check_index),
        .probe_in8  (debug_timeout),
        .probe_out0 (run_test)
    );

    fsa_dma_ila_0 u_ila (
        .clk     (clk_100m),
        .probe0  (run_test),
        .probe1  (debug_state),
        .probe2  ({test_fail, test_pass, test_done, test_busy}),
        .probe3  (debug_axil_awaddr),
        .probe4  (debug_axil_write_flags),
        .probe5  (debug_axil_araddr),
        .probe6  (debug_axil_read_flags),
        .probe7  (debug_axil_rdata),
        .probe8  (debug_m_axi_araddr),
        .probe9  (debug_m_axi_read_flags),
        .probe10 (debug_m_axi_awaddr),
        .probe11 (debug_m_axi_wdata),
        .probe12 (debug_m_axi_write_flags),
        .probe13 (debug_fail_code),
        .probe14 (debug_check_index)
    );

endmodule

