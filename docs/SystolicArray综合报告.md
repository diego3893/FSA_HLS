# 4×4 SystolicArray 综合报告

## 1. 综合配置

| 项目 | 配置 |
|---|---|
| HLS 工具 | Vitis HLS 2024.2（Build 5238294） |
| 报告时间 | 2026-08-13 09:12:52 CST |
| 顶层函数 | `systolic_array_top` |
| 目标器件 | `xcvu37p_CIV-fsvh2892-2-e` |
| 目标时钟周期 | 10 ns（100 MHz） |
| Clock Uncertainty | 2.70 ns |
| HLS 有效时序预算 | 7.30 ns |
| 顶层流水线目标 | `II=16` |
| 顶层控制协议 | `ap_ctrl_hs` |
| 数据端口协议 | `input_r` / `output_r` 为 `ap_none` |

本报告依据 `build/sa_build/solution1/` 中的最新 C 仿真、C 综合、C/RTL 协同仿真和 IP 导出结果更新。参与本次构建的源文件修改时间均早于综合报告生成时间。

`cvtAtoE` 保持固定两拍流水线：

```cpp
#pragma HLS INLINE off
#pragma HLS PIPELINE II=1
#pragma HLS LATENCY min=2 max=2
```

## 2. 流程结果

| 阶段 | 结果 |
|---|---|
| C 仿真 | 通过，0 errors |
| C 综合 | 完成，但顶层存在 0.007 ns 的 HLS 估算时序违例 |
| C/RTL 协同仿真 | Verilog `Pass` |
| Vivado IP 导出 | 完成，已生成 `impl/export.zip` |
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

| 项目 | 上次报告值 | 本次构建 |
|---|---:|---:|
| Target Clock Period | 10.000 ns | 10.000 ns |
| HLS 有效时序预算 | 7.300 ns | 7.300 ns |
| Estimated Clock Period | 7.150 ns | **7.307 ns** |
| 估算时序余量 | +0.150 ns | **-0.007 ns** |
| Estimated Fmax | 未记录 | 136.86 MHz |
| Latency | 17 拍 | 17 拍 |
| Final II | 16 | 16 |
| Pipeline | yes | yes |

顶层的最大估算延时为 7.307 ns，比 7.300 ns 有效预算多 0.007 ns。Vitis HLS 明确输出了 `HLS 200-871` 警告，因此本次构建不能判定为“100 MHz HLS 时序通过”。

关键路径为：

```text
current.cmp_d_3 读取
  -> pe.cpp 中 mac_in_c 选择（0.227 ns）
  -> peMacUnit 调用（7.080 ns）
  -> 合计估算 7.307 ns
```

这个违例很小，但 HLS 估算不等于 Vivado 布局布线结果，不能用 136.86 MHz 的估算 Fmax 代替最终 WNS 结论。

### 3.2 子模块

| 子模块 | SA 内实例数 | Estimated Period | Latency | II | Pipeline |
|---|---:|---:|---:|---:|---|
| `peMacUnit` | 16 | 7.120 ns | 16 拍 | 1 | yes |
| `peExp2PWL` | 每个 `peMacUnit` 内 1 个 | 7.080 ns | 13 拍 | 1 | yes |
| `cvtAtoE` | 每个 `peMacUnit` 内 2 个，SA 顶层另有 1 个 | 7.120 ns | 2 拍 | 1 | yes |

`cvtAtoE` 的 RTL 结构中有两级 16 位结果寄存器，该模块资源为 35 FF、2 LUT、0 DSP，说明固定两拍约束已生效。

### 3.3 C/RTL 协同仿真

| 项目 | 最小值 | 平均值 | 最大值 |
|---|---:|---:|---:|
| Latency | 17 拍 | 17 拍 | 17 拍 |
| Interval | 16 拍 | 16 拍 | 16 拍 |

- 总执行时间：401 拍
- RTL：Verilog
- 状态：`Pass`

## 4. 资源使用

| 资源 | 上次报告值 | 本次构建 | 变化 | 整个器件占比 | 单个 SLR 占比 |
|---|---:|---:|---:|---:|---:|
| BRAM_18K | 0 | 0 | 0 | 0% | 0% |
| DSP | 274 | **274** | 0 | 3% | 9% |
| FF | 59,266 | **61,698** | +2,432 | 2% | 7% |
| LUT | 77,960 | **65,160** | -12,800 | 4% | 14% |
| URAM | 0 | 0 | 0 | 0% | 0% |

顶层报告列出 16 个独立 `peMacUnit` 实例，每个使用 17 DSP、3,568 FF 和 3,656 LUT。另外，顶层 CMP 浮点减法使用 2 DSP，因此 DSP 总数为：

```text
16 × 17 + 2 = 274 DSP
```

`peMacUnit` 中同时包含普通 MAC 和 exp2 路径的浮点运算资源，而 `cvtAtoE` 本身不使用 DSP。本次构建的 DSP 用量与上次报告相同；FF 增加 2,432，LUT 减少 12,800。

## 5. 当前合格性结论

| 检查项 | 结论 |
|---|---|
| 矩阵乘与 `rowmax` 功能 | 合格 |
| C/RTL 行为一致性 | 合格 |
| 4×4 PE 空间展开 | 合格，RTL 层次中有 16 个 `peMacUnit` |
| `cvtAtoE` 两拍流水线 | 生效 |
| 顶层目标 II=16 | 达到 |
| 100 MHz HLS 时序估算 | **未通过，负余量 0.007 ns** |
| Vivado IP 导出 | 完成 |
| 最终实现时序 | 未验证 |
| FPGA 上板 | 未验证 |

综合结论：

> 当前 4×4 SA 的 C 仿真、C 综合、C/RTL 协同仿真和 Vivado IP 导出流程均已完成；功能、C/RTL 一致性、16 个 PE 的空间展开以及 `cvtAtoE` 两拍流水线均已确认。顶层 Latency 为 17 拍，II 为 16。但本次 HLS 估算周期为 7.307 ns，超出 7.300 ns 有效预算 0.007 ns，因此当前版本只能作为“功能与流程验证通过、HLS 时序轻微违例”的基准，不能宣称已满足 100 MHz 最终时序。

## 6. 问题与下一步

1. 针对 `current.cmp_d_3 -> mac_in_c -> peMacUnit` 关键路径做进一步切分，优先从 `mac_in_c` 选择与 `peMacUnit` 输入边界入手。
2. 优化后重新核对 Estimated Period、Latency、II、DSP、FF 和 LUT，避免为消除 0.007 ns 违例引入过大资源增长。
3. 将导出 IP 加入 Vivado 工程，完成综合、布局布线并检查 WNS。
4. HLS 对多个静态状态寄存器给出了 power-on initialization 警告，在最终集成时需继续验证复位和初始状态行为。

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
