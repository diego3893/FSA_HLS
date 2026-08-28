# 4×4 SystolicArray 综合报告

## 1. 综合配置

| 项目 | 配置 |
|---|---|
| HLS 工具 | Vitis HLS 2024.2（Build 5238294） |
| C 综合报告时间 | 2026-08-13 15:43:10 CST |
| C/RTL 协同仿真时间 | 2026-08-13 15:46:06 CST |
| IP 导出时间 | 2026-08-13 15:46:53 CST |
| 顶层函数 | `systolic_array_top` |
| 目标器件 | `xcvu37p_CIV-fsvh2892-2-e` |
| 目标时钟周期 | 10 ns（100 MHz） |
| Clock Uncertainty | 2.70 ns |
| HLS 有效时序预算 | 7.30 ns |
| 顶层流水线目标 | `II=16` |
| 顶层控制协议 | `ap_ctrl_hs` |
| 输入接口 | `input_r`，122 bit，`ap_none` |
| 输出接口 | `output_r`，132 bit，`ap_none` |

本报告依据 `build/sa_build/solution1/` 中15:42—15:47生成的最新 C 仿真、C 综合、C/RTL 协同仿真和 IP 导出结果更新。构建数据库记录的顶层 `II=16` 和 `peExp2PWL` 固定13拍约束均与当前工作区源码一致。

关键延迟约束如下：

```cpp
// peExp2PWL
#pragma HLS INLINE off
#pragma HLS PIPELINE II=1
#pragma HLS LATENCY min=13 max=13

// cvtAtoE
#pragma HLS INLINE off
#pragma HLS PIPELINE II=1
#pragma HLS LATENCY min=2 max=2
```

## 2. 流程结果

| 阶段 | 结果 |
|---|---|
| C 仿真 | 通过，0 errors |
| C 综合 | 完成，HLS 估算时序通过 |
| C/RTL 协同仿真 | Verilog `Pass` |
| Vivado IP 导出 | 完成，已生成 `impl/export.zip` |
| 构建与当前源码一致性 | 一致，构建数据库记录为顶层 II=16、`peExp2PWL` Latency=13 |
| Vivado 综合、布局布线 | 未进行 |
| FPGA 上板 | 未验证 |

C 仿真输出：

```text
S = Q * K^T
  [11, 13, 8, 8]  rowmax=13
  [3, 3, 6, 4]    rowmax=6
  [3, 1, 2, 2]    rowmax=3
  [5, 3, 8, 8]    rowmax=8
[PASS] test_systolic_array_top: 4x4 complete test
```

## 3. 性能结果

### 3.1 SA 顶层

| 项目 | 14:55 构建（12拍/II=15） | 15:43 构建（13拍/II=16） |
|---|---:|---:|
| Target Clock Period | 10.000 ns | 10.000 ns |
| HLS 有效时序预算 | 7.300 ns | 7.300 ns |
| Estimated Clock Period | 13.966 ns | **7.150 ns** |
| 估算时序余量 | -6.666 ns | **+0.150 ns** |
| Estimated Fmax | 71.60 MHz | **139.86 MHz** |
| Latency | 15 拍 | **17 拍** |
| Final II | 15 | **16** |
| Pipeline | yes | yes |

将 `peExp2PWL` 固定延迟从12拍增加到13拍，并把顶层目标恢复为 `II=16` 后，顶层重新形成16个独立 `peMacUnit` 实例。此前同一拍内串联的 `peExp2PWL -> cvtAtoE` 组合路径被模块流水线边界切开，顶层估算周期由13.966 ns降至7.150 ns。

本次估算周期比7.300 ns有效预算少0.150 ns，日志中没有 `HLS 200-871` 时序违例警告，因此可以判定为“100 MHz HLS 估算时序通过”。该结论仍不等同于 Vivado 布局布线后的最终 WNS。

### 3.2 子模块

| 子模块 | SA 层次中的实例数 | Estimated Period | Latency | II | Pipeline | 单实例资源（DSP/FF/LUT） |
|---|---:|---:|---:|---:|---|---:|
| `peMacUnit` | 16 | 7.120 ns | 16 拍 | 1 | yes | 17 / 3,416 / 4,456 |
| `peExp2PWL` | 每个 `peMacUnit` 内1个 | 6.846 ns | 13 拍 | 1 | yes | 7 / 1,260 / 2,641 |
| `cvtAtoE` | 每个 `peMacUnit` 内2个，顶层另有1个 | 7.120 ns | 2 拍 | 1 | yes | 0 / 35 / 2 |

`peExp2PWL` 调度日志显示 Pipeline Depth=14，综合报告中的对外 Latency=13、II=1。两者采用不同计数口径；与函数调用者对齐时，应使用综合报告给出的13拍 Latency。

