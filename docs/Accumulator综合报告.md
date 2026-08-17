# Accumulator 综合报告

> **MOD（2026-08-17）**：本报告记录的是修改前的 baseline 构建。源码现已改为
> “四列 complete UNROLL + RECIPROCAL 单次事务内部15阶段 + 显式协议包装层”。
> 下文 8～23 拍、II 9～24、15 DSP 和 359 拍推导仅用于旧版对照，不能作为
> 当前源码的综合结论。新源码尚需在 Vitis HLS 2024.2 重新执行 C synthesis、
> C/RTL co-simulation，并以新报告确认顶层 reciprocal latency、实例数和资源。

## 1. 综合配置

| 项目 | 配置 |
|---|---|
| HLS 工具 | Vitis HLS 2024.2（Build 5238294） |
| 报告时间 | 2026-08-13 11:47:56 CST |
| 顶层函数 | `accumulator_top` |
| 目标器件 | `xcvu37p_CIV-fsvh2892-2-e` |
| 目标时钟周期 | 10 ns（100 MHz） |
| Clock Uncertainty | 2.70 ns |
| HLS 有效时序预算 | 7.30 ns |
| 顶层控制协议 | `ap_ctrl_hs` |
| 输入接口 | `input_r`，266 bit，`ap_none` |
| 输出接口 | `output_r`，128 bit，`ap_none` |
| 数据列数 | 4 |

本报告依据 `build/accumulator_build/solution1/` 中的最新 C 仿真、C 综合、C/RTL 协同仿真和 IP 导出结果更新。参与本次构建的源文件修改时间均早于综合报告生成时间。

## 2. 流程结果

| 阶段 | 结果 |
|---|---|
| C 仿真 | 通过，0 errors |
| C 综合 | 完成，HLS 时序估算通过 |
| C/RTL 协同仿真 | Verilog `Pass` |
| Vivado IP 导出 | 完成，已生成 `impl/export.zip` |
| Vivado 综合、布局布线 | 未进行 |
| FPGA 上板 | 未验证 |

当前 HLS 脚本设置为：

```tcl
set RUN_CSIM  1
set RUN_COSIM 1
set EXPORT_IP 1
```

当前流程已完成 C 仿真、C 综合、Verilog C/RTL 协同仿真和 Vivado IP 导出。

C 仿真结果：

```text
[PASS] test_accumulator_top: RESET, invalid, SET_SCALE,
ACC, ACC_SA, EXP_S1, EXP_S2, RECIPROCAL
```

## 3. 性能结果

### 3.1 Accumulator 顶层

| 项目 | 结果 |
|---|---:|
| Target Clock Period | 10.000 ns |
| HLS 有效时序预算 | 7.300 ns |
| Estimated Clock Period | **6.819 ns** |
| 估算时序余量 | **+0.481 ns** |
| Estimated Fmax | 146.65 MHz |
| Latency | 8～23 拍 |
| Initiation Interval | 9～24 拍 |
| Pipeline | no |

顶层估算周期为 6.819 ns，小于 7.300 ns 有效预算，正余量为 0.481 ns，因此 100 MHz HLS 时序估算通过。它仍只是 HLS 估算，最终频率需以 Vivado 布局布线 WNS 为准。

`accumulator_top` 未做函数级流水化。复位、普通命令和内部多周期操作走不同的控制路径，所以顶层 Latency 为 8～23 拍，Interval 为 9～24 拍。

### 3.2 子模块与关键循环

| 模块或循环 | Estimated Period | Latency | II / Interval | Pipeline |
|---|---:|---:|---:|---|
| `accumulator_step` | 6.819 ns | 20 拍 | Interval=20 | no |
| 四列复位循环 `VITIS_LOOP_295_1` | 1.346 ns | 迭代循环 4 拍，包装模块 6 拍 | II=1，模块 Interval=5 | yes |
| 四列计算循环 `VITIS_LOOP_315_1` | 归属 `accumulator_step` | 18 拍 | II=1 | yes |
| `begin_reciprocal` | 6.394 ns | 1 拍 | II=1 | yes |
| `normalize_and_round` | 5.016 ns | 0 拍 | II=1 | yes |

`VITIS_LOOP_315_1` 的 Trip Count 为 4，单次迭代延迟为 16 拍，通过 II=1 的循环流水线处理四列，整个循环延迟为 18 拍。

这里的“循环 II=1”只表示相邻列的迭代可每拍启动，不代表 Accumulator 顶层可每拍接收新事务。顶层仍是 `Pipeline=no`，最小 Interval 为 9 拍。

### 3.3 四列综合结构

综合日志中生成的主要浮点运算单元为：

| 运算单元 | 实例数 | 单实例 DSP | DSP 小计 |
|---|---:|---:|---:|
| FP32 加法 | 2 | 2 | 4 |
| FP32 乘法 | 3 | 3 | 9 |
| FP32 减法 | 1 | 2 | 2 |
| **合计** | 6 | - | **15** |

资源总数表明，HLS 没有为四列各复制一套完整浮点 datapath，而是通过 II=1 的列循环流水线在连续拍处理四列。因此，不能只根据数组 `ARRAY_PARTITION` 判断四列是四套算术硬件的完全空间并行。

### 3.4 C/RTL 协同仿真

| 项目 | 最小值 | 平均值 | 最大值 |
|---|---:|---:|---:|
| Latency | 6 拍 | 22 拍 | 23 拍 |
| Interval | 7 拍 | 23 拍 | 24 拍 |

- 总执行时间：1,525 拍
- RTL：Verilog
- 仿真器：XSIM
- 状态：`Pass`

协同仿真的最大 Latency/Interval 为 23/24 拍，与 C 综合报告上界一致。协同仿真观测到的最小值为 6/7 拍，比 C 综合摘要的 8/9 拍更小；这不影响最坏延迟与事务间隔的判定。

