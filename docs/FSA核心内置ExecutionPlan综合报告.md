# FSA 核心内置 ExecutionPlan 阶段一报告

## 1. 当前交付状态

阶段一代码已经完成：新顶层 `fsa_core_execute_top` 在一次 `ap_ctrl_hs` 事务内执行
一条完整 `MatrixInstruction`，旧 `fsa_core_top` 继续保留为逐 logical step 金标准。

当前环境没有 `vitis-run` 或 `vitis_hls`，因此本报告只记录已经完成的普通 C++
功能验证，不声明 C/RTL 协同仿真、综合时序、资源或内部 loop Final II 已经通过。
独立脚本 `hls/fsa_core_execute/run_hls.tcl` 已包含 csim、csynth、cosim 和 IP 导出流程，
需要在 Vitis HLS 2024.2 环境补跑。

## 2. 实现结构

新增结构分为三层：

1. `execution_plan.hpp/.cpp`：纯组合生成五种 `MxFunc` 的地址、PE/CMP 控制、
   Accumulator 控制、first/last、semaphore release 和 conflict-free 标记；
2. `fsa_core_datapath.hpp/.cpp`：保存 Scratchpad、Delayer、SA、Accumulator 和 accRAM
   状态，并推进一个 logical step；
3. `fsa_core_execute_top.hpp/.cpp`：内部循环查询 ExecutionPlan 并连续推进 datapath。

4×4 当前计划长度如下：

| Matrix 操作 | logical steps |
|---|---:|
| `LOAD_STATIONARY` | 5 |
| `ATTENTION_SCORE_COMPUTE` | 28 |
| `ATTENTION_VALUE_COMPUTE` | 12 |
| `ATTENTION_LSE_NORM_SCALE` | 17 |
| `ATTENTION_LSE_NORM` | 5 |

顶层仍使用 `ap_ctrl_hs`，但 `instruction_valid=true` 时一次事务不再只推进一个 step。
`instruction_valid=false` 的维护事务只推进一个空闲 step，用于 Scratchpad 预装和 accRAM
调试读回。

## 3. 与 Chisel 的对照

控制表逐项迁移自：

```text
FSA-main/src/main/scala/fsa/ExecutionPlan.scala
FSA-main/src/main/scala/fsa/ControlGen.scala
FSA-main/src/main/scala/fsa/MatrixEngineController.scala
```

地址按每次有效 SRAM/常量读执行 `addr += stride` 的规则等价计算。PE 控制使用与
`ControlGen.flow_up/flow_down/parallel` 相同的行波前定义。Score 的 causal counter、
exp2 slope 顺序、Accumulator RMW 地址均由同一查询函数生成，不再散落在新顶层中。

有一个有意的数值等价优化：当 `instruction.acc.zero=true` 时，旧 L/O 已确定为零，
所以 Score 不发出 `EXP_S1/EXP_S2`。当前 Accumulator PWL 对 `exp2(-inf)` 不作特殊值
处理，继续计算会让后续 `0 * NaN` 污染首块 L；跳过这两条命令等价于直接消去零乘项，
也保持了不修改 Accumulator 内部算法的任务边界。非零旧 L/O 路径仍完整发出两条命令。

## 4. 已完成验证

### 4.1 ExecutionPlan 单元测试

`tests/test_execution_plan.cpp` 检查：

- 五种操作的总 step 数；
- Scratchpad 和 accRAM 地址、stride、zero/RMW；
- Score 全 timer、全 PE 行的九个控制字段；
- CMP 命令和 causal counter；
- Accumulator 命令；
- first/last、release 和 conflict-free 边界；
- 非零旧 L 路径的 `EXP_S1/EXP_S2`。

本地结果：

```text
[PASS] test_execution_plan: five Chisel schedules match
```

### 4.2 新顶层完整 FA 测试

`tests/hls/test_fsa_core_execute_top.cpp` 的执行序列为：

```text
预装 Q/K/V
-> 1次 LOAD_STATIONARY
-> 1次 ATTENTION_SCORE_COMPUTE
-> 1次 ATTENTION_VALUE_COMPUTE
-> 1次 ATTENTION_LSE_NORM_SCALE
-> 1次 ATTENTION_LSE_NORM
-> 读回 L/O 与独立 golden model 比较
```

测试没有逐 step 手工产生任何 PE、CMP 或 Accumulator 控制。本地结果：

```text
[PASS] test_fsa_core_execute_top: five instruction transactions completed full 4x4 FA
```

旧 `test_fsa_core_full.cpp` 也使用相同的本地浮点仿真桩重新运行并通过，说明旧逐-step
参考路径未被新文件破坏。

## 5. 周期与 II：待 Vitis 报告确认

旧基线每个 logical step 的顶层事务 Interval 为 69 拍。仅按事务边界口径，五个阶段
原来分别需要约 345、1932、828、1173 和 345 拍。新顶层已经把每阶段的多次
`ap_start/ap_done` 合并为一次，但物理周期不能用 logical step 数直接替代。

内部循环包含 `state(t+1)=next(t)` 的真实 RAW dependence，并对循环使用不指定目标 II
的 `PIPELINE`，让 HLS 报告实际可达到的 Final II。必须从最新 csynth/cosim 报告填写：

| 项目 | 当前结论 |
|---|---|
| ExecutionPlan 控制生成 II | 待 csynth |
| 内部完整 loop Final II | 待 csynth |
| LOAD 物理 latency | 待 cosim |
| SCORE 物理 latency | 待 cosim |
| VALUE 物理 latency | 待 cosim |
| NORM_SCALE 物理 latency | 待 cosim |
| NORM 物理 latency | 待 cosim |
| 下一条完整任务启动间隔 | 待 csynth/cosim |

预期首先检查的限制顺序为 SA current/next RAW、Accumulator scale/reciprocal、accRAM
RMW，最后才是聚合状态复制。没有综合证据前不宣称 II=1，也不计算虚假的节省比例。

## 6. Accumulator pipeline 集成契约

后续接入快慢路径 Accumulator 时，ExecutionPlan 侧保持以下规则：

```text
生成 acc token
-> input_ready 接受后才跨越依赖点
-> 数据、valid、RMW地址和tag使用相同延迟
-> fast ACC/ACC_SA结果按write_addr写回
-> EXP/RECIPROCAL慢路径等待slow_done
```

当前阶段继续调用原 `accumulator_step()`，没有修改
`src/core/accumulator.cpp`、`include/fsa/accumulator.hpp` 或任何 accumulator pipeline 文件。

## 7. Vitis 2024.2 补跑命令

在已加载 Vitis 环境的项目根目录运行：

```bash
vitis-run --mode hls --tcl hls/fsa_core_execute/run_hls.tcl
```

若环境只提供旧入口：

```bash
vitis_hls -f hls/fsa_core_execute/run_hls.tcl
```

完成后必须从本次构建对应的报告补充 Final II、latency、10 ns 时钟估算、DSP/LUT/FF、
SA/PE 实例数量和 C/RTL transaction 周期，不能沿用旧 `fsa_core_top` 报告的数据。
