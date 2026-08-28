# FSA Core 综合报告

## 1. 综合配置

| 项目 | 配置 |
|---|---|
| HLS 工具 | Vitis HLS 2024.2（Build 5238294） |
| C 综合时间 | 2026-08-19 03:03:27 CST |
| 顶层函数 | `fsa_core_top` |
| 构建目录 | `build/fsa_core_build/solution1` |
| 目标器件 | `xcvu37p_CIV-fsvh2892-2-e` |
| 目标时钟周期 | 10 ns（100 MHz） |
| Clock Uncertainty | 2.70 ns |
| HLS 有效时序预算 | 7.30 ns |
| 控制协议 | `ap_ctrl_hs` |
| SA 规模 | 4×4 |
| Scratchpad | 24 行、2 bank、1 sub-bank |
| Accumulator SRAM | 5 行、2 bank、2 sub-bank |

本报告依据 `build/fsa_core_build/solution1/` 中 2026-08-19 03:01～03:09 生成的最新构建结果。参与构建的顶层源码、SA/Accumulator 源码和 `test_fsa_core_top.cpp` 均早于本次流程，版本一致。

## 2. 流程结果

| 阶段 | 结果 |
|---|---|
| C 仿真 | 通过，0 errors |
| C 综合 | 完成 |
| C/RTL 协同仿真 | Verilog `Pass` |
| Vivado IP 导出 | 完成，已生成 `impl/export.zip` |
| Vivado 综合、布局布线 | 未进行 |
| FPGA 上板 | 未验证 |

C 仿真输出：

```text
[PASS] test_fsa_core_top: Scratchpad -> InputDelayer ->
SystolicArray -> OutputDelayer -> Accumulator -> accRAM
```

当前构建证明上述端到端路径在现有测试向量下 C 模型与 RTL 一致，但不能等同于完整 FlashAttention 算法、最终实现时序或上板均已通过。

本次构建相对上一版的主要变化：

| 项目 | 上一版 | 本次 |
|---|---:|---:|
| SA 运算并行度 | 8 套 PE MAC 等效资源，II=16 | **16 套 PE MAC 等效资源，II=1** |
| Accumulator lane 实例 | 4 | **4** |
| Estimated Clock Period | 7.191 ns | **7.123 ns** |
| HLS 估算时序余量 | 0.109 ns | **0.177 ns** |
| C 综合最大 Latency / Interval | 89 / 90 拍 | **89 / 90 拍** |
| 协同仿真普通事务 Latency / Interval | 68 / 69 拍 | **68 / 69 拍** |
| DSP | 158 | **300** |
| FF | 49,045 | **82,431** |
| LUT | 71,856 | **110,748** |

本次将 SA stage 的约束从 `II=16` 改为 `II=1`，综合结果已展开 16 套 PE MAC 等效运算资源。顶层事务时间没有继续下降，说明当前 68/69 拍吞吐瓶颈已不在 SA stage 的启动间隔。

## 3. 功能验证范围

当前 `test_fsa_core_top.cpp` 只通过正式 `fsa_core_top` 接口覆盖：

- reset 清除各模块响应、RMW 和 DMA 有效状态；
- Scratchpad 常量请求及布局控制延迟一个逻辑 step 后对齐；
- 通过 Scratchpad 窄写端口预装 4×4 Q、K；
- Q 经 Scratchpad 和 InputDelayer 装入 PE stationary 寄存器；
- K 流经 InputDelayer 和 4×4 SystolicArray；
- MAC 结果与软件计算的 4×4 `Q×K^T` 金标准比较；
- CMP `UPDATE` 和 `PROP_MAX` 控制，以及 OutputDelayer 的结果对齐；
- Accumulator `ACC_SA` 和 accRAM 读改写地址对齐；
- 通过 accRAM 窄读端口验证写回的 `-rowmax`；
- DMA 窄读响应 valid 延迟一个逻辑 step。

测试只在控制 valid 有效时检查数据；trace 中无效拍出现的 NaN 或保留值属于 don't-care，不作为失败。

`tests/hls/test_fsa_core_full.cpp` 虽然早于本次构建，但当前 `hls/fsa_core/run_hls.tcl` 没有加入该文件。因此，本次流程仍只执行 `test_fsa_core_top.cpp`，本报告不宣称已经验证从 Q/K/V 到 L、softmax、V 累加和最终 O 写回的完整 FlashAttention 流程。

