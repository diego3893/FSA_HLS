# Accumulator 快慢路径拆分与流水化优化方案

## 1. 任务目标

本任务负责优化 FSA Accumulator，使高频普通累加操作不再被 `EXP_S2` 和
`RECIPROCAL` 的多周期路径拖慢，并为后续内置 ExecutionPlan 的系统顶层提供
可流水接收的 Accumulator 接口。

当前综合基线：

| 项目 | 当前结果 |
|---|---:|
| Accumulator lane 数量 | 4，空间并行已达到 |
| `accumulator_step` Latency | 7～18 拍 |
| `accumulator_step` Interval | 7～18 拍 |
| Pipeline | no |
| DSP | 20 |
| core 普通事务 Latency / Interval | 68 / 69 拍 |

当前问题不是 lane 数量不足，而是所有命令都进入同一个非流水、变延迟函数：

```text
ACC / ACC_SA / SET_SCALE / EXP_S1
                    ┐
EXP_S2              ├── accumulator_step，Latency=7～18，Pipeline=no
RECIPROCAL          ┘
```

本任务的最终目标是：

1. 四列仍保持四套独立算术资源；
2. `ACC`、`ACC_SA` 等高频命令可以连续接收 token，目标 II=1；
3. `EXP_S2`、`RECIPROCAL` 作为独立慢速 scale 变换运行；
4. 写回地址、RMW 元数据、valid 与计算结果严格对齐；
5. 保持现有数值语义、accRAM 布局和四列 scale 状态语义；
6. 保留当前 `accumulator_step()` 作为参考模型，避免直接破坏现有 core。

## 2. 与另一个智能体的边界

本任务的智能体负责 Accumulator，不负责 ExecutionPlan、Controller 或 SA 流水线。

建议由本任务独占以下新增文件：

```text
include/fsa/accumulator_pipeline.hpp
src/core/accumulator_pipeline.cpp
include/fsa/hls/accumulator_pipeline_top.hpp
src/hls/accumulator_pipeline_top.cpp
tests/test_accumulator_pipeline.cpp
tests/hls/test_accumulator_pipeline_top.cpp
hls/accumulator_pipeline/run_hls.tcl
```

允许读取但第一阶段不要修改：

```text
src/core/accumulator.cpp
include/fsa/accumulator.hpp
src/hls/fsa_core_top.cpp
tests/hls/test_fsa_core_full.cpp
run_hls.sh
```

原因：另一个智能体可能同时新增 ExecutionPlan 顶层。两边都修改
`fsa_core_top.cpp` 或 `run_hls.sh` 容易产生冲突。完成独立验证后再进行单独的集成步骤。

## 3. 必须保持的现有语义

现有命令定义在 `include/fsa/control.hpp`：

| 命令 | 当前语义 |
|---|---|
| `EXP_S1` | `scale = sa_in * attentionScale()` |
| `EXP_S2` | `scale = accExp2PWL(scale)` |
| `ACC_SA` | `sram_out = scale * sram_in + sa_in` |
| `ACC` | `sram_out = scale * sram_in` |
| `SET_SCALE` | `scale = sram_in` |
| `RECIPROCAL` | 启动 `scale = 1 / scale`，固定状态机窗口完成 |

以下行为不能改变：

- 四个 lane 使用四个独立的 `scale`；
- `ACC` 和 `ACC_SA` 读取 token 被接受时对应的 scale；
- `SET_SCALE`、`EXP_S1`、`EXP_S2` 和 `RECIPROCAL` 按命令顺序更新 scale；
- reciprocal 只需要一个启动脉冲，运行期间不能被重复启动；
- reciprocal 完成结果优先写回 scale；
- reset 必须取消所有在途 token、busy 状态和 reciprocal 状态；
- 无效周期的数据是 don't-care，但任何 write-valid 都必须对应有效有限结果或明确测试的特殊值。

## 4. 推荐架构

### 4.1 总体结构

```text
Accumulator command token
          │
          v
     命令译码与hazard检查
       │              │
       │              └──────────────┐
       v                             v
快速FMA流水线                  慢速scale变换引擎
ACC / ACC_SA / EXP_S1          EXP_S2 / RECIPROCAL
       │                             │
       v                             v
结果+valid+地址流水              scale提交+done
       │                             │
       └──────────> scale bank <─────┘
                         │
                         v
                    accRAM写回token
```

