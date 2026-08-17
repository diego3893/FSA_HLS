#include "fsa/hls/accumulator_top.hpp"
#include "fsa/accumulator.hpp"

void accumulator_top(
    const fsa::AccumulatorTopInput& input,
    fsa::AccumulatorTopOutput& output
){
    // 与PE顶层保持相同的事务控制协议
    #pragma HLS INTERFACE ap_ctrl_hs port=return

    // 把输入、输出结构体紧密打包成顶层位向量
    #pragma HLS AGGREGATE variable=input compact=bit
    #pragma HLS AGGREGATE variable=output compact=bit

    // 数据端口本身不增加valid/ready，统一由ap_start/ap_done控制
    #pragma HLS INTERFACE ap_none port=input
    #pragma HLS INTERFACE ap_none port=output

    /**
     * 跨顶层调用保存Accumulator状态。
     *
     * scale和reciprocal分别对应每列独立的寄存器及除法器状态。
     */
    static fsa::AccumulatorState current{};

    // 四列需要在同一逻辑步骤内并行访问
    #pragma HLS ARRAY_PARTITION variable=current.scale \
        type=complete dim=1
    #pragma HLS ARRAY_PARTITION variable=current.reciprocal \
        type=complete dim=1

    if(input.reset){
        fsa::reset_accumulator_state(current);
        output = fsa::AccumulatorTopOutput{};
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

    // Accumulator核心输出映射到顶层输出
    for(int col=0; col<fsa::SA_COLS; ++col){
        #pragma HLS UNROLL
        output.sram_out[(std::size_t)col] =
            io.sram_out[(std::size_t)col];
    }

    // 模拟时钟沿：统一提交下一状态
    current = next;

    return;
}