## 4. 时序与吞吐

### 4.1 顶层结果

| 项目 | 结果 |
|---|---:|
| Target Clock Period | 10.000 ns |
| HLS 有效时序预算 | 7.300 ns |
| Estimated Clock Period | **7.123 ns** |
| 估算时序余量 | **+0.177 ns** |
| Estimated Fmax | 140.39 MHz |
| C 综合 Latency | 10～89 拍 |
| C 综合 Interval | 11～90 拍 |
| Pipeline | no |
| 协同仿真 Latency | 8～68 拍，平均 65 拍 |
| 协同仿真 Interval | 9～69 拍，平均 65 拍 |
| 协同仿真总执行时间 | 2,639 拍 |

顶层估算周期小于 7.30 ns 有效预算，且日志中没有 `HLS 200-871`，因此 100 MHz HLS 时序估算通过。但余量只有 0.177 ns，仍属于很窄的 HLS 估算余量，最终布局布线存在较高失配风险。

逐事务协同仿真记录显示，两次 reset 事务为 8/9 拍，其余 38 个逻辑 step 均为 68/69 拍。C 综合范围和协同仿真实测范围不同，是因为测试没有覆盖综合工具考虑的全部控制路径；协同仿真最大值仍处于综合上界以内。

一次 `fsa_core_top` 调用只表示整个核心推进一个 Chisel 逻辑 step，并不等于一个物理时钟周期。当前非 reset 逻辑 step 实测需要 68 拍，下一事务间隔为 69 拍，系统集成必须使用 `ap_start/ap_ready/ap_done` 握手。

### 4.2 关键子模块

| 模块 | Estimated Period | Latency | II / Interval | Pipeline | DSP | FF | LUT |
|---|---:|---:|---:|---|---:|---:|---:|
| `fsa_core_sa_stage` | 7.120 ns | 16 拍 | II=1 | yes | 280 | 64,349 | 79,829 |
| `peMacUnit`（单 PE 参考） | 7.120 ns | 16 拍 | II=1 | yes | 17 | 3,416 | 4,458 |
| `peExp2PWL` | 6.846 ns | 13 拍 | II=1 | yes | 7 | 1,260 | 2,641 |
| `cvtAtoE` | 7.120 ns | 2 拍 | II=1 | yes | 0 | 35 | 2 |
| `accumulator_step` | 6.516 ns | 7～18 拍 | 7～18 拍 | no | 20 | 7,314 | 20,660 |
| `accumulator_lane_step` | 6.516 ns | 6～17 拍 | 6～17 拍 | no | 5 | 1,599 | 5,044 |
| `begin_reciprocal` | 6.394 ns | 1 拍 | 1 拍 | no | 0 | 169 | 783 |
| `normalize_and_round` | 5.016 ns | 0 拍 | 0 拍 | no | 0 | 0 | 1,000 |

顶层还生成 9 个数据搬运或打包循环模块，Latency 为 4～8 拍、Interval 为 3～7 拍。它们局部流水化，但顶层仍是非流水事务，不能据此认为整核具有逐拍吞吐。

表中 `peMacUnit` 是单 PE 函数的参考报告；在本次 SA 集成结果中该包装被内联，实际并行度应以 SA stage 的 II、展开后的运算实例数和资源总量判断。

### 4.3 阵列并行性

综合层次和生成 RTL 显示：

- 顶层只有 1 个 `fsa_core_sa_stage` 实例；
- SA stage 的源码约束为 `PIPELINE II=1`，综合结果实际达到 Latency=16、II=1；
- `peMacUnit` 包装已被内联，因此不能再按名为 `peMacUnit` 的 RTL 模块计数；
- SA 层次中有 **16 个 `peExp2PWL`**，同时存在 16 个 FP32 加法器、32 个 FP32 乘法器以及 20 个 FP32 减法器；其中 16 个减法器属于各 PE，另外 4 个属于四列 CMP；
- 上述算术实例合计 280 DSP，与 `16×17 DSP` 的 PE MAC 等效资源加 `4×2 DSP` 的 CMP 减法器完全一致；
- `accumulator_step` 内部有 **4 个 `accumulator_lane_step`**，四列各有一套独立运算资源。

