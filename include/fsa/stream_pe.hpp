/**
 * @file stream_pe.hpp
 * @brief 不使用外部current/next拷贝的持久流式PE。
 */
#ifndef STREAM_PE_HPP
#define STREAM_PE_HPP

#include "fsa/stream_types.hpp"

namespace fsa{

    struct StreamPeState{
        elem_t reg{};

        /// @brief 8拍PWL系数波期间保持不变的N输入。
        elem_t pwl_input{};
    };

    void reset_stream_pe_state(StreamPeState& state);

    /**
     * @brief 接受一个token并原地推进PE局部状态。
     *
     * 普通MAC和8段PWL统一经过peMacUnit的唯一FMA调用点。状态由该PE
     * 自己持有，不再复制整个mesh的current/next数组。
     */
    StreamPeOutput stream_pe_step(
        StreamPeState& state,
        const StreamPeToken& token
    );

    /**
     * @brief 一个物理PE处理一个完整phase的定长token波。
     *
     * resident在phase内只读，phase结果经lane或down stream提交，因此
     * FMA流水线没有resident写回造成的loop-carried dependence。
     */
    void stream_pe_process(
        elem_t resident,
        bool lane_enabled,
        bool reduction,
        StreamPeOp op,
        int wave_count,
        StreamPeTokenStream& left,
        StreamPeTokenStream& up,
        StreamPeTokenStream& right,
        StreamPeTokenStream& down,
        StreamPeLaneStream& lane
    );

}  // namespace fsa

#endif  // STREAM_PE_HPP
