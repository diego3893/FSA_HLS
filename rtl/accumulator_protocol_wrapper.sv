/**
 * @file accumulator_protocol_wrapper.sv
 * @brief Accumulator HLS IP的显式物理时钟握手包装层
 *
 * MOD: HLS内部继续使用ap_ctrl_hs保证普通浮点事务有可靠背压；本包装层
 * 将ap_idle/ap_done转换为命名清楚的busy、sram_write_valid和result_valid。
 * request_valid必须表示一笔事务请求，并在request_ready为1时被接受；
 * request_is_reciprocal在request_valid撤销前必须保持稳定。
 */
module accumulator_protocol_wrapper #(
    parameter int INPUT_WIDTH = 266,
    parameter int OUTPUT_WIDTH = 128
) (
    input  logic                    ap_clk,
    input  logic                    ap_rst,

    input  logic                    request_valid,
    input  logic                    request_is_reciprocal,
    output logic                    request_ready,
    input  logic [INPUT_WIDTH-1:0]  input_r,

    output logic [OUTPUT_WIDTH-1:0] sram_out,
    output logic                    busy,
    output logic                    sram_write_valid,
    output logic                    result_valid
);

    logic ap_done;
    logic ap_idle;
    logic ap_ready;
    logic core_sram_write_valid;
    logic core_reciprocal_result;
    logic reciprocal_request_seen;
    logic core_start;

    /**
     * MOD: 原Chisel会把RECIPROCAL连续保持有效15拍。第一次握手后锁住
     * ap_start，直到request_valid撤销，防止完成拍被误接受为第二次倒数。
     * 普通命令仍保持标准ap_ctrl_hs的逐次握手语义。
     */
    always_ff @(posedge ap_clk) begin
        if (ap_rst) begin
            reciprocal_request_seen <= 1'b0;
        end else if (!request_valid) begin
            reciprocal_request_seen <= 1'b0;
        end else if (core_start && ap_ready && request_is_reciprocal) begin
            reciprocal_request_seen <= 1'b1;
        end
    end

    assign core_start = request_valid
        && !(request_is_reciprocal && reciprocal_request_seen);

    // MOD: busy覆盖真实HLS事务的全部物理时钟，而不是C++逻辑step数量。
    assign request_ready = ap_ready
        && !(request_is_reciprocal && reciprocal_request_seen);
    assign busy = ~ap_idle;

    // MOD: ap_done将事务类型标志收窄成单个物理时钟的有效脉冲。
    assign sram_write_valid = ap_done && core_sram_write_valid;
    assign result_valid = ap_done && core_reciprocal_result;

    /**
     * 端口名与src/hls/accumulator_top.cpp中的显式顶层参数对应。
     * 重新综合若工具对聚合端口追加后缀，应只在此处同步生成后的端口名。
     */
    accumulator_top accumulator_top_i (
        .ap_clk(ap_clk),
        .ap_rst(ap_rst),
        .ap_start(core_start),
        .ap_done(ap_done),
        .ap_idle(ap_idle),
        .ap_ready(ap_ready),
        .input_r(input_r),
        .sram_out(sram_out),
        .sram_write_valid(core_sram_write_valid),
        .reciprocal_result(core_reciprocal_result)
    );

endmodule
