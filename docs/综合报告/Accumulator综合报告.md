# Accumulator 综合报告

## 1. 综合配置

| 项目 | 配置 |
|---|---|
| HLS 工具 | Vitis HLS 2024.2（Build 5238294） |
| C 综合时间 | 2026-08-18 14:49:31 CST |
| 顶层函数 | `accumulator_top` |
| 构建目录 | `build/accumulator_build/solution1` |
| 目标器件 | `xcvu37p_CIV-fsvh2892-2-e` |
| 目标时钟周期 | 10 ns（100 MHz） |
| Clock Uncertainty | 2.70 ns |
| HLS 有效时序预算 | 7.30 ns |
| 顶层控制协议 | `ap_ctrl_hs` |
| 输入接口 | `input_r`，266 bit，`ap_none` |
| 输出接口 | `output_r`，128 bit，`ap_none` |
| 数据列数 | 4 |

本报告依据 `build/accumulator_build/solution1/` 中 2026-08-18 14:49 后生成的最新 C 仿真、C 综合、C/RTL 协同仿真和 IP 导出结果更新。最新源码修改时间早于本次构建，构建版本与当前源码一致。

## 2. 流程结果

| 阶段 | 结果 |
|---|---|
| C 仿真 | 通过，0 errors |
| C 综合 | 完成，HLS 时序估算通过 |
| C/RTL 协同仿真 | Verilog `Pass` |
| Vivado IP 导出 | 完成，已生成 `impl/export.zip` |
| Vivado 综合、布局布线 | 未进行 |
| FPGA 上板 | 未验证 |

当前 HLS 脚本启用了 C 仿真、协同仿真和 IP 导出。C 仿真输出：

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
| Estimated Clock Period | **6.516 ns** |
| 估算时序余量 | **+0.784 ns** |
| Estimated Fmax | 153.47 MHz |
| Latency | 1～18 拍 |
| Initiation Interval | 2～19 拍 |
| Pipeline | no |

顶层估算周期为 6.516 ns，小于 7.300 ns 有效预算，日志中没有 `HLS 200-871` 时序违例，因此 100 MHz HLS 时序估算通过。最终频率仍需以 Vivado 布局布线后的 WNS 为准。

`accumulator_top` 仍是非流水 `ap_ctrl_hs` 事务。不同命令的延迟不同，系统必须等待 `ap_done`，不能假定每拍都能启动新事务。

### 3.2 单列计算模块

| 模块 | Estimated Period | Latency | Interval | Pipeline | DSP | FF | LUT |
|---|---:|---:|---:|---|---:|---:|---:|
| `accumulator_lane_step` | 6.516 ns | 6～17 拍 | 6～17 拍 | no | 5 | 1,599 | 5,044 |
| `begin_reciprocal` | 6.394 ns | 1 拍 | 1 拍 | no | 0 | 169 | 783 |
| `normalize_and_round` | 5.016 ns | 0 拍 | 0 拍 | no | 0 | 0 | 1,000 |

当前版本把普通 FP32 乘加、8 段 `accExp2PWL` 和 reciprocal 单步逻辑都放入每列独立的 `accumulator_lane_step`。`accExp2PWL` 与 `divider_tick` 已内联，不再生成独立综合报告或独立 RTL 模块，因此上一构建中 `accExp2PWL` 独立模块的 15 拍 latency 已不再适用于当前硬件层次。

### 3.3 四列并行结构

源码为四个调用点传入常量 lane 编号，并使用：

```cpp
#pragma HLS INLINE off
#pragma HLS FUNCTION_INSTANTIATE variable=lane
```

综合报告和顶层 RTL 均显示 4 个 `accumulator_lane_step` 实例。每个实例包含：

- 1 个 FP32 加减单元，使用 2 DSP；
- 1 个 FP32 乘法单元，使用 3 DSP；
- 1 套 PWL 查表和组合逻辑；
- 1 套独立 reciprocal 状态推进与舍入逻辑。

因此四列已从上一构建的共享计算 datapath 改为四套空间并行 datapath。四列 `EXP_S2` 同时计算，不再串行复用单个 PWL 实例。

