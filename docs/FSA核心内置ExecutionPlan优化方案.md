# FSA 核心内置 ExecutionPlan 优化方案

## 1. 任务目标

本任务负责将当前“外部每个 logical step 调用一次 `fsa_core_top`”的执行方式，改为
“一次顶层事务在 core 内部执行一段或完整 ExecutionPlan”。

当前基线：

| 项目 | 当前结果 |
|---|---:|
| SA stage | Latency=16，II=1，16套PE资源 |
| Accumulator | Latency=7～18，Pipeline=no |
| core普通事务 | Latency=68，Interval=69 |
| core综合最大值 | Latency=89，Interval=90 |
| core Pipeline | no |

当前一次 `fsa_core_top()` 调用只推进一个 Chisel logical step。以28步
`ATTENTION_SCORE` 为例，普通协同仿真口径下约需：

```text
28 × 69 ≈ 1932个物理时钟
```

本任务的目标是取消每个 logical step 的独立 `ap_start/ap_done` 开销，让内部 timer
连续运行 ExecutionPlan。第一阶段不承诺 II=1，而是先测出消除事务边界后的真实
dependence-limited II；第二阶段再根据报告决定是否引入完整 token pipeline。

## 2. 设计依据和源文件

ExecutionPlan 的功能真值以原 Chisel 为准：

```text
../FSA-main/src/main/scala/fsa/ExecutionPlan.scala
../FSA-main/src/main/scala/fsa/MatrixEngineController.scala
../FSA-main/src/main/scala/fsa/FSA.scala
```

当前 HLS 侧可参考：

```text
include/fsa/instruction.hpp
include/fsa/control.hpp
tests/hls/test_fsa_core_full.cpp
src/hls/fsa_core_top.cpp
docs/FSA系统级顶层模块编写指南.md
docs/SA流水线修改指南.md
```

`test_fsa_core_full.cpp` 的周期表可用于回归，但不能取代 Chisel ExecutionPlan 作为
长期真值来源。

## 3. 与另一个智能体的边界

本任务不负责修改 Accumulator 内部算法。另一个智能体会开发快慢路径拆分和 fast
token pipeline。

建议由本任务独占以下新增文件：

```text
include/fsa/execution_plan.hpp
src/core/execution_plan.cpp
include/fsa/fsa_core_datapath.hpp
src/core/fsa_core_datapath.cpp
include/fsa/hls/fsa_core_execute_top.hpp
src/hls/fsa_core_execute_top.cpp
tests/test_execution_plan.cpp
tests/hls/test_fsa_core_execute_top.cpp
hls/fsa_core_execute/run_hls.tcl
docs/FSA核心内置ExecutionPlan综合报告.md
```

第一阶段不要修改：

```text
src/core/accumulator.cpp
include/fsa/accumulator.hpp
include/fsa/accumulator_pipeline.hpp
src/core/accumulator_pipeline.cpp
run_hls.sh
```

允许修改 `src/hls/fsa_core_top.cpp` 的前提是只做“提取共享 datapath step”所需的机械
重构，并且必须保持原 `test_fsa_core_top.cpp` 和 `test_fsa_core_full.cpp` 通过。更稳妥的
做法是第一阶段保留旧顶层不动，新顶层使用独立状态和共享核心函数。

## 4. 必须理解的 II 限制

当前 `fsa_core_sa_stage` 报告 II=1，是因为它看到的是独立的 `current` 输入和 `next`
输出。它本身不知道相邻调用满足：

```text
current(t+1) = next(t)
```

如果内部 ExecutionPlan 直接写成：

```cpp
for(int timer=0; timer<max_cycle; ++timer){
    #pragma HLS PIPELINE II=1
    fsa_core_sa_stage(current_sa, next_sa, io);
    current_sa = next_sa;
}
```

HLS 可能检测到跨 iteration 的 RAW dependence。由于 SA 结果约16拍后才产生，循环的
实际 II 很可能被提高到16。

因此本任务必须区分：

```text
SA子函数资源吞吐II=1
ExecutionPlan控制生成II
ExecutionPlan完整循环Final II
完整FA任务级Interval
```

不能因为 SA 子报告为 II=1 就直接宣称内部 ExecutionPlan 也达到 II=1。

## 5. 推荐的两阶段实现

### 5.1 阶段一：内部顺序 ExecutionPlan

阶段一的目标是消除每 logical step 一次顶层事务，不立即重写 SA 状态语义。

新增顶层概念接口：

```cpp
struct FsaCoreExecuteInput {
    bool reset = false;
    MatrixInstruction instruction{};

    // 第一版可保留Scratchpad预装/读回调试端口。
    bool spad_write_valid[nMemPorts]{};
    sram_address_t spad_write_addr[nMemPorts]{};
    // 其余窄端口字段按现有顶层复用。
};

struct FsaCoreExecuteOutput {
    bool instruction_done = false;
    bool busy = false;
    ap_uint<16> executed_steps = 0;
    // 保留必要的accRAM读回和错误状态，不复制全部调试宽输出。
};
```