四个 lane 必须完全分割。快速路径可以共享一个命令 token，但每列拥有独立 FMA
资源；慢速路径也必须保持每列独立状态。

### 4.2 建议的 token 接口

可根据 HLS 约束微调类型，但接口必须包含等价信息：

```cpp
struct AccumulatorToken {
    bool valid = false;
    AccumulatorCmd cmd = AccumulatorCmd::ACC;
    AccVector sa_in{};
    AccVector sram_in{};
    sram_address_t write_addr = 0;
    bool write_enable = false;
    ap_uint<8> tag = 0;
};

struct AccumulatorResultToken {
    bool valid = false;
    AccVector data{};
    sram_address_t write_addr = 0;
    bool write_enable = false;
    ap_uint<8> tag = 0;
};
```

`write_addr`、`write_enable` 和 `tag` 必须与数据经过同样的流水延迟，禁止在结果
产生时重新读取当前外部地址。

还应输出最少以下状态：

```cpp
bool input_ready;
bool scale_busy;
bool slow_done;
```

### 4.3 快速路径

第一版快速路径建议覆盖：

```text
ACC
ACC_SA
EXP_S1
SET_SCALE
```

其中：

- `ACC`、`ACC_SA` 使用四路 FP32 FMA pipeline；
- `EXP_S1` 可以复用每个 lane 自己的 FMA pipeline；
- `SET_SCALE` 是直接状态写入，不需要进入长计算路径；
- 目标是连续 `ACC/ACC_SA` token 的 Final II=1；
- Latency 可以大于1，只要固定并且 valid/address 对齐。

不要为了得到 II=1 而在四个 lane 之间共享一套 FMA。综合后至少应保持当前四 lane
等效的20 DSP量级，具体资源允许因流水寄存器和译码有所增加。

### 4.4 慢速路径

慢速路径覆盖：

```text
EXP_S2
RECIPROCAL
```

`EXP_S2` 可以做成固定延迟 PWL pipeline。`RECIPROCAL` 继续使用恢复除法状态机，
但必须从阻塞整个 Accumulator 改为后台推进：

```text
接收一次RECIPROCAL请求
→ scale_busy=1
→ 每物理拍推进divider
→ 完成时更新四列scale
→ slow_done=1，scale_busy=0
```

运行期间的策略第一版采用保守规则：

- 任何需要读取或更新 scale 的新命令在 `scale_busy=1` 时 backpressure；
- 不允许新的 `SET_SCALE/EXP_S1/EXP_S2/RECIPROCAL` 越过未完成的慢操作；
- 后续若 ExecutionPlan 证明某些 ACC 与慢操作无依赖，再考虑更积极的并发。

这种保守设计可能使包含 reciprocal 的局部命令间隔大于1，但不会让普通连续
`ACC/ACC_SA` 退化。

## 5. 状态相关与正确性要求

流水化后最危险的是 scale RAW/WAW hazard。

例如：

```text
token0: SET_SCALE(L)
token1: ACC(O)
```

`token1` 必须读取 token0 提交后的新 scale，不能读取旧值。类似地：

```text
token0: EXP_S1
token1: EXP_S2
token2: ACC_SA
```

三条命令存在严格依赖，不能只因为各算术单元 II=1 就无条件重叠。

第一版建议实现一个简单 scoreboard：

```text
scale_update_pending
scale_busy
```

只有满足以下条件时才接受依赖 scale 的后续 token：

```text
前序scale更新已经commit
且slow path不busy
```

吞吐优化应集中在连续无 scale 更新的 `ACC/ACC_SA` token 上。

## 6. 实施阶段

### 阶段A：建立不修改旧实现的新参考测试

1. 新增纯 C++ token pipeline 接口和状态。
2. 使用当前 `accumulator_step()` 生成命令级金标准。
3. 测试四列不同 scale、不同 `sa_in/sram_in` 和连续地址。
4. 暂不接入 `fsa_core_top`。

### 阶段B：实现快速路径

1. 实现四 lane FMA pipeline。
2. 固定并集中声明快速路径 latency。
3. 对 valid、write address、tag 使用相同深度的移位寄存器。
4. 连续输入至少64个带随机 bubble 的 `ACC/ACC_SA` token。
5. 验证输出顺序、地址和数值。

