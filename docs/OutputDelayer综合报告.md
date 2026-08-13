# OutputDelayer 综合报告

## 1. 综合配置

| 项目 | 配置 |
|---|---|
| HLS工具 | Vitis HLS 2024.2 |
| 顶层函数 | `output_delayer_top` |
| 目标器件 | `xcvu37p_CIV-fsvh2892-2-e` |
| 目标时钟周期 | 10 ns（100 MHz） |
| 顶层控制协议 | `ap_ctrl_hs` |
| 输入接口 | `input_r`，129 bit，`ap_none` |
| 输出接口 | `output_r`，128 bit，`ap_none` |

参与综合的设计文件：

```text
src/hls/output_delayer_top.cpp
src/core/delayer.cpp
```

使用的testbench：

```text
tests/hls/test_output_delayer_top.cpp
```

## 2. 流程结果

| 阶段 | 结果 |
|---|---|
| C仿真 | 通过 |
| C综合 | 通过 |
| C/RTL协同仿真 | 通过 |
| Vivado IP导出 | 通过 |

协同仿真完成5次顶层事务，日志结果为：

```text
[PASS] test_output_delayer_top: complete functional test
INFO: [COSIM-1000] *** C/RTL co-simulation finished: PASS ***
```

## 3. 时序与吞吐

### 3.1 HLS综合估计

| 项目 | 结果 |
|---|---:|
| Target Clock Period | 10.00 ns |
| Clock Uncertainty | 2.70 ns |
| 有效时序预算 | 7.30 ns |
| Estimated Clock Period | 0.387 ns |
| 估计时序余量 | 约6.91 ns |
| Latency | 0拍 |
| Initiation Interval | 1拍 |
| Pipeline | no |

与 InputDelayer 相同，OutputDelayer 被综合为**纯组合逻辑**（Latency = 0），数据在单拍内完成所有反转、阶梯延迟和输出反转计算。Estimated Clock Period 仅 0.387 ns，在 10 ns 目标下时序余量极大。

由于 InputDelayer 和 OutputDelayer 共享同一个模板 `delayerStep`，OutputDelayer 的 `rev_input`、`delay_output`、`rev_output` 三个标志在内部被硬编码为 `true`，HLS 工具得以进行更彻底的常量折叠优化，使得 Estimated Clock 比 InputDelayer（0.764 ns）更低。

### 3.2 C/RTL协同仿真

| 项目 | 最小值 | 平均值 | 最大值 |
|---|---:|---:|---:|
| Latency | 0拍 | 0拍 | 0拍 |
| Interval | 1拍 | 1拍 | 1拍 |

协同仿真总执行时间为5拍，Verilog 状态为 `Pass`。零延迟与综合报告的 Latency=0 一致。

## 4. 资源使用

| 资源 | OutputDelayer 使用量 | 器件可用量 | 占比 |
|---|---:|---:|---:|
| BRAM_18K | 0 | 4032 | 0% |
| DSP | 0 | 9024 | 0% |
| FF | 193 | 2607360 | 约0.007% |
| LUT | 68 | 1303680 | 约0.005% |
| URAM | 0 | 960 | 0% |

资源构成明细：

| 类别 | FF | LUT | 说明 |
|---|---|---|---|
| Register | 193 | 0 | 6个32-bit移位寄存器级（延迟管道）+ FSM |
| Multiplexer | 0 | 68 | 各lane输出选择器 |
| Expression | 0 | 0 | 无组合逻辑表达式（全部由 MUX 覆盖） |
| Instance | 0 | 0 | 无实例化子模块 |
| Memory | 0 | 0 | 无存储资源 |
| **Total** | **193** | **68** | |

OutputDelayer 的资源特点：