顶层保持：

```cpp
#pragma HLS INTERFACE ap_ctrl_hs port=return
```

但一次事务执行完整的一条 MatrixInstruction，而不是一个 logical step。

内部结构：

```cpp
for(int timer=0; timer<max_cycle; ++timer){
    // 1. execution_plan(func, timer)产生本step控制
    // 2. 计算Scratchpad和accRAM地址
    // 3. 推进InputDelayer
    // 4. 推进SA
    // 5. 推进OutputDelayer
    // 6. 推进Accumulator
    // 7. 处理RMW并提交状态
}
```

第一版允许循环 Final II 大于1。必须记录真实综合结果，不要为了满足目标而隐藏 II
violation。

### 阶段一的价值

即使循环最终 II=16，28步 score 的估算也可能从约1932拍下降到约：

```text
27 × 16 + pipeline drain ≈ 448拍量级
```

具体值以综合和协同仿真为准。该结果足以判断继续进行 token pipeline 是否值得。

### 5.2 阶段二：token 化 ExecutionPlan

如果阶段一 Final II 被 SA/Accumulator 状态相关限制，则将 logical step 拆成可在途的
token，而不是等待完整 `next` 返回。

建议控制 token 至少包含：

```cpp
struct CoreControlToken {
    bool valid = false;
    MxFunc func = MxFunc::LOAD_STATIONARY;
    ap_uint<16> timer = 0;

    ScratchpadReadRequest sp_read{};
    ValidData<PECtrl> pe_ctrl[SA_ROWS]{};
    ValidData<CmpControl> cmp_ctrl{};
    AccumulatorReadRequest acc_read{};
    ValidData<AccumulatorControl> acc_ctrl{};

    sram_address_t write_addr = 0;
    ap_uint<8> tag = 0;
};
```

所有数据、valid、控制、RMW地址和tag必须一起经过对应流水线：

```text
第0拍：token0进入SA
第1拍：token1进入SA
...
第16拍：token0的SA结果、valid、地址同时到达下游
第17拍：token1结果到达
```

禁止出现控制在第1拍到达而浮点数据到第16拍才到达的情况。

## 6. ExecutionPlan 的迁移范围

至少实现 `MxFunc` 中五种 Matrix 操作：

```text
LOAD_STATIONARY
ATTENTION_SCORE_COMPUTE
ATTENTION_VALUE_COMPUTE
ATTENTION_LSE_NORM_SCALE
ATTENTION_LSE_NORM
```

建议 `execution_plan.hpp` 提供纯组合、无静态状态的查询接口：

```cpp
struct ExecutionPlanStep {
    bool valid = false;
    bool last = false;
    // 本logical step需要的所有控制和地址增量。
};

ExecutionPlanStep make_execution_plan_step(
    const MatrixInstruction& instruction,
    unsigned timer
);

unsigned execution_plan_length(
    const MatrixInstruction& instruction
);
```

控制字段必须逐项对照 Chisel，不要把 testbench 中的固定常量散落复制到新顶层。

参数例如以下内容应集中配置：

```text
SA_ROWS / SA_COLS
exp2PWLPieces
reciprocalLatency
attentionScale
各阶段起始和结束周期
```

## 7. 状态和数据通路组织

建议将当前匿名的 `FsaCoreState` 和一步数据通路逻辑提取成可复用形式：

```cpp
struct FsaCoreDatapathState;
struct FsaCoreStepInput;
struct FsaCoreStepOutput;

void fsa_core_datapath_step(
    const FsaCoreDatapathState& current,
    FsaCoreDatapathState& next,
    const FsaCoreStepInput& input,
    FsaCoreStepOutput& output
);
```

旧 `fsa_core_top` 和新 `fsa_core_execute_top` 都可以复用它：

```text
旧顶层：一次事务调用一次step，作为参考模型
新顶层：一次事务在内部循环调用多次step
```

但是需要注意：如果 `fsa_core_datapath_step()` 仍包含阻塞的多拍 SA 和 Accumulator，
内部循环 II 仍会受 current/next 相关限制。提取函数只解决代码复用，不等于解决流水。

## 8. 与 Accumulator 优化的集成契约

第一阶段可继续调用当前 `accumulator_step()`，以便独立测量内部 ExecutionPlan 的收益。

另一个智能体完成新 Accumulator 后，集成方通过以下抽象连接：

```text
ExecutionPlan产生acc token
→ 检查acc input_ready/scale_busy
→ token被接受时timer才能跨越相关依赖点
→ result.valid到达时按result.write_addr写回accRAM
→ slow_done用于scale变换完成同步
```

本任务不要假设所有 Accumulator 命令都 II=1。报告必须分别处理：

- fast `ACC/ACC_SA`；
- `EXP_S1/EXP_S2` scale依赖；
- reciprocal busy窗口。

## 9. 测试计划

### 9.1 ExecutionPlan 单元测试

