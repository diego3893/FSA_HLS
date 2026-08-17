`timescale 1ns / 1ps

module @SIMULATION_TOP@;

    reg clk = 1'b0;
    reg rst = 1'b1;
    reg run_test = 1'b0;

    always #5 clk = ~clk; // 100 MHz

    @TESTBENCH_SIGNAL_DECLARATIONS@

    @TEST_CONTROLLER_INSTANCE@

    @HLS_IP_INSTANCE@

    initial begin
        repeat (10) @(posedge clk);
        rst <= 1'b0;

        repeat (5) @(posedge clk);
        run_test <= 1'b1;
        @(posedge clk);
        run_test <= 1'b0;

        wait (test_done == 1'b1);
        @(posedge clk);

        if (test_pass && !test_fail)
            $display("[PASS] @MODULE_NAME@ IP board controller test");
        else
            $error("[FAIL] @MODULE_NAME@ IP board controller test");

        repeat (10) @(posedge clk);
        $finish;
    end

    initial begin
        #@TIMEOUT_NS@;
        $fatal(1, "[TIMEOUT] @MODULE_NAME@ IP board controller test");
    end

endmodule

