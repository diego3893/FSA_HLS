# InputDelayer 综合报告

## 1. 综合配置

| 项目 | 配置 |
|---|---|
| HLS工具 | Vitis HLS 2024.2 |
| 顶层函数 | `input_delayer_top` |
| 目标器件 | `xcvu37p_CIV-fsvh2892-2-e` |
| 目标时钟周期 | 10 ns（100 MHz） |
| 顶层控制协议 | `ap_ctrl_hs` |
| 输入接口 | `input_r`，69 bit，`ap_none` |
| 输出接口 | `output_r`，64 bit，`ap_none` |

参与综合的设计文件：

```text
src/hls/input_delayer_top.cpp
src/core/delayer.cpp
```

使用的testbench：

```text
tests/hls/test_input_delayer_top.cpp
```

## 2. 流程结果

| 阶段 | 结果 |
|---|---|
| C仿真 | 通过 |
| C综合 | 通过 |
| C/RTL协同仿真 | 通过 |
| Vivado IP导出 | 通过 |

协同仿真完成15次顶层事务，日志结果为：

```text
[PASS] test_input_delayer_top: complete functional test
INFO: [COSIM-1000] *** C/RTL co-simulation finished: PASS ***
```

## 3. 时序与吞吐

### 3.1 HLS综合估计

| 项目 | 结果 |
|---|---:|
| Target Clock Period | 10.00 ns |
| Clock Uncertainty | 2.70 ns |
| 有效时序预算 | 7.30 ns |
| Estimated Clock Period | 0.764 ns |
| 估计时序余量 | 约6.54 ns |
| Latency | 0拍 |
| Initiation Interval | 1拍 |
| Pipeline | no |

关键特征：InputDelayer 被综合为**纯组合逻辑**（Latency = 0），数据在单拍内完成所有反转和阶梯延迟计算。Estimated Clock Period 仅 0.764 ns，时序余量极大，在 10 ns 目标下毫无压力。

当前顶层标记为 no pipeline，但由于 Latency=0、II=1，实际上已具备每拍接受一次新事务的能力。

### 3.2 C/RTL协同仿真

| 项目 | 最小值 | 平均值 | 最大值 |
|---|---:|---:|---:|
| Latency | 0拍 | 0拍 | 0拍 |
| Interval | 1拍 | 1拍 | 1拍 |

协同仿真总执行时间为15拍，Verilog 状态为 `Pass`。零延迟与综合报告的 Latency=0 一致。

## 4. 资源使用

| 资源 | InputDelayer 使用量 | 器件可用量 | 占比 |
|---|---:|---:|---:|
| BRAM_18K | 0 | 4032 | 0% |
| DSP | 0 | 9024 | 0% |
| FF | 99 | 2607360 | 约0.004% |
| LUT | 266 | 1303680 | 约0.02% |
| URAM | 0 | 960 | 0% |

资源构成明细：

| 类别 | FF | LUT | 说明 |
|---|---|---|---|
| Register | 99 | 0 | 6个16-bit移位寄存器级（延迟管道）+ 2个1-bit标志位 + FSM |
| Expression | 0 | 180 | 反转/延迟/输出选择的组合逻辑 |
| Multiplexer | 0 | 86 | 各lane输出选择器 |
| Instance | 0 | 0 | 无实例化子模块 |
| Memory | 0 | 0 | 无存储资源 |
| **Total** | **99** | **266** | |

InputDelayer 是一个轻量级纯组合逻辑模块，不使用 DSP 和 BRAM。99个FF完全来自跨事务状态保持所需的移位寄存器管道（每lane 16-bit × 跨lane存储）。按4×4 SA 阵列仅需1个 InputDelayer，其资源开销在整个系统中可忽略不计。

## 5. 已验证功能

当前 testbench 和 C/RTL 协同仿真已经验证：

- 顶层复位和复位后输出归零；
- 不启任何延迟时的直通（bypass）；
- 仅反转输入（`rev_input`）时，lane 3↔0、2↔1 正确交换；
- 仅反转输出（`rev_output`）时，输出顺序正确反转；
- 同时反转输入和输出时，两次反转相互抵消，输出保持原序；
- 阶梯延迟（`delay_output`）：lane 0 不延迟、lane 1 延迟1拍、lane 2 延迟2拍、lane 3 延迟3拍，连续多拍移位正确；
- 输入反转、阶梯延迟、输出反转同时启用时，三者的组合行为正确。

## 6. 尚未验证范围

本报告仍不能证明以下内容：

- InputDelayer 与 Systolic Array 连接后，4路输出在逐拍时序下能否正确对齐 SA 的 PE 输入；
- `rev_input`/`delay_output`/`rev_output` 标志跨事务动态变化的正确性（当前测试中每拍可独立设置这些标志，实际控制器如何驱动尚待验证）；
- 与原 Chisel 仿真进行独立的位级交叉对比；
- 在完整 SA 顶层综合后的资源与吞吐变化。

## 7. 需要关注的工具警告

综合日志中存在以下警告：

1. **`ap_none` 端口无独立有效信号**：与 PE/CMP 相同，`output_r` 使用 `ap_none` 时没有独立的端口级有效信号。当前输出向量不包含 valid 字段（与 PE/CMP 不同），集成时需依赖 `ap_ctrl_hs` 握手协议确定数据有效时刻。

2. **上电初始化寄存器**：以下寄存器被标记为 power-on initialization：
   - `current_delay_r`
   - `current_rev_out_r`
   - 6个移位寄存器（`current_out_delay_pipe`）

   这些静态变量的初始值在 FPGA 配置时确定，与 `reset_input_delayer_state()` 的行为一致。在 Vivado 中上电后、复位前这些寄存器已为正确初值，因此不影响实际功能。

## 8. 结论

InputDelayer 已经完成 C仿真、C综合、15次 C/RTL 协同仿真事务和 Vivado IP 导出。全部反转组合、阶梯延迟和组合行为均通过测试。

当前 InputDelayer 可以判定为：

> 单 InputDelayer 功能验证合格，可以进入 SA 级联合验证。

该模块为纯组合逻辑（Latency=0、II=1），时序余量极大（0.764 ns vs 7.30 ns 预算），资源开销极低（99 FF + 266 LUT）。作为 SA 的数据输入前端，其功能已就绪。

## 9. 后续工作

1. 联合 Systolic Array 验证 InputDelayer 输出与 PE 左端输入的逐拍数据对齐；
2. 在完整 SA 顶层综合中确认内联后的资源和时序；
3. 使用 Vivado 布局布线确认最终时序。

## 10. 相关文件

```text
build/input_delayer_build/solution1/syn/report/input_delayer_top_csynth.rpt
build/input_delayer_build/solution1/sim/report/input_delayer_top_cosim.rpt
build/input_delayer_build/solution1/sim/report/verilog/input_delayer_top.log
build/input_delayer_build/solution1/syn/verilog/input_delayer_top.v
build/input_delayer_build/solution1/impl/ip/component.xml
build/input_delayer_build/solution1/impl/ip/xilinx_com_hls_input_delayer_top_1_0.zip
```

重新执行：

```bash
./run_hls.sh input_delayer
```
