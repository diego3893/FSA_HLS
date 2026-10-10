`timescale 1ns/1ps
module split_d_nm37_top(
    input wire board_clk_p,
    input wire board_clk_n,
    input wire reset_n
);
    wire sys_clk_100, ctrl_clk, clock_locked, init_done;
    wire run_test, stress_enable;
    wire test_busy, test_done, test_pass, test_fail;
    wire [7:0] fail_code;
    wire [5:0] cases_done;
    wire [12:0] debug_pc;
    wire [3:0] debug_state;
    wire [31:0] last_case_cycles;
    wire [63:0] actual_word;
    wire [3:0] flags = {test_fail,test_pass,test_done,test_busy};
    IBUFDS u_board_clk(.I(board_clk_p),.IB(board_clk_n),.O(sys_clk_100));
    split_d_system_wrapper u_bd(
        .sys_clk_100(sys_clk_100),.reset_n(reset_n),
        .ctrl_clk(ctrl_clk),.clock_locked(clock_locked),.init_done(init_done),
        .run_test(run_test),.stress_enable(stress_enable),
        .test_busy(test_busy),.test_done(test_done),.test_pass(test_pass),.test_fail(test_fail),
        .fail_code(fail_code),.cases_done(cases_done),.debug_pc(debug_pc),.debug_state(debug_state),
        .last_case_cycles(last_case_cycles),.actual_word(actual_word)
    );
    split_d_vio u_vio(
        .clk(ctrl_clk),.probe_out0(run_test),.probe_out1(stress_enable),
        .probe_in0(flags),.probe_in1(fail_code),.probe_in2(cases_done),.probe_in3(debug_pc),
        .probe_in4(last_case_cycles),.probe_in5(debug_state),.probe_in6(actual_word),.probe_in7(init_done)
    );
    split_d_ila u_ila(
        .clk(ctrl_clk),.probe0(flags),.probe1(fail_code),.probe2(cases_done),.probe3(debug_pc),
        .probe4(last_case_cycles),.probe5(debug_state),.probe6(actual_word),.probe7(init_done)
    );
endmodule
