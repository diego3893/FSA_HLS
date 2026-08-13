# PE 综合报告

## 1. 综合配置

| 项目 | 配置 |
|---|---|
| HLS 工具 | Vitis HLS 2024.2（Build 5238294） |
| 报告时间 | 2026-08-13 10:34:48 CST |
| 顶层函数 | `pe_top` |
| 目标器件 | `xcvu37p_CIV-fsvh2892-2-e` |
| 目标时钟周期 | 10 ns（100 MHz） |
| Clock Uncertainty | 2.70 ns |
| HLS 有效时序预算 | 7.30 ns |
| 顶层控制协议 | `ap_ctrl_hs` |
| 输入接口 | `input_r`，94 bit，`ap_none` |
| 输出接口 | `output_r`，93 bit，`ap_none` |

本报告依据 `build/pe_build/solution1/` 中的最新 C 仿真、C 综合、C/RTL 协同仿真和 IP 导出结果更新。参与本次构建的源文件修改时间均早于综合报告生成时间。

`cvtAtoE` 当前为固定两拍流水线：

```cpp
#pragma HLS INLINE off
#pragma HLS PIPELINE II=1
#pragma HLS LATENCY min=2 max=2
```

## 2. 流程结果

| 阶段 | 结果 |
|---|---|
| C 仿真 | 通过，0 errors |
| C 综合 | 完成，HLS 时序估算通过 |
| C/RTL 协同仿真 | Verilog `Pass` |
| Vivado IP 导出 | 完成，已生成 `impl/export.zip` |
| Vivado 综合、布局布线 | 未进行 |
| FPGA 上板 | 未验证 |

```text
[PASS] test_pe_top: complete PE functional test
C/RTL co-simulation finished: PASS
```

## 3. 性能结果

### 3.1 PE 顶层

| 项目 | 上次报告值 | 本次构建 |
|---|---:|---:|
| Target Clock Period | 10.000 ns | 10.000 ns |
| HLS 有效时序预算 | 7.300 ns | 7.300 ns |
| Estimated Clock Period | 7.120 ns | **7.120 ns** |
| 估算时序余量 | +0.180 ns | **+0.180 ns** |
| Estimated Fmax | 未记录 | **140.45 MHz** |
| Latency | 1～15 拍 | **1～17 拍** |
| Initiation Interval | 2～16 拍 | **2～18 拍** |
| Pipeline | no | no |

PE 顶层的最大估算延时为 7.120 ns，小于 7.300 ns 有效预算，正余量为 0.180 ns，因此 100 MHz HLS 时序估算通过。余量仍较小，最终结论需以 Vivado 布局布线 WNS 为准。

`pe_top` 仍未做函数级流水化，顶层事务不能每拍启动。由于输入控制可选择简单状态更新或调用浮点 `peMacUnit`，顶层 Latency 和 Interval 均是可变范围。

### 3.2 子模块

| 模块 | Estimated Period | Latency | II | Pipeline | 资源（DSP / FF / LUT） |
|---|---:|---:|---:|---|---:|
| `peMacUnit` | 7.120 ns | 16 拍 | 1 | yes | 17 / 3,416 / 4,456 |
| `peExp2PWL` | 6.846 ns | 13 拍 | 1 | yes | 7 / 1,260 / 2,641 |
| `cvtAtoE` | 7.120 ns | 2 拍 | 1 | yes | 0 / 35 / 2 |

`cvtAtoE` 的报告为 Latency=2、II=1，RTL 寄存器列表中包含两级 16 位转换结果寄存器，说明固定两拍约束已形成真实流水级。

与上次报告相比，`cvtAtoE` 从 1 拍增加到 2 拍，`peMacUnit` 从 14 拍增加到 16 拍，因此 PE 顶层最大 Latency 从 15 拍增加到 17 拍，最大 Interval 从 16 拍增加到 18 拍。

### 3.3 C/RTL 协同仿真

| 项目 | 最小值 | 平均值 | 最大值 |
|---|---:|---:|---:|
| Latency | 1 拍 | 15 拍 | 17 拍 |
| Interval | 2 拍 | 16 拍 | 18 拍 |

