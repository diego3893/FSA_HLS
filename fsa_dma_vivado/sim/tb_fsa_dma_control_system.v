`timescale 1ns / 1ps

module tb_fsa_dma_control_system;

    reg clk = 1'b0;
    reg rst = 1'b1;
    reg run_test = 1'b0;

    wire test_busy;
    wire test_done;
    wire test_pass;
    wire test_fail;
    wire [5:0] debug_state;
    wire [7:0] debug_fail_code;
    wire [7:0] debug_status;
    wire [5:0] debug_check_index;
    wire [31:0] debug_timeout;

    always #5 clk = ~clk;

    fsa_dma_control_system dut (
        .clk                     (clk),
        .rst                     (rst),
        .run_test                (run_test),
        .test_busy               (test_busy),
        .test_done               (test_done),
        .test_pass               (test_pass),
        .test_fail               (test_fail),
        .debug_state             (debug_state),
        .debug_fail_code         (debug_fail_code),
        .debug_status            (debug_status),
        .debug_check_index       (debug_check_index),
        .debug_timeout           (debug_timeout),
        .debug_axil_awaddr       (),
        .debug_axil_write_flags  (),
        .debug_axil_araddr       (),
        .debug_axil_read_flags   (),
        .debug_axil_rdata        (),
        .debug_m_axi_araddr      (),
        .debug_m_axi_read_flags  (),
        .debug_m_axi_awaddr      (),
        .debug_m_axi_wdata       (),
        .debug_m_axi_write_flags ()
    );

    initial begin
        repeat (20) @(posedge clk);
        rst <= 1'b0;
        repeat (10) @(posedge clk);

        run_test <= 1'b1;
        @(posedge clk);
        run_test <= 1'b0;

        wait (test_done == 1'b1);
        @(posedge clk);

        if (test_pass && !test_fail) begin
            $display("[PASS] fsa_dma_top AXI fixed-vector board-control test");
        end else begin
            $error("[FAIL] fsa_dma_top test: code=0x%02x state=%0d status=%0d check=%0d",
                debug_fail_code, debug_state, debug_status,
                debug_check_index);
        end

        repeat (10) @(posedge clk);
        $finish;
    end

    initial begin
        #4000000;
        $fatal(1,
            "[TIMEOUT] state=%0d code=0x%02x poll_count=%0d",
            debug_state, debug_fail_code, debug_timeout);
    end

endmodule

