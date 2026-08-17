`timescale 1ns/1ps

/**
 * @brief 仅用于包装层单元测试的15拍HLS行为桩。
 *
 * MOD: 真正的15拍仍必须由Vitis生成的accumulator_top替换本桩后再验证；
 * 本桩只检查busy/result_valid映射和持续valid的one-shot保护。
 */
module accumulator_top (
    input  logic         ap_clk,
    input  logic         ap_rst,
    input  logic         ap_start,
    output logic         ap_done,
    output logic         ap_idle,
    output logic         ap_ready,
    input  logic [265:0] input_r,
    output logic [127:0] sram_out,
    output logic         sram_write_valid,
    output logic         reciprocal_result
);
    logic [4:0] cycle_count;

    always_ff @(posedge ap_clk) begin
        if (ap_rst) begin
            ap_done <= 1'b0;
            ap_idle <= 1'b1;
            ap_ready <= 1'b1;
            sram_out <= '0;
            sram_write_valid <= 1'b0;
            reciprocal_result <= 1'b0;
            cycle_count <= '0;
        end else begin
            ap_done <= 1'b0;
            if (ap_idle && ap_start) begin
                ap_idle <= 1'b0;
                ap_ready <= 1'b0;
                reciprocal_result <= 1'b0;
                cycle_count <= 5'd1;
            end else if (!ap_idle) begin
                if (cycle_count == 5'd14) begin
                    // 接受拍计为第1拍，本拍为第15拍。
                    ap_done <= 1'b1;
                    ap_idle <= 1'b1;
                    ap_ready <= 1'b1;
                    reciprocal_result <= 1'b1;
                    sram_out <= 128'h3f800000_3f000000_3eaaaaab_3e800000;
                    cycle_count <= '0;
                end else begin
                    cycle_count <= cycle_count + 1'b1;
                end
            end
        end
    end

    // 防止严格lint把行为桩输入报告成未使用。
    logic unused_input;
    assign unused_input = ^input_r;
endmodule

module test_accumulator_protocol_wrapper;
    logic clk = 1'b0;
    logic rst = 1'b1;
    logic request_valid = 1'b0;
    logic request_is_reciprocal = 1'b1;
    logic request_ready;
    logic [265:0] input_r = '0;
    logic [127:0] sram_out;
    logic busy;
    logic sram_write_valid;
    logic result_valid;

    always #5 clk = ~clk;

    accumulator_protocol_wrapper dut (
        .ap_clk(clk),
        .ap_rst(rst),
        .request_valid(request_valid),
        .request_is_reciprocal(request_is_reciprocal),
        .request_ready(request_ready),
        .input_r(input_r),
        .sram_out(sram_out),
        .busy(busy),
        .sram_write_valid(sram_write_valid),
        .result_valid(result_valid)
    );

    task automatic check_condition(input logic condition, input string message);
        if (!condition) begin
            $error("[FAIL] %s", message);
            $fatal(1);
        end
    endtask

    initial begin
        repeat (2) @(posedge clk);
        rst = 1'b0;
        request_valid = 1'b1;

        // 接受拍为第1拍。
        @(posedge clk); #1;
        check_condition(busy && !result_valid, "cycle 1 busy/result_valid");

        for (int cycle = 2; cycle <= 14; ++cycle) begin
            @(posedge clk); #1;
            check_condition(busy && !result_valid, "cycles 2..14 busy/result_valid");
        end

        @(posedge clk); #1;
        check_condition(!busy && result_valid, "cycle 15 result_valid");
        check_condition(!request_ready, "held reciprocal request must not re-handshake");

        // 持续valid不会在完成拍或后续空闲拍重启第二笔倒数。
        repeat (4) begin
            @(posedge clk); #1;
            check_condition(!busy && !result_valid, "one-shot blocks repeated reciprocal");
        end

        // 拉低一拍后重新arm。
        request_valid = 1'b0;
        @(posedge clk); #1;
        check_condition(request_ready, "request rearmed after valid low");

        // 第二笔事务运行中复位，不能产生迟到result_valid。
        request_valid = 1'b1;
        @(posedge clk); #1;
        check_condition(busy, "second reciprocal accepted");
        repeat (2) @(posedge clk);
        rst = 1'b1;
        @(posedge clk); #1;
        check_condition(!busy && !result_valid, "reset cancels busy reciprocal");

        $display("[PASS] test_accumulator_protocol_wrapper");
        $finish;
    end
endmodule
