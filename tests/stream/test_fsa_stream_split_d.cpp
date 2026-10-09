#include "fsa/stream/split_d/fsa_stream_split_d.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <random>
#include <vector>

namespace{

    using fsa::acc_t;
    using fsa::dma_word_t;
    using fsa::elem_t;
    using namespace fsa::split_d;

    static dma_word_t q_memory[MAX_QKV_WORDS];
    static dma_word_t k_memory[MAX_QKV_WORDS];
    static dma_word_t v_memory[MAX_QKV_WORDS];
    static dma_word_t o_memory[MAX_O_WORDS];

    enum class InputPattern{
        Random,
        OnesV,
        BasisV,
        BasisVLastFeature,
        /// @brief 后一个key tile产生明显更大的max，用于检验online重标定
        RescaleSecondTile,
        /// @brief Q/K全0、V为跨块位置的基向量，检验query路由与零score路径
        ZeroQkOnesV,
        /// @brief exp2自变量的分数部分落在PWL分段边界附近，检验8段命中
        PwlBoundary
    };

    void packInput(
        const std::vector<std::vector<elem_t>>& input,
        dma_word_t memory[MAX_QKV_WORDS]
    ){
        for(std::size_t token=0; token<input.size(); ++token){
            for(int word=0; word<QKV_WORDS_PER_TOKEN; ++word){
                elem_t values[QKV_ELEMS_PER_WORD]{};
                for(int lane=0; lane<QKV_ELEMS_PER_WORD; ++lane){
                    values[lane] =
                        input[token][word*QKV_ELEMS_PER_WORD+lane];
                }
                memory[token*(std::size_t)QKV_WORDS_PER_TOKEN+
                    (std::size_t)word] = fsa::dma_pack_elem_word(values);
            }
        }
    }

    /// @brief 误差门槛，与原测试台一致
    const double ERROR_LIMIT = 0.03;

    /// @brief 按FP32位模式报告一个float（用于诊断非有限输出）
    std::uint32_t floatBits(const float value){
        std::uint32_t bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        return bits;
    }

    /// @brief 按FP64位模式报告一个double（数学参考是double，其位模式按FP64报告）
    std::uint64_t doubleBits(const double value){
        std::uint64_t bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        return bits;
    }

    /**
     * @brief 比较一个用例的全部输出，返回是否通过。
     *
     * 与旧测试台的差别只在“先判有限、再比误差”：
     * - 若actual或expected非有限，直接判失败。旧逻辑里NaN会使error成为NaN，
     *   而 `error>maximum_error` 与 `error>0.03` 对NaN恒为假，该坐标既不进入
     *   统计也不触发失败，构成漏报；Inf虽然通常会被0.03门槛挡下，但报出的是
     *   “数值超差”而不是“输出非有限”，诊断会误导。
     * - 位模式在诊断中按各自格式报告：actual是FP32，expected是FP64。
     * 误差门槛0.03保持不变。
     */
    bool checkCase(
        const char* name,
        const unsigned length,
        const bool causal,
        const InputPattern pattern,
        const std::vector<std::vector<float>>& actual,
        const std::vector<std::vector<double>>& expected
    ){
        bool non_finite_found = false;
        unsigned non_finite_token = 0;
        int non_finite_feature = 0;

        double maximum_error = 0.0;
        unsigned maximum_token = 0;
        int maximum_feature = 0;
        unsigned first_token = 0;
        int first_feature = 0;
        bool first_error_found = false;

        for(unsigned token=0; token<length; ++token){
            for(int feature=0; feature<HEAD_DIM; ++feature){
                const float actual_value = actual[token][feature];
                const double expected_value = expected[token][feature];

                if(!std::isfinite(actual_value) ||
                        !std::isfinite(expected_value)){
                    if(!non_finite_found){
                        non_finite_found = true;
                        non_finite_token = token;
                        non_finite_feature = feature;
                        std::cerr << "non-finite output: case=" << name
                            << " L=" << length
                            << " causal=" << causal
                            << " at=[" << token << "][" << feature << "]"
                            << " actual=" << actual_value
                            << " actual_bits=0x" << std::hex
                            << floatBits(actual_value)
                            << " expected=" << std::dec << expected_value
                            << " expected_bits=0x" << std::hex
                            << doubleBits(expected_value) << std::dec
                            << "\n";
                    }
                    continue;
                }

                const double error = std::abs(
                    (double)actual_value-expected_value
                );
                if(error > maximum_error){
                    maximum_error = error;
                    maximum_token = token;
                    maximum_feature = feature;
                }
                if(!first_error_found && error>ERROR_LIMIT){
                    first_token = token;
                    first_feature = feature;
                    first_error_found = true;
                }
            }
        }

        if(non_finite_found){
            std::cerr << "non-finite failure: case=" << name
                << " first_at=[" << non_finite_token << "]["
                << non_finite_feature << "]\n";
            return false;
        }

        if(maximum_error > ERROR_LIMIT){
            std::cerr << "numerical failure: case=" << name
                << " L=" << length
                << " causal=" << causal
                << " max_error=" << maximum_error
                << " max_at=[" << maximum_token << "]["
                << maximum_feature << "]"
                << " actual=" << actual[maximum_token][maximum_feature]
                << " expected=" << expected[maximum_token][maximum_feature]
                << " first_at=[" << first_token << "]["
                << first_feature << "]"
                << " first_actual=" << actual[first_token][first_feature]
                << " first_expected=" << expected[first_token][first_feature]
                << "\n";
            if((pattern==InputPattern::BasisV ||
                    pattern==InputPattern::BasisVLastFeature) && length>=2U){
                std::cerr << "basis output: q0=[" << actual[0][0]
                    << ", " << actual[0][1] << "] q1=["
                    << actual[1][0] << ", " << actual[1][1]
                    << "] expected=[" << expected[0][0]
                    << ", " << expected[0][1] << "]\n";
            }
            return false;
        }
        return true;
    }

