#include "fsa/hls/accumulator_top.hpp"
#include "fsa/accumulator.hpp"

void accumulator_top(
    const fsa::AccumulatorTopInput& input,
    fsa::AccVector& sram_out,
    bool& sram_write_valid,
    bool& reciprocal_result
){
    /**
     * MOD: 保留可靠的事务背压；一次RECIPROCAL请求在事务内部完成15个
     * 固定阶段，外部不再发送14笔空事务。实际物理拍数由综合报告确认。
     */
    #pragma HLS INTERFACE ap_ctrl_hs port=return

    // 把输入、四列输出紧密打包成顶层位向量。
    #pragma HLS AGGREGATE variable=input compact=bit
    #pragma HLS AGGREGATE variable=sram_out compact=bit

    // MOD: 两类有效标志分开，避免reciprocal结果被误当成SRAM写使能。
    #pragma HLS INTERFACE ap_none port=input
    #pragma HLS INTERFACE ap_none port=sram_out
    #pragma HLS INTERFACE ap_none port=sram_write_valid
    #pragma HLS INTERFACE ap_none port=reciprocal_result

    /**
     * 跨顶层调用保存Accumulator状态。
     *
     * scale和reciprocal分别对应每列独立的寄存器及除法器状态。
     */
    static fsa::AccumulatorState current{};

    // MOD: ap_rst同时清除业务状态；input.reset仍保留为显式业务复位。
    #pragma HLS RESET variable=current

    // 四列需要在同一逻辑步骤内并行访问
    #pragma HLS ARRAY_PARTITION variable=current.scale \
        type=complete dim=1
    #pragma HLS ARRAY_PARTITION variable=current.reciprocal \
        type=complete dim=1

    if(input.reset){
        fsa::reset_accumulator_state(current);
        sram_out = fsa::AccVector{};
        sram_write_valid = false;
        reciprocal_result = false;
        return;
    }

    sram_write_valid = false;
    reciprocal_result = false;

    /**
     * MOD: reciprocal单事务快路径。分母在事务开始时从scale锁存，四列
     * 并行运行固定15阶段，最后原子写回scale。
     */
    if(input.ctrl.valid
            && input.ctrl.bits.cmd==fsa::AccumulatorCmd::RECIPROCAL){
        fsa::accumulator_reciprocal_transaction(
            current.scale,
            sram_out
        );

        // reciprocal只更新内部scale；wrapper在ap_done拍产生result_valid。
        reciprocal_result = true;
        return;
    }

    /**
     * 顶层端口格式转换成Accumulator核心接口。
     */
    fsa::AccumulatorIO io{};

    #pragma HLS ARRAY_PARTITION variable=io.sa_in \
        type=complete dim=1
    #pragma HLS ARRAY_PARTITION variable=io.sram_in \
        type=complete dim=1
    #pragma HLS ARRAY_PARTITION variable=io.sram_out \
        type=complete dim=1

    io.ctrl_in = input.ctrl;

    for(int col=0; col<fsa::SA_COLS; ++col){
        #pragma HLS UNROLL
        io.sa_in[(std::size_t)col] =
            input.sa_in[(std::size_t)col];

        io.sram_in[(std::size_t)col] =
            input.sram_in[(std::size_t)col];
    }

    /**
     * next表示下一逻辑步骤的Accumulator寄存器状态。
     *
     * accumulator_step内部已经执行next=current，
     * 所以这里使用next{}是安全的。
     */
    fsa::AccumulatorState next{};

    #pragma HLS ARRAY_PARTITION variable=next.scale \
        type=complete dim=1
    #pragma HLS ARRAY_PARTITION variable=next.reciprocal \
        type=complete dim=1

    fsa::accumulator_step(current, next, io);

    // Accumulator核心输出映射到顶层输出。
    for(int col=0; col<fsa::SA_COLS; ++col){
        #pragma HLS UNROLL
        sram_out[(std::size_t)col] =
            io.sram_out[(std::size_t)col];
    }

    sram_write_valid = io.sram_write_valid;

    // 模拟时钟沿：统一提交下一状态
    current = next;

    return;
}