顶层报告明确列出16个独立 `peMacUnit`，因此4×4 PE阵列的空间展开不仅由顶层 II推断，也有 RTL层次和资源规模佐证。

### 3.3 C/RTL 协同仿真

| 项目 | 最小值 | 平均值 | 最大值 |
|---|---:|---:|---:|
| Latency | 17 拍 | 17 拍 | 17 拍 |
| Interval | 16 拍 | 16 拍 | 16 拍 |

- 总执行时间：401拍
- RTL：Verilog
- 仿真器：XSIM
- 状态：`Pass`

协同仿真的17拍 Latency 和16拍 Interval 与 C综合报告一致。它证明当前测试向量下 RTL 与 C模型行为一致，但不代替 Vivado 实现时序验证。

## 4. 资源使用

| 资源 | 14:55 构建 | 15:43 构建 | 变化 | 整个器件占比 | 单个 SLR 占比 |
|---|---:|---:|---:|---:|---:|
| BRAM_18K | 0 | 0 | 0 | 0% | 0% |
| DSP | 137 | **274** | +137 | 3% | 9% |
| FF | 29,610 | **59,266** | +29,656 | 2% | 6% |
| LUT | 54,564 | **77,960** | +23,396 | 5% | 17% |
| URAM | 0 | 0 | 0 | 0% | 0% |

DSP 总数与16个独立 PE MAC单元吻合：

```text
16 × peMacUnit（17 DSP） = 272 DSP
顶层 CMP 浮点减法        =   2 DSP
-----------------------------------
合计                    = 274 DSP
```

与低资源的14:55构建相比，本次 DSP、FF和LUT明显增加。代价换来的是独立 `peMacUnit` 流水线边界以及HLS估算时序重新通过。当前数据反映的是“更多资源换取可调度的时序边界”，并非算法功能发生变化。

## 5. 当前合格性结论

| 检查项 | 结论 |
|---|---|
| 矩阵乘与 `rowmax` 功能 | 合格 |
| C/RTL 行为一致性 | 合格，Verilog `Pass` |
| 构建与当前源码版本 | 一致 |
| 4×4 PE空间展开 | 合格，顶层有16个独立 `peMacUnit` |
| `peExp2PWL` 固定13拍、II=1 | 生效 |
| `cvtAtoE` 固定2拍、II=1 | 生效 |
| 顶层目标 II=16 | 达到 |
| 100 MHz HLS时序估算 | **通过，正余量0.150 ns** |
| Vivado IP导出 | 完成 |
| 最终实现时序 | 未验证 |
| FPGA上板 | 未验证 |

综合结论：

> 当前4×4 SA的 C仿真、C综合、C/RTL协同仿真和 Vivado IP导出均已完成，且构建约束与当前源码一致。`peExp2PWL` 的综合 Latency 为13拍、II=1；顶层 Latency 为17拍、II=16。16个独立 `peMacUnit` 使资源增加到274 DSP、59,266 FF和77,960 LUT，但顶层估算周期降至7.150 ns，以0.150 ns正余量通过100 MHz HLS时序估算。因此该版本可以作为当前“功能、协同仿真和HLS估算时序均通过”的基准，但最终频率仍需 Vivado布局布线确认。

## 6. 问题与下一步

1. 当前HLS正余量只有0.150 ns，仍较紧，应在后续修改中持续监控 Estimated Period，避免重新出现违例。
2. 将导出IP加入 Vivado工程，完成综合、布局布线并检查 WNS，确认100 MHz最终实现时序。
3. HLS对 `output_r` 的 `ap_none` 接口给出数据有效信号警告，系统集成时需明确输出采样周期或配套 valid信号。
4. 多个静态状态寄存器使用 power-on initialization，最终集成时需继续验证复位和初始化行为。
5. 若需要再次降低资源，应保留 `peExp2PWL` 与 `cvtAtoE` 之间的流水线边界，避免回到13.966 ns组合关键路径。

## 7. 结果文件

```text
build/sa_build/solution1/csim/report/systolic_array_top_csim.log
build/sa_build/solution1/syn/report/systolic_array_top_csynth.rpt
build/sa_build/solution1/syn/report/peMacUnit_csynth.rpt
build/sa_build/solution1/syn/report/peExp2PWL_csynth.rpt
build/sa_build/solution1/syn/report/cvtAtoE_csynth.rpt
build/sa_build/solution1/sim/report/systolic_array_top_cosim.rpt
build/sa_build/solution1/solution1.log
build/sa_build/solution1/impl/export.zip
```

重新运行 SA HLS 流程：

```bash
./run_hls.sh sa
```