    /**
     * @brief 自检：用人工构造的NaN/Inf确认比较逻辑确实会判失败。
     *
     * 不向attention顶层注入特殊输入——顶层特殊值合同尚未定案，此处只验证
     * “检查器本身能不能发现非有限输出”。
     */
    bool checkNonFiniteDetection(){
        const double inf_value = std::numeric_limits<double>::infinity();
        const double quiet_nan = std::numeric_limits<double>::quiet_NaN();
        std::vector<std::vector<float>> nan_actual(1,
            std::vector<float>(HEAD_DIM, 0.0F));
        std::vector<std::vector<double>> nan_expected(1,
            std::vector<double>(HEAD_DIM, 0.0));
        // 人工注入NaN：actual侧与被比较的另一侧各放一个，确认两条分支都生效
        nan_actual[0][0] = (float)quiet_nan;
        nan_expected[0][1] = quiet_nan;
        if(checkCase(
                "self-test-nan", 1U, false, InputPattern::Random,
                nan_actual, nan_expected)){
            std::cerr << "checker self-test failure: NaN was not detected\n";
            return false;
        }
        std::vector<std::vector<float>> inf_actual(1,
            std::vector<float>(HEAD_DIM, 0.0F));
        inf_actual[0][0] = (float)inf_value;
        std::vector<std::vector<double>> inf_expected(1,
            std::vector<double>(HEAD_DIM, 0.0));
        inf_expected[0][0] = inf_value;
        if(checkCase(
                "self-test-inf", 1U, false, InputPattern::Random,
                inf_actual, inf_expected)){
            std::cerr << "checker self-test failure: Inf was not detected\n";
            return false;
        }
        return true;
    }

    std::vector<std::vector<float>> unpackOutput(const unsigned length){
        std::vector<std::vector<float>> output(
            length, std::vector<float>(HEAD_DIM, 0.0F)
        );
        for(unsigned token=0; token<length; ++token){
            for(int word=0; word<O_WORDS_PER_TOKEN; ++word){
                const dma_word_t packed =
                    o_memory[token*(unsigned)O_WORDS_PER_TOKEN+
                        (unsigned)word];
                for(int lane=0; lane<O_ACCS_PER_WORD; ++lane){
                    output[token][word*O_ACCS_PER_WORD+lane] =
                        fsa::dma_unpack_acc(packed, lane);
                }
            }
        }
        return output;
    }

