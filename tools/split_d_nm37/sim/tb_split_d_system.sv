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
    reg [5:0] previous_cases = 0;
    always @(posedge ctrl_clk) begin
        if(!reset_n) previous_cases <= 0;
        else if(cases!=previous_cases) begin
            if(cases!=0) $display("SYSTEM PROGRESS mode=%b completed=%0d full_case_cycles=%0d",stress_enable,cases,last_cycles);
            previous_cases <= cases;
        end
    end
    split_d_system_wrapper dut(
        .sys_clk_100(clk),.reset_n(reset_n),.run_test(run_test),.stress_enable(stress_enable),
        .ctrl_clk(ctrl_clk),.clock_locked(clock_locked),.init_done(init_done),
        .test_busy(busy),.test_done(done),.test_pass(pass),.test_fail(fail),
        .fail_code(code),.cases_done(cases),.debug_pc(pc),.debug_state(state),
        .last_case_cycles(last_cycles),.actual_word(actual)
    );
`ifdef NM37_HBM_TLM_FUNCTIONAL
    // The official hbm_sc.h declares apb_complete as xsc_stub_port. TLM
    // does not model calibration; enable functional traffic after lock.
    // RTL/default runs never enable this adapter and must await real init.
    initial begin
        $display("HBM TLM FUNCTIONAL ONLY: APB initialization is unmodeled; readiness adapter enabled");
        force dut.split_d_system_i.init_done = clock_locked;
    end
`endif
    initial begin
        #1000 reset_n = 1;
        wait(clock_locked && init_done);
        repeat(20) @(posedge ctrl_clk);
        // Reset the entire clock/reset/AXI/HBM system during an active write.
        @(negedge ctrl_clk) run_test = 1;
        wait(state==5);
        @(negedge ctrl_clk) begin reset_n = 0; run_test = 0; end
        #1000 reset_n = 1;
        wait(clock_locked && init_done);
        repeat(20) @(posedge ctrl_clk);
        $display("SYSTEM RESET EXERCISE initialization restored");
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
        #10000;
        if(clock_locked!==1'b1 || dut.split_d_system_i.selftest_0.reset_n!==1'b1 ||
            dut.split_d_system_i.hbm_0.AXI_00_ARESET_N!==1'b1 ||
            dut.split_d_system_i.hbm_0.APB_0_PRESET_N!==1'b1 ||
            dut.split_d_system_i.hbm_0.APB_1_PRESET_N!==1'b1)
            $fatal(1,"SYSTEM RESET RELEASE FAIL at 10us");
        $display("SYSTEM CLOCK DIAGNOSTIC time=%t ctrl_clk=%b locked=%b init_done=%b reset_n=%b",$time,ctrl_clk,clock_locked,init_done,reset_n);
    end
    initial begin
        forever begin
            #50000;
            $display("SYSTEM HEARTBEAT time=%t locked=%b apb0=%b apb1=%b reset100=%b state=%0d pc=%0d cases=%0d",
                $time,clock_locked,dut.split_d_system_i.hbm_0.apb_complete_0,
                dut.split_d_system_i.hbm_0.apb_complete_1,
                dut.split_d_system_i.selftest_0.reset_n,state,pc,cases);
        end
    end
    initial begin
        #(64'd3000000000);
        $fatal(1,"SYSTEM TIMEOUT busy=%b done=%b cases=%0d pc=%0d state=%0d",busy,done,cases,pc,state);
    end
endmodule