### 3.4 C/RTL 协同仿真

| 项目 | 最小值 | 平均值 | 最大值 |
|---|---:|---:|---:|
| Latency | 1 拍 | 7 拍 | 17 拍 |
| Interval | 2 拍 | 8 拍 | 18 拍 |

- 总执行时间：520 拍
- RTL：Verilog
- 仿真器：XSIM
- 状态：`Pass`

逐事务记录与 testbench 调用顺序对应如下：

| 事务类型 | 协同仿真 Latency | Interval |
|---|---:|---:|
| reset | 1 拍 | 2 拍 |
| 多数普通状态推进 | 7 拍 | 8 拍 |
| reciprocal 启动 | 8 拍 | 9 拍 |
| `EXP_S2`（编号 8，第 9 次顶层调用） | **17 拍** | **18 拍** |

`EXP_S2` 的当前顶层实测 latency 为 17 拍，位于 C 综合给出的最坏 18 拍上界内。若外部控制必须使用固定预算，应至少按综合上界 18 拍预留；更稳妥的方式是等待 `ap_done`。

reciprocal 的 `reciprocalLatency=15` 仍表示 1 次启动、13 次 ITER 和 1 次 DONE，共 15 个**逻辑 step**，不是 15 个物理时钟周期。每个逻辑 step 都对应一次多拍顶层事务。

## 4. 资源使用

| 资源 | 本次构建 | 整个器件可用 | 整个器件占比 | 单个 SLR 占比（HLS 报告） |
|---|---:|---:|---:|---:|
| BRAM_18K | 0 | 4,032 | 0% | 0% |
| DSP | **20** | 9,024 | 约 0.22% | 小于 1% |
| FF | **8,236** | 2,607,360 | 约 0.32% | 小于 1% |
| LUT | **20,633** | 1,303,680 | 约 1.58% | 4% |
| URAM | 0 | 960 | 0% | 0% |

四个 lane 实例的资源完全一致：

| 结构 | 数量 | 单实例资源 | 小计 |
|---|---:|---:|---:|
| `accumulator_lane_step` | 4 | 5 DSP、1,599 FF、5,044 LUT | 20 DSP、6,396 FF、20,176 LUT |

其余资源来自顶层状态寄存器、多路选择器和少量控制逻辑。每个 lane 内部有 3 个小型 PWL ROM，使用 96 FF、99 LUT、1,144 bit；四列共 12 个 ROM，合计 384 FF、396 LUT、4,576 bit，未使用 BRAM 或 URAM。

与上一构建相比：

| 项目 | 上一构建（共享 datapath） | 当前构建（4 lane） | 变化 |
|---|---:|---:|---:|
| 最大 Latency | 80 拍 | 18 拍 | 降低 62 拍 |
| 最大 Interval | 81 拍 | 19 拍 | 降低 62 拍 |
| DSP | 10 | 20 | +10 |
| FF | 4,711 | 8,236 | +3,525 |
| LUT | 7,067 | 20,633 | +13,566 |

当前实现以更高资源占用换取四列并行和显著更低的事务延迟。其中 LUT 增长最明显，需要在 Vivado 实现阶段继续检查布线和 WNS。

## 5. 功能验证范围

当前 C testbench 只通过正式 `accumulator_top` 接口覆盖：

- 显式复位及复位当拍零输出；
- `ctrl.valid=false` 时的状态保持；
- `SET_SCALE` 对四列 scale 的跨调用保存；
- `ACC`：`scale * sram_in`；
- `ACC_SA`：`scale * sram_in + sa_in`；
- `EXP_S1`：注意力缩放前处理；
- `EXP_S2`：8 段 PWL `exp2` 及结果写回 scale；
- `RECIPROCAL`：普通有限数、正负零、Infinity 和负数；
- 一次 `RECIPROCAL` 启动、13 次 ITER 和 1 次 DONE，共固定 15 个逻辑 step；
- reciprocal 运行中复位可取消多周期状态。

上述 testbench 已用于 Verilog C/RTL 协同仿真并通过，说明当前测试向量下 RTL 与 C 模型行为一致。