    std::vector<std::vector<double>> reference(
        const std::vector<std::vector<elem_t>>& q,
        const std::vector<std::vector<elem_t>>& k,
        const std::vector<std::vector<elem_t>>& v,
        const bool causal
    ){
        const unsigned length = (unsigned)q.size();
        std::vector<std::vector<double>> output(
            length, std::vector<double>(HEAD_DIM, 0.0)
        );
        for(unsigned query=0; query<length; ++query){
            const unsigned key_count = causal ? query+1U : length;
            std::vector<double> score(key_count, 0.0);
            for(unsigned key=0; key<key_count; ++key){
                for(int feature=0; feature<HEAD_DIM; ++feature){
                    score[key] += (double)q[query][feature]*
                        (double)k[key][feature];
                }
            }
            const double maximum =
                *std::max_element(score.begin(), score.end());
            std::vector<double> probability(key_count, 0.0);
            double denominator = 0.0;
            for(unsigned key=0; key<key_count; ++key){
                probability[key] = std::exp(
                    (score[key]-maximum)/std::sqrt((double)HEAD_DIM)
                );
                denominator += probability[key];
            }
            for(int feature=0; feature<HEAD_DIM; ++feature){
                for(unsigned key=0; key<key_count; ++key){
                    output[query][feature] += probability[key]*
                        (double)v[key][feature];
                }
                output[query][feature] /= denominator;
            }
        }
        return output;
    }

    /**
     * @brief 用给定输入提交一次顶层事务并比较结果。
     *
     * 输入构造与提交分开，是为了让定向用例（online重标定、跨块基向量、
     * PWL分段边界）能显式构造矩阵，而不是只能从随机分布生成。
     */
    bool runCaseWithInput(
        const char* name,
        const unsigned length,
        const bool causal,
        const InputPattern pattern,
        const std::vector<std::vector<elem_t>>& q,
        const std::vector<std::vector<elem_t>>& k,
        const std::vector<std::vector<elem_t>>& v
    ){
        if(q.size()<length || k.size()<length || v.size()<length){
            std::cerr << "case input too short: " << name << "\n";
            return false;
        }

        std::fill(q_memory, q_memory+MAX_QKV_WORDS, (dma_word_t)0);
        std::fill(k_memory, k_memory+MAX_QKV_WORDS, (dma_word_t)0);
        std::fill(v_memory, v_memory+MAX_QKV_WORDS, (dma_word_t)0);
        std::fill(o_memory, o_memory+MAX_O_WORDS, (dma_word_t)0);
        packInput(q, q_memory);
        packInput(k, k_memory);
        packInput(v, v_memory);

        ap_uint<8> status = 0xff;
        fsa_stream_split_d(
            q_memory, k_memory, v_memory, o_memory,
            (ap_uint<32>)length, causal, status
        );
        if(status != 0){
            std::cerr << "status failure: " << status.to_uint() << "\n";
            return false;
        }

        const std::vector<std::vector<float>> actual =
            unpackOutput(length);
        const std::vector<std::vector<double>> expected =
            reference(q, k, v, causal);
        return checkCase(name, length, causal, pattern, actual, expected);
    }

