`timescale 1ns/1ps
// Exercise the same generated HBM/SmartConnect/FSA/selftest BD as hardware.
// The simulation bypasses only package IBUFDS and JTAG VIO/ILA.
module tb_split_d_system;
    reg clk = 0;
    always #5 clk = !clk;
    reg reset_n = 0, run_test = 0, stress_enable = 0;
    wire ctrl_clk, clock_locked, init_done, busy, done, pass, fail;
    wire [7:0] code;
    wire [5:0] cases;
    wire [12:0] pc;
    wire [3:0] state;
    wire [31:0] last_cycles;
    wire [63:0] actual;
    split_d_system_wrapper dut(
        .sys_clk_100(clk),.reset_n(reset_n),.run_test(run_test),.stress_enable(stress_enable),
        .ctrl_clk(ctrl_clk),.clock_locked(clock_locked),.init_done(init_done),
        .test_busy(busy),.test_done(done),.test_pass(pass),.test_fail(fail),
        .fail_code(code),.cases_done(cases),.debug_pc(pc),.debug_state(state),
        .last_case_cycles(last_cycles),.actual_word(actual)
    );
    initial begin
        #1000 reset_n = 1;
        wait(clock_locked && init_done);
        repeat(20) @(posedge ctrl_clk);
        @(negedge ctrl_clk) run_test = 1;
        wait(done);
        if(!pass || fail || cases!=26) $fatal(1,"SYSTEM FAIL mode0 cases=%0d code=%h pc=%0d actual=%h",cases,code,pc,actual);
        $display("SYSTEM PASS mode0 cases=%0d",cases);
        @(negedge ctrl_clk) run_test = 0;
        repeat(20) @(posedge ctrl_clk);
        @(negedge ctrl_clk) begin stress_enable = 1; run_test = 1; end
        wait(busy);
        wait(done);
        if(!pass || fail || cases!=26) $fatal(1,"SYSTEM FAIL mode1 cases=%0d code=%h pc=%0d actual=%h",cases,code,pc,actual);
        $display("SYSTEM PASS mode1 cases=%0d",cases);
        $finish;
    end
    initial begin
        #3000000000;
        $fatal(1,"SYSTEM TIMEOUT busy=%b done=%b cases=%0d pc=%0d state=%0d",busy,done,cases,pc,state);
    end
endmodule