因此，当前 SA 的 16 个 PE 运算 datapath 和 Accumulator 的 4 个 lane 均已达到空间并行。虽然 `peMacUnit` 不再作为独立 RTL 子模块出现，但 II、浮点运算实例数和 DSP 资源三项证据一致，不能据此误判为资源仍在共享。

需要区分局部并行与顶层事务吞吐：SA stage 已能每拍接受一组新输入，但 `fsa_core_top` 仍是非流水 `ap_ctrl_hs` 事务，非 reset 事务实测 Interval=69 拍。因此整核仍不是“一物理拍推进一个逻辑 step”，上层必须等待 `ap_ready/ap_done`。

## 5. 资源使用

| 资源 | 本次构建 | 整个器件可用 | 整个器件占比 | 单个 SLR 占比（HLS 报告） |
|---|---:|---:|---:|---:|
| BRAM_18K | 0 | 4,032 | 0% | 0% |
| DSP | **300** | 9,024 | 约 3.32% | 9% |
| FF | **82,431** | 2,607,360 | 约 3.16% | 9% |
| LUT | **110,748** | 1,303,680 | 约 8.49% | 25% |
| URAM | 0 | 960 | 0% | 0% |

主要层次资源：

| 层次 | DSP | FF | LUT |
|---|---:|---:|---:|
| SA 阶段（16 套 PE MAC 等效资源） | 280 | 64,349 | 79,829 |
| Accumulator（4 个 lane） | 20 | 7,314 | 20,660 |
| 单个 Accumulator lane | 5 | 1,599 | 5,044 |
| 9 个顶层循环模块合计 | 0 | 41 | 3,372 |

与上一版 158 DSP、49,045 FF、71,856 LUT 相比，本次分别增加 142 DSP、33,386 FF 和 38,892 LUT。主要原因是 SA 从 8 套提升到 16 套 PE MAC 等效资源；Accumulator 仍保持 4 个独立 lane。其余资源来自顶层状态寄存器、仲裁表达式、多路选择器、SRAM 响应和接口打包逻辑。

### 5.1 存储映射

| 存储结构 | 实例与形状 | 报告位数 |
|---|---|---:|
| Scratchpad 主存储 | 2×`24×64 bit` | 3,072 bit |
| Accumulator SRAM 主存储 | 4×`5×64 bit` | 1,280 bit |
| `acc_ram_io` 临时/聚合存储 | `3×384 bit` | 1,152 bit |
| accRAM 窄读响应 | `8×32 bit` | 256 bit |
| **合计** | 8 个报告实例 | **5,760 bit** |

Memory 分类合计使用 672 FF 和 680 LUT。由于存储深度很浅，HLS 全部使用寄存器/LUT 实现，没有推断 BRAM 或 URAM。

## 6. 接口

| 端口 | 方向 | 位宽 | 协议 |
|---|---|---:|---|
| `input_r` | input | 445 bit | `ap_none` |
| `output_r_i` | input | 597 bit | `ap_none` |
| `output_r_o` | output | 597 bit | `ap_none` |
| 控制端口 | `ap_start/ap_done/ap_idle/ap_ready` | - | `ap_ctrl_hs` |

虽然 C++ 参数 `output` 表面上是输出引用，但顶层在计算请求接受状态时又读取了 `output.sp_read_ready`、`output.acc_read_ready` 和 `output.acc_dma_read_ready`。HLS 因而把聚合输出拆成 597-bit 输入 `output_r_i` 与 597-bit 输出 `output_r_o`，形成不理想的读写接口，而不是纯输出端口。

集成前应改用局部 ready 变量完成内部判断，再统一赋给 `output`，并重新综合确认只保留纯输出数据端口。当前 `ap_none` 输出也没有独立 data-valid，必须结合 `ap_done` 判断有效时刻。

## 7. 警告与风险

本次综合的主要非致命警告为：

1. `HLS 214-167`：变量 `acc_ram_io` 可能存在数组越界访问。当前 C 仿真和协同仿真通过，但警告尚未从源码结构上消除。
2. `HLS 214-250/253`：对 Scratchpad/accRAM 响应数组结构体成员的部分 `ARRAY_PARTITION` 指令被忽略。主存储 bank 实例仍已分开，但不能假定所有响应数组分割均生效。
3. `HLS 214-366`：多个 `std::array` 辅助函数因调用签名差异被复制，可能增加资源。
4. `SYNCHK 200-23`：`accumulator.cpp:202` 的变量索引位段选择可能导致较差 QoR。
5. 大量持久状态寄存器被报告为 power-on initialization，包括 SA 管线、Accumulator reciprocal、Delayer 和响应状态；最终系统必须验证 reset 和初始化行为。
6. `output_r` 使用 `ap_none`，没有独立数据有效信号；同时当前被拆成 `output_r_i/output_r_o`。
7. XSIM 检测到 `LIBRARY_PATH` 环境变量可能影响 C 编译器；本次协同仿真仍通过。
8. 当前没有 Vivado 布局布线、WNS 或板级结果。