    bool runCase(
        const char* name,
        const unsigned length,
        const bool causal,
        const int seed,
        const InputPattern pattern
    ){
        std::mt19937 generator(seed);
        std::uniform_real_distribution<float> qk_distribution(-0.5F, 0.5F);
        std::uniform_real_distribution<float> v_distribution(-1.0F, 1.0F);
        std::vector<std::vector<elem_t>> q(
            length, std::vector<elem_t>(HEAD_DIM)
        );
        std::vector<std::vector<elem_t>> k = q;
        std::vector<std::vector<elem_t>> v = q;
        for(unsigned token=0; token<length; ++token){
            for(int feature=0; feature<HEAD_DIM; ++feature){
                q[token][feature] = (elem_t)qk_distribution(generator);
                k[token][feature] = (elem_t)qk_distribution(generator);
                v[token][feature] = (elem_t)v_distribution(generator);
            }
        }
        if(pattern == InputPattern::OnesV){
            for(unsigned token=0; token<length; ++token){
                for(int feature=0; feature<HEAD_DIM; ++feature){
                    v[token][feature] = (elem_t)1.0F;
                }
            }
        }else if(pattern==InputPattern::BasisV ||
                pattern==InputPattern::BasisVLastFeature){
            const int probe_feature =
                pattern==InputPattern::BasisV ? 0 : HEAD_DIM-1;
            for(unsigned token=0; token<length; ++token){
                for(int feature=0; feature<HEAD_DIM; ++feature){
                    q[token][feature] = (elem_t)0.0F;
                    k[token][feature] = (elem_t)0.0F;
                    v[token][feature] = (elem_t)0.0F;
                }
                q[token][probe_feature] = (elem_t)1.0F;
            }
            k[0][probe_feature] = (elem_t)0.5F;
            k[1][probe_feature] = (elem_t)-0.5F;
            v[0][0] = (elem_t)1.0F;
            v[1][1] = (elem_t)1.0F;
        }else if(pattern==InputPattern::RescaleSecondTile){
            // 每个query只在feature0上与key对齐；第0个key tile的logit很小，
            // 第2个key tile的key值明显更大 → 触发online重标定(alpha<1)。
            for(unsigned token=0; token<length; ++token){
                for(int feature=0; feature<HEAD_DIM; ++feature){
                    q[token][feature] = (elem_t)0.0F;
                    k[token][feature] = (elem_t)0.0F;
                    v[token][feature] = (elem_t)0.0F;
                }
                q[token][0] = (elem_t)1.0F;
                v[token][0] = (elem_t)1.0F;
            }
            for(unsigned token=0; token<length; ++token){
                k[token][0] = token<(unsigned)PE_DIM
                    ? (elem_t)1.0F : (elem_t)2.5F;
            }
        }else if(pattern==InputPattern::ZeroQkOnesV){
            // Q/K全0 → 所有score相同；V为按token位置轮转的基向量，
            // 检验跨块位置的query路由与均匀概率下的输出。
            for(unsigned token=0; token<length; ++token){
                for(int feature=0; feature<HEAD_DIM; ++feature){
                    q[token][feature] = (elem_t)0.0F;
                    k[token][feature] = (elem_t)0.0F;
                    v[token][feature] = (elem_t)0.0F;
                }
                v[token][token%(unsigned)HEAD_DIM] = (elem_t)1.0F;
            }
        }else if(pattern==InputPattern::PwlBoundary){
            // 确定性构造：q全1、k按token取1.5×网格值，使点积为
            // HEAD_DIM×(1.5×grid)，归一化后(score-max)/sqrt(H)恰好落在
            // 0.75网格上，即PWL自变量的分段边界 x2=-1,-0.75,...,0.75 附近。
            const float grid[6] = {1.0F, 0.5F, 0.0F, -0.5F, -1.0F, -1.5F};
            for(unsigned token=0; token<length; ++token){
                for(int feature=0; feature<HEAD_DIM; ++feature){
                    q[token][feature] = (elem_t)1.0F;
                    k[token][feature] = (elem_t)(1.5F*grid[token%6U]);
                }
            }
        }

        return runCaseWithInput(name, length, causal, pattern, q, k, v);
    }

}  // namespace

namespace{

    /**
     * @brief 非法长度调用：要求status=1且O内存逐word未被改写。
     *
     * 输入缓冲复用合法大小的Q/K/V/O数组（MAX_QKV_WORDS/MAX_O_WORDS），
     * 不为非法长度另外分配或打包越界输入。
     */
    bool runInvalidLengthCase(
        const char* name,
        const unsigned length
    ){
        std::fill(q_memory, q_memory+MAX_QKV_WORDS, (dma_word_t)0);
        std::fill(k_memory, k_memory+MAX_QKV_WORDS, (dma_word_t)0);
        std::fill(v_memory, v_memory+MAX_QKV_WORDS, (dma_word_t)0);
        for(int word=0; word<MAX_O_WORDS; ++word){
            o_memory[word] = (dma_word_t)0x12345678U;
        }

        ap_uint<8> status = 0;
        fsa_stream_split_d(
            q_memory, k_memory, v_memory, o_memory,
            (ap_uint<32>)length, false, status
        );

        bool ok = true;
        if(status != 1){
            std::cerr << "invalid-length status failure: case=" << name
                << " L=" << length
                << " status=" << status.to_uint() << "\n";
            ok = false;
        }
        for(int word=0; word<MAX_O_WORDS; ++word){
            if(o_memory[word] != (dma_word_t)0x12345678U){
                std::cerr << "invalid-length canary failure: case=" << name
                    << " L=" << length
                    << " word=" << word
                    << " value=0x" << std::hex
                    << o_memory[word].to_uint64() << std::dec << "\n";
                ok = false;
                break;
            }
        }
        return ok;
    }

}  // namespace