- 总执行时间：4,467 拍
- RTL：Verilog
- 状态：`Pass`

## 4. 资源使用

| 资源 | 上次报告值 | 本次构建 | 变化 | 整个器件占比 | 单个 SLR 占比 |
|---|---:|---:|---:|---:|---:|
| BRAM_18K | 0 | 0 | 0 | 0% | 0% |
| DSP | 15 | **17** | +2 | 约 0.19% | 小于 1% |
| FF | 2,747 | **3,629** | +882 | 约 0.14% | 小于 1% |
| LUT | 3,737 | **4,766** | +1,029 | 约 0.37% | 1% |
| URAM | 0 | 0 | 0 | 0% | 0% |

本次顶层资源的主体是单个 `peMacUnit`，其使用 17 DSP、3,416 FF 和 4,456 LUT。PE 顶层的状态更新、选择和控制逻辑使总量增加到 3,629 FF 和 4,766 LUT。

`cvtAtoE` 本身不使用 DSP，因此总 DSP 从 15 增加到 17 不能简单归因于增加一拍转换寄存器。当前 `peMacUnit` 同时包含普通 MAC 和 exp2 路径的浮点运算资源，调度和资源共享结果与上次构建不同。

## 5. 功能验证范围

当前完整 testbench 覆盖：

- 复位、无效控制和状态保持；
- 三个方向的数据流；
- 两种 `PE.reg` 装载方式；
- 向上和向下 MAC；
- MAC 结果写回；
- 8 段 PWL exp2 及分段边界；
- `exp2Done` 和 MAC/exp2 模式切换。

C/RTL 协同仿真通过说明当前测试输入下 C 模型与 RTL 行为一致，但尚不等于已覆盖 NaN、Inf、非规格化数等全部 IEEE 边界。

## 6. 警告与结论

本次构建存在以下非致命警告：

- `output_r` 使用 `ap_none`，HLS 提醒与其他模块交互时可能需要关联的 data-valid 信号。当前 C/RTL 协同仿真已通过。
- `current_reg` 和 `current_exp2Done` 被报告为 power-on initialization，最终集成时需验证复位和初始状态。
- 浮点 IP 继续报告 17/16 bit 和 33/32 bit 的 `m_axis_result_tdata` 端口位宽差异。当前测试未发现功能错误，但集成前仍应确认多出位的语义。

当前结论：

> PE 的 C 仿真、C 综合、C/RTL 协同仿真和 Vivado IP 导出均已完成。功能与 C/RTL 一致性验证通过，顶层 HLS 估算周期为 7.120 ns，在 7.300 ns 有效预算内。`cvtAtoE` 已形成固定 2 拍、II=1 的流水级，`peMacUnit` 为固定 16 拍、II=1。与上次报告相比，延迟和资源均增加；最终 100 MHz 时序仍需 Vivado 布局布线确认。

## 7. 后续工作

1. 保留当前 PE 构建作为两拍 `cvtAtoE` 的功能与时序基准。
2. 定位 `peMacUnit` 中 DSP 从 15 增加到 17、LUT 增加的调度原因，评估互斥 MAC/exp2 路径的资源共享机会。
3. 为浮点转换和 exp2 路径增加 NaN、Inf、正负零、非规格化数和溢出边界测试。
4. 将导出 IP 加入 Vivado 工程，完成综合、布局布线并检查 WNS。

## 8. 结果文件

```text
build/pe_build/solution1/csim/report/pe_top_csim.log
build/pe_build/solution1/syn/report/pe_top_csynth.rpt
build/pe_build/solution1/syn/report/peMacUnit_csynth.rpt
build/pe_build/solution1/syn/report/peExp2PWL_csynth.rpt
build/pe_build/solution1/syn/report/cvtAtoE_csynth.rpt
build/pe_build/solution1/sim/report/pe_top_cosim.rpt
build/pe_build/solution1/sim/report/verilog/pe_top.log
build/pe_build/solution1/solution1.log
build/pe_build/solution1/impl/export.zip
```

重新运行 PE HLS 流程：

```bash
./run_hls.sh pe
```