### 阶段C：实现慢速路径和hazard

1. 接入 `EXP_S2`。
2. 接入 reciprocal 单脉冲启动和后台推进。
3. 添加 `scale_busy` 和输入 backpressure。
4. 验证 reset 能取消在途慢操作。
5. 验证特殊输入：0、-0、Inf、NaN、普通正数和负数。

### 阶段D：独立 HLS 顶层

新增 `accumulator_pipeline_top`，只用于验证新架构：

```cpp
#pragma HLS INTERFACE ap_ctrl_hs port=return
```

如果逐 token 调用该顶层仍引入事务开销，应再增加一个批处理测试顶层，在内部循环中
连续注入 token：

```cpp
for(int cycle=0; cycle<TEST_CYCLES; ++cycle){
    #pragma HLS PIPELINE II=1
    accumulator_pipeline_tick(...);
}
```

性能验收应以批处理循环能否连续接受 fast token 为准，而不是只看单 token
`ap_ctrl_hs` 的事务 Interval。

### 阶段E：与 ExecutionPlan 顶层集成

独立测试和综合通过后才修改系统顶层。集成时由 ExecutionPlan 智能体或专门的集成
任务调用本模块，不要在本任务中提前修改其新顶层。

## 7. 测试要求

至少新增以下测试：

1. reset 后 scale=0，所有 valid 清空；
2. 四列 `SET_SCALE` 后连续64个 `ACC`，无 bubble；
3. 连续64个 `ACC_SA`，每个 token 使用不同写回地址和tag；
4. 随机混合 `ACC/ACC_SA` 与输入 bubble；
5. `SET_SCALE -> ACC` hazard；
6. `EXP_S1 -> EXP_S2 -> ACC_SA` hazard；
7. reciprocal 只启动一次，运行期间 backpressure 正确；
8. reciprocal 完成后第一条 ACC 使用新倒数 scale；
9. reciprocal 运行中 reset；
10. 与当前 `accumulator_step()` 或软件公式逐 token 比较。

数值比较继续使用工程当前 FP32/PWL 误差口径。地址、valid、tag 和命令顺序必须精确
匹配，不能使用数值容差掩盖控制错位。

## 8. 综合与验收标准

### 功能验收

- C Simulation 通过；
- C/RTL Co-simulation 通过；
- fast token 有 bubble 和无 bubble 两种测试均通过；
- scale hazard、reset 和 reciprocal 特殊值通过；
- 不修改旧 `accumulator_step()` 的既有测试结果。

### 性能验收

| 项目 | 最低要求 |
|---|---|
| 四 lane 空间并行 | 保持4套独立 lane |
| 连续 `ACC/ACC_SA` | Final II=1 |
| fast latency | 固定并写入报告，允许大于1 |
| 地址/valid延迟 | 与fast latency完全一致 |
| reciprocal | 可多拍，但不能阻塞无关顶层逻辑时钟 |
| 时钟目标 | 10 ns，不放宽 |
| Estimated Period | 不超过7.30 ns有效预算 |

### 报告必须区分

```text
fast ACC/ACC_SA II
EXP_S2 latency/II
RECIPROCAL latency和busy窗口
混合命令序列实际间隔
```

不能只报告最好的 fast II=1，然后声称所有 Accumulator 命令都达到 II=1。

## 9. 失败回退策略

如果直接流水化整个新函数仍得到 II>1：

1. 查看 loop-carried dependence，确认是否来自 scale；
2. 将 scale 更新命令与普通 ACC token 分成两个明确函数；
3. 禁止 HLS 把四个 lane 重新共享；
4. 使用编译期 lane 特化或四个显式调用点；
5. 不要仅通过继续增加 `LATENCY` pragma 掩盖状态相关；
6. 保留已通过的参考实现，先缩小到只含 `ACC/ACC_SA` 的最小顶层证明 II=1。

## 10. 交付物

本任务完成时应交付：

```text
新流水Accumulator源码和头文件
纯C++单元测试
HLS C testbench
独立csim/csynth/cosim Tcl
综合报告Markdown
与旧accumulator_step的行为对照说明
供ExecutionPlan顶层使用的接口说明
```

最终说明必须明确：哪些命令达到 II=1，哪些命令仍需要等待，以及集成方应该如何
使用 `input_ready/scale_busy/slow_done/result.valid`。