int main(){
    const unsigned primary_length = PE_DIM==4 ? 7U : 5U;
    bool passed = true;

    // 检查器自检：确认非有限输出不会被误差比较漏掉
    passed = checkNonFiniteDetection() && passed;

    // 原有用例，全部保留（不删除任何既有诊断用例）
    passed = runCase(
        "single-key-first", 1U, false, 1606, InputPattern::Random
    ) && passed;
    passed = runCase(
        "two-key-ones-v", 2U, false, 1607, InputPattern::OnesV
    ) && passed;
    passed = runCase(
        "two-key-basis-v", 2U, false, 1608, InputPattern::BasisV
    ) && passed;
    passed = runCase(
        "two-key-basis-v-last-feature", 2U, false, 1609,
        InputPattern::BasisVLastFeature
    ) && passed;
    passed = runCase(
        "primary-noncausal", primary_length, false, 1604,
        InputPattern::Random
    ) && passed;
    passed = runCase(
        "primary-causal", primary_length, true, 1605,
        InputPattern::Random
    ) && passed;
    if(PE_DIM==4 && HEAD_DIM==16){
        passed = runCase(
            "full-tile-noncausal", 16U, false, 1616,
            InputPattern::Random
        ) && passed;
        passed = runCase(
            "full-tile-causal", 16U, true, 1617,
            InputPattern::Random
        ) && passed;
    }

    // 边界长度：D-1、D、D+1、2D、2D+1，causal与非causal各一遍
    {
        const unsigned boundary_lengths[5] = {
            (unsigned)PE_DIM-1U,
            (unsigned)PE_DIM,
            (unsigned)PE_DIM+1U,
            2U*(unsigned)PE_DIM,
            2U*(unsigned)PE_DIM+1U
        };
        int seed = 1700;
        for(int index=0; index<5; ++index){
            const unsigned length = boundary_lengths[index];
            passed = runCase(
                "boundary-noncausal", length, false, seed++,
                InputPattern::Random
            ) && passed;
            passed = runCase(
                "boundary-causal", length, true, seed++,
                InputPattern::Random
            ) && passed;
        }
    }

    // 定向状态路径用例（随机长度不能替代这些覆盖）
    passed = runCase(
        "rescale-second-tile", 2U*(unsigned)PE_DIM+1U, false, 1801,
        InputPattern::RescaleSecondTile
    ) && passed;
    passed = runCase(
        "rescale-second-tile-causal", 2U*(unsigned)PE_DIM+1U, true, 1802,
        InputPattern::RescaleSecondTile
    ) && passed;
    passed = runCase(
        "zero-qk-ones-v", 2U*(unsigned)PE_DIM+1U, false, 1803,
        InputPattern::ZeroQkOnesV
    ) && passed;
    passed = runCase(
        "zero-qk-ones-v-causal", 2U*(unsigned)PE_DIM+1U, true, 1804,
        InputPattern::ZeroQkOnesV
    ) && passed;
    passed = runCase(
        "pwl-boundary-noncausal", 2U*(unsigned)PE_DIM+1U, false, 1805,
        InputPattern::PwlBoundary
    ) && passed;
    passed = runCase(
        "pwl-boundary-causal", 2U*(unsigned)PE_DIM+1U, true, 1806,
        InputPattern::PwlBoundary
    ) && passed;

    // 非法长度：0 与 MAX_SEQUENCE_LENGTH+1，都要求status=1且O全canary
    passed = runInvalidLengthCase("invalid-length-zero", 0U) && passed;
    passed = runInvalidLengthCase(
        "invalid-length-over-max", (unsigned)fsa::MAX_SEQUENCE_LENGTH+1U
    ) && passed;

    if(!passed){
        return 1;
    }

    std::cout << "[PASS] fsa_stream_split_d: PE=" << PE_DIM
        << "x" << PE_DIM << " HEAD_DIM=" << HEAD_DIM
        << " DIM_BLOCKS=" << DIM_BLOCKS << "\n";
    return 0;
}