## 8. 当前合格性结论

| 检查项 | 结论 |
|---|---|
| 当前端到端测试功能 | 合格 |
| C/RTL 行为一致性 | 合格，Verilog `Pass` |
| C 综合 | 完成 |
| 100 MHz HLS 时序估算 | 通过，但余量仅 0.177 ns |
| IP 导出 | 完成 |
| 4×4 SA 空间并行度 | **达到：16 套 PE MAC 等效运算资源，SA II=1** |
| 四列 Accumulator 空间并行度 | **达到：4 个独立 lane** |
| 逐拍逻辑 step 吞吐 | **未达到，非 reset 实测 Interval=69 拍** |
| 顶层数据接口 | **需整改，输出被拆成 597-bit 输入/输出端口** |
| 完整 FlashAttention 流程 | **本次构建未测试** |
| Vivado 最终实现时序 | **未验证** |
| FPGA 上板 | **未验证** |

综合结论：

> 当前 `fsa_core_top` 已完成 C 仿真、C 综合、Verilog C/RTL 协同仿真和 IP 导出，现有 `Q×K^T`、rowmax 与 accRAM RMW 端到端测试通过。HLS 估算周期为 7.123 ns，以 0.177 ns 余量满足 100 MHz 目标，资源为 300 DSP、82,431 FF 和 110,748 LUT。SA stage 已展开 16 套 PE MAC 等效资源并达到 II=1，Accumulator 也保持 4 个独立 lane，局部空间并行度已达标；但顶层非 reset 逻辑 step 仍需 68 拍、间隔 69 拍。此外，输出端口仍被综合成 597-bit 输入/输出对。当前版本可作为空间并行化后的功能和 C/RTL 一致性基准，但不能作为整核逐拍吞吐、完整 FlashAttention 或最终实现时序已达标的结论。

## 9. 后续工作

1. 定位并消除 `acc_ram_io` 潜在越界警告，清理被忽略的响应数组分割指令。
2. 使用局部 ready 信号替代对 `output` 的内部回读，使顶层恢复纯输出接口。
3. SA 16-PE 与 Accumulator 四列并行均已实现；下一步应定位顶层 Interval=69 的主要调度路径，判断是否需要重构顶层事务、拆分状态推进或引入更高层流水化。
4. 将 `test_fsa_core_full.cpp` 加入 HLS 流程，完成完整 Q/K/V、L、softmax、V 累加和 O 写回的 C 仿真及 C/RTL 协同仿真。
5. 将导出 IP 加入 Vivado 工程，完成综合、布局布线并重点检查仅 0.177 ns HLS 余量以及单 SLR 约 25% LUT 对最终 WNS 和拥塞的影响。

## 10. 结果文件

```text
build/fsa_core_build/solution1/csim/report/fsa_core_top_csim.log
build/fsa_core_build/solution1/syn/report/fsa_core_top_csynth.rpt
build/fsa_core_build/solution1/syn/report/p_anonymous_namespace_fsa_core_sa_stage_csynth.rpt
build/fsa_core_build/solution1/syn/report/peMacUnit_csynth.rpt
build/fsa_core_build/solution1/syn/report/peExp2PWL_csynth.rpt
build/fsa_core_build/solution1/syn/report/accumulator_step_csynth.rpt
build/fsa_core_build/solution1/syn/report/p_anonymous_namespace_accumulator_lane_step_csynth.rpt
build/fsa_core_build/solution1/sim/report/fsa_core_top_cosim.rpt
build/fsa_core_build/solution1/sim/report/verilog/result.transaction.rpt
build/fsa_core_build/solution1/sim/report/verilog/fsa_core_top.log
build/fsa_core_build/solution1/solution1.log
build/fsa_core_build/solution1/impl/export.zip
```

重新运行 FSA Core HLS 流程：

```bash
./run_hls.sh fsa_core
```