## 4. 资源使用

| 资源 | 本次构建 | 整个器件可用 | 整个器件占比 | 单个 SLR 占比（HLS 报告） |
|---|---:|---:|---:|---:|
| BRAM_18K | 0 | 4,032 | 0% | 0% |
| DSP | **15** | 9,024 | 约 0.17% | 小于 1% |
| FF | **5,636** | 2,607,360 | 约 0.22% | 小于 1% |
| LUT | **8,052** | 1,303,680 | 约 0.62% | 1% |
| URAM | 0 | 960 | 0% | 0% |

顶层实例资源中，`accumulator_step` 占 15 DSP、3,406 FF 和 7,459 LUT；四列复位循环包装模块占 5 FF 和 49 LUT。加上顶层寄存器与多路选择器后，总量为 5,636 FF 和 8,052 LUT。

`accumulator_step` 内的 3 组小型 ROM 被实现为分布式 FF/LUT，未占用 BRAM。

## 5. 功能验证范围

当前 C testbench 通过正式 `accumulator_top` 接口覆盖：

- 显式复位及复位当拍零输出；
- `ctrl.valid=false` 时的状态保持；
- `SET_SCALE` 对四列 scale 的跨调用保存；
- `ACC`：`scale * sram_in`；
- `ACC_SA`：`scale * sram_in + sa_in`；
- `EXP_S1`：注意力缩放前处理；
- `EXP_S2`：PWL `exp2` 及结果写回 scale；
- `RECIPROCAL`：普通有限数、正负零、Infinity 和负数；
- 单拍 `RECIPROCAL` 启动后的固定 15 个逻辑 step 控制窗口；
- 倒数运行中复位可取消多周期状态。

上述 testbench 已用于 Verilog C/RTL 协同仿真并通过，说明当前测试向量下 RTL 与 C 模型行为一致。

## 6. 警告与风险

本次综合的主要非致命警告为：

1. `accumulator.cpp:202` 的变量索引位段选择可能导致较差 QoR，它位于倒数结果的次规格化舍入处理中。
2. `std::array<float, 4>` 索引辅助函数因调用签名差异被 HLS 复制，可能影响资源 QoR。
3. `output_r` 使用 `ap_none`，HLS 提醒与其他模块交互时可能需要关联的 data-valid 信号。
4. `current.scale` 和四列 reciprocal 状态寄存器被报告为 power-on initialization，最终集成时需核对复位与初始状态行为。
5. XSIM 检测到 `LIBRARY_PATH` 环境变量可能影响 C 编译器；本次协同仿真仍正常通过。
6. 当前仍没有 Vivado 布局布线数据。

## 7. 当前合格性结论

| 检查项 | 结论 |
|---|---|
| C 模型功能 | 合格 |
| 跨顶层调用状态保存 | C 仿真和 C/RTL 协同仿真合格 |
| 固定 15 个逻辑 step 倒数控制 | C 仿真和 C/RTL 协同仿真合格 |
| C 综合 | 完成 |
| 循环约束 | 全部满足 |
| 100 MHz HLS 时序估算 | 通过，正余量 0.481 ns |
| C/RTL 行为一致性 | 合格，Verilog `Pass` |
| Vivado IP 导出 | 完成 |
| 最终实现时序 | **未验证** |
| FPGA 上板 | **未验证** |

综合结论：

> 当前 4 列 Accumulator 的 C 仿真、C 综合、Verilog C/RTL 协同仿真和 Vivado IP 导出已完成。Testbench 覆盖 RESET、invalid、SET_SCALE、ACC、ACC_SA、EXP_S1、EXP_S2 和 RECIPROCAL，C 仿真与 C/RTL 协同仿真均通过。顶层 HLS 估算周期为 6.819 ns，在 7.300 ns 有效预算内；顶层最大 Latency 为 23 拍，最大 Interval 为 24 拍。四列核心计算通过 Trip Count=4、II=1 的循环流水线处理，使用 15 DSP、5,636 FF 和 8,052 LUT。当前版本可作为“功能、C/RTL 一致性、HLS 时序估算和 IP 导出均通过”的基准；最终 100 MHz 时序仍需 Vivado 布局布线确认。

## 8. 后续工作

1. 保留当前构建作为 Accumulator 功能、C/RTL 一致性和 HLS 时序通过的基准。
2. 在顶层集成中确认 `output_r` 的 `ap_none` 时序语义与下游 SRAM 写入控制匹配。
3. 评估变量索引位段选择和 `std::array` 辅助函数复制对 LUT 的影响。
4. 将导出 IP 加入 Vivado 工程，完成综合、布局布线并检查 WNS。

## 9. 结果文件

```text
build/accumulator_build/solution1/csim/report/accumulator_top_csim.log
build/accumulator_build/solution1/syn/report/accumulator_top_csynth.rpt
build/accumulator_build/solution1/syn/report/accumulator_step_csynth.rpt
build/accumulator_build/solution1/syn/report/accumulator_top_Pipeline_VITIS_LOOP_295_1_csynth.rpt
build/accumulator_build/solution1/syn/report/p_anonymous_namespace_begin_reciprocal_csynth.rpt
build/accumulator_build/solution1/syn/report/p_anonymous_namespace_normalize_and_round_csynth.rpt
build/accumulator_build/solution1/sim/report/accumulator_top_cosim.rpt
build/accumulator_build/solution1/sim/report/verilog/accumulator_top.log
build/accumulator_build/solution1/solution1.log
build/accumulator_build/solution1/impl/export.zip
```

重新运行 Accumulator HLS 流程：

```bash
./run_hls.sh accumulator
```