- **FF 数量多于 InputDelayer（193 vs 99）**：因为 `acc_t` 为 FP32（32-bit），而 InputDelayer 的 `elem_t` 为 FP16（16-bit），移位寄存器的位宽翻倍。193 bits 恰好等于：lane 0 延迟3拍（3×32=96） + lane 1 延迟2拍（2×32=64） + lane 2 延迟1拍（1×32=32） + FSM（1） = 193。
- **LUT 数量远少于 InputDelayer（68 vs 266）**：因为三个控制标志被硬编码为常量，HLS 工具消除了所有条件分支的组合逻辑，仅保留必��的 MUX 选通。
- **无 Expression 类资源**：InputDelayer 有180个 LUT 用于 `rev_input`/`delay_output`/`rev_output` 的条件选择逻辑，OutputDelayer 因这些标志为常量而被完全优化掉。

按4×4 SA 阵列仅需1个 OutputDelayer，其资源开销在整个系统中可忽略不计。

## 5. 已验证功能

当前 testbench 和 C/RTL 协同仿真已经验证：

- 顶层复位和复位后输出归零；
- SA 底部各列输出的反向阶梯延迟：lane 0（延迟3拍）→ lane 1（延迟2拍）→ lane 2（延迟1拍）→ lane 3（不延迟）；
- 连续多拍数据对齐的正确性，5拍内全部输出稳定正确。

## 6. 尚未验证范围

本报告仍不能证明以下内容：

- OutputDelayer 与 Systolic Array 底部 PE 的 `d_output` 连接后，4列交错数据的逐拍对齐；
- 进入 Accumulator 前的延迟输出与 Accumulator 的 `sa_in` 时序匹配；
- 与原 Chisel 仿真进行独立的位级交叉对比；
- 在完整 SA 顶层综合后的资源与吞吐变化。

## 7. 需要关注的工具警告

综合日志中存在以下警告：

1. **`ap_none` 端口无独立有效信号**：与其他模块相同，`output_r` 使用 `ap_none` 时没有独立的端口级有效信号。当前输出向量不包含 valid 字段，集成时需依赖 `ap_ctrl_hs` 握手协议确定数据有效时刻。

2. **上电初始化寄存器**：以下寄存器被标记为 power-on initialization：
   - 6个移位寄存器（`current_out_delay_pipe`）

   这些静态变量的初始值在 FPGA 配置时确定，与 `reset_output_delayer_state()` 的行为一致。由于 OutputDelayer 的 `rev_out_r` 和 `delay_r` 标志在每次调用时被硬编码重写（不依赖跨事务记忆），这两个标志的寄存器实际上已被 HLS 优化消除，因此不再出现在警告列表中（与 InputDelayer 不同）。

## 8. 结论

OutputDelayer 已经完成 C仿真、C综合、5次 C/RTL 协同仿真事务和 Vivado IP 导出。反向阶梯延迟对齐功能通过测试。

当前 OutputDelayer 可以判定为：

> 单 OutputDelayer 功能验证合格，可以进入 SA 级联合验证。

该模块为纯组合逻辑（Latency=0、II=1），时序余量极大（0.387 ns vs 7.30 ns 预算），资源开销极低（193 FF + 68 LUT）。作为 SA 与 Accumulator 之间的数据重对齐前端，其功能已就绪。

## 9. 后续工作

1. 联合 Systolic Array 和 Accumulator 验证 OutputDelayer 输出与 Accumulator `sa_in` 的逐拍数据对齐；
2. 在完整 SA 顶层综合中确认内联后的资源和时序；
3. 使用 Vivado 布局布线确认最终时序。

## 10. 相关文件

```text
build/output_delayer_build/solution1/syn/report/output_delayer_top_csynth.rpt
build/output_delayer_build/solution1/sim/report/output_delayer_top_cosim.rpt
build/output_delayer_build/solution1/sim/report/verilog/output_delayer_top.log
build/output_delayer_build/solution1/syn/verilog/output_delayer_top.v
build/output_delayer_build/solution1/impl/ip/component.xml
build/output_delayer_build/solution1/impl/ip/xilinx_com_hls_output_delayer_top_1_0.zip
```

重新执行：

```bash
./run_hls.sh output_delayer
```
