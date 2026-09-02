/**
 * @file stream_types.hpp
 * @brief 高吞吐FSA路径在PE、CMP和Accumulator之间传递的定长token。
 */
#ifndef STREAM_TYPES_HPP
#define STREAM_TYPES_HPP

#include <cstdint>

#include <hls_stream.h>

#include "fsa/control.hpp"
#include "fsa/fsa_core_interface.hpp"
#include "fsa/types.hpp"

namespace fsa{

    /**
     * @brief 流式PE每拍执行的操作。
     *
     * BUBBLE仍会沿网络传递，用固定token数避免DATAFLOW进程因分支少写
     * FIFO而死锁。
     */
    enum class StreamPeOp : std::uint8_t{
        BUBBLE = 0,
        LOAD_Q = 1,
        QK_MAC = 2,
        LOAD_SCORE = 3,
        SUB_MAX = 4,
        SCALE_SCORE = 5,
        EXP2_PWL = 6,
        ROWSUM_MAC = 7,
        PV_MAC = 8,
        PASS = 9,

        /// @brief wave0计算rowsum，后续SA_ROWS个wave连续计算PV。
        ROWSUM_PV = 10
    };

    /// @brief 任一mesh phase允许的最大token波数，仅用于综合边界和报告。
    constexpr int STREAM_MAX_PHASE_WAVES =
        SA_ROWS+1>exp2PWLPieces ? SA_ROWS+1 : exp2PWLPieces;

    /// @brief reset、初始化和finalize均开启时单请求的最大逻辑拍数。
    constexpr int STREAM_MAX_REQUEST_CYCLES =
        1+SA_COLS+(SA_COLS+1)+
        (3*SA_ROWS+SA_COLS+exp2PWLPieces+4)+
        (2*SA_ROWS+SA_COLS)+
        (2+reciprocalLatency)+(SA_ROWS+1);

    struct StreamPeToken{
        bool valid = false;
        bool last = false;
        bool masked = false;
        StreamPeOp op = StreamPeOp::BUBBLE;
        ap_uint<16> tag = 0;

        /// @brief 左右方向的K/V/PWL系数或待装入PE的数据。
        elem_t horizontal{};

        /// @brief 上下方向的部分和或PWL编码截距。
        acc_t vertical{};
    };

    struct StreamPeOutput{
        StreamPeToken right{};
        StreamPeToken down{};
        bool register_written = false;
        elem_t resident{};
    };

    /**
     * @brief 非归约phase中由单个PE提交给输出对齐网络的数据。
     */
    struct StreamPeLaneResult{
        bool valid = false;
        bool segment_match = false;
        elem_t element{};
    };

    using StreamPeTokenStream = hls::stream<StreamPeToken>;
    using StreamPeLaneStream = hls::stream<StreamPeLaneResult>;

    /**
     * @brief InputDelayer每个逻辑拍送入持久化FSA阵列的完整边界token。
     *
     * token只携带边界数据和FSA原生控制；S、N、P以及竖直部分和均不
     * 离开PE/CMP网络。last是请求末拍，不是矩阵行末拍。
     */
    struct StreamArrayCycleToken{
        bool reset = false;
        bool last = false;
        bool request_valid = false;
        bool finalize = false;

        elem_t pe_data[SA_ROWS]{};
        ValidData<PECtrl> pe_ctrl[SA_ROWS]{};
        ValidData<CmpControl> cmp_ctrl{};

        AccReadRequest acc_read{};
        acc_t acc_constant_value{};
        ValidData<AccumulatorControl> acc_ctrl{};
    };

    /**
     * @brief FSA阵列底部输出及同拍Accumulator控制。
     */
    struct StreamArrayOutputToken{
        bool reset = false;
        bool last = false;
        bool request_valid = false;
        bool finalize = false;

        acc_t array_output[SA_COLS]{};
        AccReadRequest acc_read{};
        acc_t acc_constant_value{};
        ValidData<AccumulatorControl> acc_ctrl{};
    };

    using StreamArrayCycleStream = hls::stream<StreamArrayCycleToken>;
    using StreamArrayOutputStream = hls::stream<StreamArrayOutputToken>;

    struct StreamScoreToken{
        bool valid = false;
        bool last = false;
        bool masked = false;
        ap_uint<16> query = 0;
        ap_uint<16> key = 0;
        acc_t score{};
    };

    struct StreamProbabilityToken{
        bool valid = false;
        bool last = false;
        ap_uint<16> query = 0;
        ap_uint<16> key = 0;
        elem_t probability{};
    };

    struct StreamAccumulatorToken{
        bool valid = false;
        bool last = false;
        ap_uint<16> query = 0;
        ap_uint<16> feature = 0;
        acc_t rowsum{};
        acc_t pv{};
    };

}  // namespace fsa

#endif  // STREAM_TYPES_HPP