对每种 `MxFunc` 检查：

- 总 step 数；
- 每个 timer 的 Scratchpad 地址；
- `PECtrl` 各行 valid 和字段；
- CMP命令；
- Accumulator命令；
- accRAM读地址、RMW和写回时刻；
- first/last step；
- causal和zero配置。

4×4 配置下应与原 Chisel 或已确认的 `test_fsa_core_full` 调度逐拍一致。

### 9.2 阶段级执行测试

按以下顺序增加覆盖：

1. `LOAD_STATIONARY`；
2. `ATTENTION_SCORE_COMPUTE`，检查Score、rowmax和L；
3. `ATTENTION_VALUE_COMPUTE`，检查O numerator；
4. `ATTENTION_LSE_NORM_SCALE`，检查1/L；
5. `ATTENTION_LSE_NORM`，检查最终O。

### 9.3 完整 FA 测试

复用 `test_fsa_core_full.cpp` 的 Q/K/V 和 golden model，但新测试必须变为：

```text
预装Q/K/V
→ 一次或少量MatrixInstruction事务
→ 等instruction_done
→ 读回L/O
→ 与golden比较
```

不能继续由 testbench 每 logical step 手工调用顶层，否则没有验证本任务目标。

### 9.4 参考模型对照

保留旧逐-step顶层作为金标准，比较：

```text
ExecutionPlan logical token顺序
Scratchpad读请求
SA有效输出顺序
CMP max
Accumulator命令顺序
accRAM写地址与数据
最终L和O
```

新实现允许物理周期不同，但有效 token 顺序和体系结构结果必须一致。

## 10. 综合实验和验收

### 阶段一验收

- C Simulation 通过；
- 内部执行至少 `LOAD_STATIONARY + ATTENTION_SCORE`；
- 一次 `ap_start` 执行完整阶段；
- C/RTL Co-simulation 通过；
- 记录内部 loop Final II；
- 完整阶段物理周期显著小于逐-step基线；
- 保持16套 PE 和4套 CMP等效资源；
- 旧 `fsa_core_top` 测试仍通过。

### 阶段二最低目标

| 项目 | 目标 |
|---|---|
| ExecutionPlan控制生成 | II=1 |
| SA输入token | 能连续valid |
| SA资源 | 16套PE，不能重新共享 |
| 数据/valid/control | 固定且一致的延迟 |
| fast Accumulator token | 使用另一个任务提供的II=1接口 |
| 完整FA | C仿真和C/RTL协同仿真通过 |
| 时钟 | 10 ns，不放宽 |

完整 ExecutionPlan 的实际 II 可能受 scale hazard 和 reciprocal 限制。报告必须区分：

```text
MAC阶段II
EXP2阶段II
RECIPROCAL等待窗口
完整指令Latency
完整FA任务级Interval
```

## 11. 不应采用的捷径

1. 不要只给新顶层加 `PIPELINE II=1` 然后忽略 dependence violation；
2. 不要认为 SA 子函数 II=1 自动等于 ExecutionPlan 循环 II=1；
3. 不要通过重复16份完整 SA state 来伪造同一指令的连续状态推进；
4. 不要让控制、valid、地址比浮点结果提前到达；
5. 不要在没有重新计算物理延迟时照搬旧 testbench 的绝对物理周期；
6. 不要删除旧逐-step顶层和测试，它们是新流水实现的重要行为参考；
7. 不要在本任务中修改另一个智能体负责的 Accumulator pipeline 文件。

## 12. 失败回退和实验顺序

如果阶段一内部 loop 的 II 仍为16或更高，这是有效实验结果，不应视为任务失败。

按以下顺序定位：

1. 查看 SA `current->next` RAW dependence；
2. 查看 Accumulator scale和reciprocal dependence；
3. 查看 accRAM RMW地址和数据依赖；
4. 查看聚合状态复制产生的4～8拍循环；
5. 将控制生成、SA token、Accumulator token分别做最小批处理顶层；
6. 先证明单个子流水能连续接收，再接入完整FA。

如果阶段二规模过大，可以先交付“内部顺序 ExecutionPlan + 真实II报告”，作为下一轮
token pipeline 重构的可靠基线。

## 13. 交付物

本任务完成时至少交付：

```text
execution_plan.hpp/.cpp
ExecutionPlan单元测试
新的fsa_core_execute_top及头文件
阶段级和完整FA HLS testbench
独立csim/csynth/cosim Tcl
新的综合报告Markdown
与逐-step基线的周期和结果对照
内部loop dependence分析
Accumulator pipeline集成接口说明
```

最终报告必须明确回答：

1. 消除逐-step事务后，每个阶段减少了多少物理周期；
2. 内部 ExecutionPlan loop 的真实 Final II；
3. II限制来自 SA、Accumulator、SRAM RMW还是状态复制；
4. 哪些阶段已经能连续 token，哪些阶段仍会停顿；
5. 完整FA的总Latency和下一任务可启动间隔。