## 6. 警告与风险

本次综合的主要非致命警告为：

1. `accumulator.cpp:202` 的变量索引位段选择可能导致较差 QoR，位置在 reciprocal 次规格化结果的舍入处理中。
2. `std::array<float, 4>` 索引辅助函数因调用签名差异被 HLS 复制，可能增加资源。
3. HLS 对匿名命名空间中的辅助函数进行了合法化重命名；这是名称处理警告，不影响功能。
4. `output_r` 使用 `ap_none`，输出有效时刻必须结合 `ap_done` 判断。
5. `current.scale` 和 reciprocal 状态寄存器被报告为 power-on initialization，最终集成时需核对复位与初始状态行为。
6. XSIM 检测到 `LIBRARY_PATH` 环境变量可能影响 C 编译器；本次协同仿真仍正常通过。
7. 当前仍没有 Vivado 布局布线数据；HLS 估算不能代替最终 WNS。

## 7. 当前合格性结论

| 检查项 | 结论 |
|---|---|
| C 模型功能 | 合格 |
| 四列独立状态与并行 datapath | 综合层次显示 4 个 lane，合格 |
| 固定 15 个逻辑 step reciprocal 控制 | C 仿真和 C/RTL 协同仿真合格 |
| C 综合 | 完成 |
| 100 MHz HLS 时序估算 | 通过，正余量 0.784 ns |
| C/RTL 行为一致性 | 合格，Verilog `Pass` |
| Vivado IP 导出 | 完成 |
| 顶层固定低延迟/逐拍吞吐 | 最大 Latency/Interval 降至 18/19 拍，但仍非流水 |
| 最终实现时序 | **未验证** |
| FPGA 上板 | **未验证** |

综合结论：

> 当前 4 列 Accumulator 已完成 C 仿真、C 综合、Verilog C/RTL 协同仿真和 Vivado IP 导出。HLS 估算周期为 6.516 ns，在 7.300 ns 有效预算内。四列现在各有一个完整 `accumulator_lane_step` 实例，顶层最大 Latency/Interval 从上一构建的 80/81 拍降至 18/19 拍，`EXP_S2` 协同仿真实测为 17/18 拍；相应资源增加到 20 DSP、8,236 FF 和 20,633 LUT。最终 100 MHz 时序仍需 Vivado 布局布线确认。

## 8. 后续工作

1. 当前不存在独立 `accExp2PWL` RTL latency；若调度的是 `accumulator_top` 的 `EXP_S2` 命令，应使用 `ap_done`，固定预算至少按 18 拍预留。
2. 在系统级调度中保留 `ap_ctrl_hs` 握手，因为当前顶层仍非流水，最大 Interval 为 19 拍。
3. 优化 `accumulator.cpp:202` 的变量索引位段选择，并评估四套 lane 带来的 LUT 增长。
4. 在顶层集成中确认 `output_r` 的 `ap_none` 时序语义与下游 SRAM 写入控制匹配。
5. 将导出 IP 加入 Vivado 工程，完成综合、布局布线并检查 100 MHz WNS 和跨 lane 布线压力。

## 9. 结果文件

```text
build/accumulator_build/solution1/csim/report/accumulator_top_csim.log
build/accumulator_build/solution1/syn/report/accumulator_top_csynth.rpt
build/accumulator_build/solution1/syn/report/p_anonymous_namespace_accumulator_lane_step_csynth.rpt
build/accumulator_build/solution1/syn/report/p_anonymous_namespace_begin_reciprocal_csynth.rpt
build/accumulator_build/solution1/syn/report/p_anonymous_namespace_normalize_and_round_csynth.rpt
build/accumulator_build/solution1/sim/report/accumulator_top_cosim.rpt
build/accumulator_build/solution1/sim/report/verilog/result.transaction.rpt
build/accumulator_build/solution1/sim/report/verilog/accumulator_top.log
build/accumulator_build/solution1/solution1.log
build/accumulator_build/solution1/impl/export.zip
```

重新运行 Accumulator HLS 流程：

```bash
./run_hls.sh accumulator
```
