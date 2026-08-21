# `accumulator_pipeline_batch_top` 综合报告

## 1. 综合配置

本报告读取 `build/accumulator_pipeline_build/solution1` 的现有产物编写，没有重新运行
Vitis HLS，也没有修改源码、测试平台、Tcl、时钟或接口。

| 项目 | 当前构建 |
|---|---|
| Vitis HLS | 2024.2，Build 5238294 |
| 综合顶层 | `accumulator_pipeline_batch_top` |
| Solution | `solution1`，Vivado IP Flow Target |
| 目标器件 | `xcvu37p_CIV-fsvh2892-2-e`（Virtex UltraScale+ HBM） |
| 时钟目标 | 10.00 ns，即 100 MHz |
| 时钟不确定度 | 2.70 ns |
| 有效组合逻辑预算 | 7.30 ns |
| RTL / 协同仿真 | Verilog / XSIM |
| 块级协议 | `ap_ctrl_hs` |
| 批处理长度 | 128 个逻辑 tick |
| 构建产物时间范围 | 2026-08-20 18:39:25 至 18:43:01（UTC+8） |

HLS Tcl 加入 `accumulator_pipeline_top.cpp`、`accumulator_pipeline.cpp`、
`arithmetic.cpp` 和测试平台 `test_accumulator_pipeline_top.cpp`，执行了 C 仿真、
C 综合、Verilog 协同仿真和 IP 导出。当前本地源码、测试和 Tcl 的修改时间均早于本次
构建开始时间；但产物内嵌路径是 Linux 服务器上的
`/home/zhangchenxuan/FSA_HLS/...`，当前检查位置为 Windows 构建副本，无法证明两边
文件逐字一致。因此以下结论以所给构建产物为准，源码版本对应性仍为
**无法完全确认，构建结果可能过期**。

## 2. 流程结果

| 阶段 | 状态 | 证据与限制 |
|---|---|---|
| C 仿真 | 通过 | 日志包含 `[PASS] test_accumulator_pipeline_top: 64 contiguous ACC_SA tokens plus four-lane EXP_S2` |
| C 综合 | 完成 | 顶层、批循环及 8 个算术子模块报告齐全 |
| RTL 协同仿真 | 通过 | Verilog/XSIM，1 个事务，延迟 656 周期 |
| IP 导出 | 完成 | `component.xml` 与 `export.zip` 存在 |
| Vivado 实现 | 未执行/未提供 | 不能据此判断布局布线后的 WNS、实际 Fmax 或资源 |
| 板级验证 | 未执行/未提供 | 不能据此判断复位、存储器连接和板上功能 |

当前证据确认 C/RTL 功能检查通过且 IP 已导出，但不能替代 Vivado 实现和板级验证。

## 3. 功能验证范围

当前唯一 Tcl 选中的测试平台执行一个 128 tick 批次，覆盖：

- 第 0 个逻辑 tick 发送 `SET_SCALE`；
- 随后发送 64 个连续 `ACC_SA`，检查输入就绪、快速结果延迟、有效位、写使能、
  tag、写地址和 4 列数值；
- 参考值独立计算为 `scale × sram_in + sa_in`，浮点容差为
  `1e-5 × (1 + |expected|)`；
- 快速流水排空后再次设置四列 scale 为 `{0, 1, -1, 2}`，发送 `EXP_S2`；
- 检查 `EXP_S2` 接受时的 `scale_busy`、忙窗口内的 backpressure、完成时的
  `slow_done` 和随后恢复的 `input_ready`；
- 完成后发送 `ACC` 读回新 scale，验证四列结果为 `{1, 2, 0.5, 4}`，从功能上覆盖
  四列 `2^x` 更新；
- 顶层调用的 `reset=true` 会在本批次开始时清除算法状态。

仍未覆盖：

- `EXP_S1`、`RECIPROCAL` 及其边界情况；
- 连续多个慢速请求、快慢命令交错和慢速请求被拒绝后的重试；
- NaN、Inf、零附近、极大/极小值及随机向量；
- 多个连续顶层事务和批次间状态保持；
- `ap_rst` 与算法侧 `reset` 的独立及组合时序；
- 外部 `output_r` 存储器的真实系统连接。

日志中的“64 contiguous”指测试数组中的连续逻辑 tick；综合后的循环 II=5，不能解释为
物理接口每个时钟接收一个 token。

## 4. 时序与吞吐

### 4.1 顶层结果

| 指标 | 当前值 |
|---|---:|
| 目标周期 | 10.000 ns |
| 时钟不确定度 | 2.700 ns |
| 有效预算 | 7.300 ns |
| HLS 估算周期 | 6.900 ns |
| 计算得到的有效预算裕量 | **+0.400 ns** |
| 由估算周期直接换算的频率 | 约 144.93 MHz |
| 顶层延迟 | 658 周期，约 6.58 μs @ 100 MHz |
| 顶层启动间隔 | 659 周期 |
| 顶层流水化 | 否 |

`6.900 + 2.700 = 9.600 ns`，相对 10.000 ns 目标保留 0.400 ns，因此本次
**100 MHz HLS 时序估算通过**。约 144.93 MHz 只是用 HLS 组合逻辑估算周期换算的
数值，不是布局布线后的 Fmax；正裕量也不能替代 Vivado 实现时序。

### 4.2 批循环

| 层级 | 延迟 | Interval / II | 备注 |
|---|---:|---:|---|
| `accumulator_pipeline_batch_top` | 658 | 659 | 非流水化块级调用 |
| `Pipeline_VITIS_LOOP_59_1` | 655 | 645 | 自动回卷 |
| `VITIS_LOOP_59_1` | 653 | **II=5** | 目标 II=1，trip count=128，单次迭代延迟 19 |

目标 II=1 仍未达到。综合日志显示 II=1 至 II=4 均受局部状态 `dc.sram3` 的
store/load 循环携带依赖限制，最终 II=5。当前 II 瓶颈不再是上一版报告所述的
`scale_update_pending`；时序问题也已消失，但循环依赖仍限制吞吐。

按最终 II=5 计算，稳态逻辑 tick 吞吐为：

`100 MHz / 5 = 20.00 M tick/s`

按完整 128 tick 批次和 658 周期顶层延迟计算，含批次开销的平均吞吐约为：

`128 / 658 × 100 MHz = 19.45 M tick/s`

测试中的 `accumulatorFastLatency=8` 和 `accumulatorExp2Latency` 都是逻辑 tick
对齐距离，不应直接解释为相同数量的物理时钟周期。

### 4.3 协同仿真

Verilog 协同仿真通过，唯一事务的最小/平均/最大延迟均为 656 周期，总执行周期为
656。事务报告中的 interval=0 表示没有下一事务可供计算的占位值，不能解释为顶层
II=0；多事务连续启动能力没有由本次协同仿真覆盖。

## 5. 关键子模块、循环与并行性

### 5.1 四路快速通路

| 模块 | 估算周期 | 延迟 | II | DSP | FF | LUT |
|---|---:|---:|---:|---:|---:|---:|
| `fastLane0` | 6.149 ns | 4 | 1 | 5 | 532 | 451 |
| `fastLane1` | 6.149 ns | 4 | 1 | 5 | 532 | 451 |
| `fastLane2` | 6.149 ns | 4 | 1 | 5 | 532 | 451 |
| `fastLane3` | 6.149 ns | 4 | 1 | 5 | 532 | 451 |

源码采用四个不同名称且禁止内联的封装函数；循环综合报告和生成 RTL 均显示 4 个独立
实例，资源合计为 `4 × 5 = 20 DSP`。因此当前构建确实实现 4 套快速 FMA 等价资源。

### 5.2 四路指数通路

| 模块 | 估算周期 | 延迟 | II | DSP | FF | LUT |
|---|---:|---:|---:|---:|---:|---:|
| `exp2Lane0` | 6.149 ns | 15 | 1 | 10 | 1,231 | 2,383 |
| `exp2Lane1` | 6.149 ns | 15 | 1 | 10 | 1,231 | 2,383 |
| `exp2Lane2` | 6.149 ns | 15 | 1 | 10 | 1,231 | 2,383 |
| `exp2Lane3` | 6.149 ns | 15 | 1 | 10 | 1,231 | 2,383 |

当前源码同样使用四个不同名称的非内联 `exp2Lane` 封装；循环层级和 RTL 均显示 4 个
独立实例。测试还实际执行四列 `EXP_S2` 并检查结果。DSP 资源可完整对账：

`4 × 5（快速通路） + 4 × 10（指数通路） = 60 DSP`

因此本次构建已经从上一版的单个共享指数单元变为 **4 路物理并行指数资源**。各
`exp2Lane` 的局部 II=1 只代表该子模块的接收能力，不会把外层循环 II=5 或非流水化
顶层事务间隔自动提升到 1。

## 6. 资源与存储映射

### 6.1 顶层资源

| 资源 | 使用 | 器件可用 | 计算利用率 |
|---|---:|---:|---:|
| BRAM | 0 | 4,032 | 0.00% |
| DSP | 60 | 9,024 | 0.66% |
| FF | 20,325 | 2,607,360 | 0.78% |
| LUT | 31,649 | 1,303,680 | 2.43% |
| URAM | 0 | 960 | 0.00% |

报告给出的单 SLR 视角约为 DSP 1%、FF 2%、LUT 7%。器件总量不是当前主要限制，
但这些数字只是 HLS 综合估算。

### 6.2 资源层级

| 层级/类别 | DSP | FF | LUT |
|---|---:|---:|---:|
| 批循环模块 | 60 | 17,552 | 30,507 |
| 顶层寄存器 | 0 | 2,773 | 0 |
| 顶层多路器 | 0 | 0 | 1,142 |

批循环内部包括 9,250 LUT 表达式逻辑、8,069 LUT 多路器、10,500 FF / 1,568 LUT
寄存器，以及 60 DSP / 7,052 FF / 11,620 LUT 子实例。DSP 全部由 4 个快速通路和
4 个指数通路解释。

BRAM/URAM 为 0 不表示没有外部存储器需求：输入、输出数组综合为外部
`ap_memory` 端口，存储体不计入本 IP 的内部 BRAM/URAM。

## 7. 接口

导出 IP 共有 15 个物理端口，采用 `ap_ctrl_hs` 控制和两个打包存储器接口。

| 接口 | 协议与方向 | 物理端口 | 数据含义 |
|---|---|---|---|
| 控制 | `ap_ctrl_hs` | 6 个 | `ap_clk`、`ap_rst`、`ap_start`、`ap_done`、`ap_idle`、`ap_ready` |
| 算法复位 | `ap_none`，输入 | 1-bit `reset` | 清除内部算法状态 |
| 输入数组 `input_r` | `ap_memory`，输入 | 7-bit address0、CE0、279-bit Q0 | 单读端口 |
| 输出数组 `output_r` | `ap_memory`，inout | 7-bit address0、CE0、WE0、146-bit D0/Q0 | 单端口读写存储器 |

279-bit 输入元素打包了 `valid`、8-bit 命令、128-bit `sa_in`、128-bit
`sram_in`、5-bit 写地址、写使能和 8-bit tag。146-bit 输出元素打包了
`input_ready`、`scale_busy`、`slow_done`，以及结果有效、128-bit 数据、
5-bit 写地址、写使能和 8-bit tag。

相较上一版，`output_r` 从双端口收敛为单端口，但它仍不是单向结果总线：
`component.xml` 把它标为 inout，并要求系统提供 Q0 读数据。集成时必须连接
address0、CE0、WE0、D0 和 Q0，并区分块级 `ap_rst` 与算法侧 `reset`。当前顶层是
128 项批处理封装，也不是逐拍 AXI-Stream 接口。

## 8. 警告与风险

本次 solution 日志统计到 137 条 warning、0 条 error。

| 告警代码 | 数量 | 说明与影响 |
|---|---:|---|
| HLS 200-880 | 4 | `dc.sram3` 的 store/load 循环依赖使 II=1～4 失败，最终 II=5 |
| RTGEN 206-101 | 124 | 寄存器具有上电初始化值；系统仍需验证复位行为 |
| SYN 201-103 | 8 | 4 个 `fastLane` 和 4 个 `exp2Lane` 名称合法化，不影响功能 |
| SYNCHK 200-23 | 1 | `accumulator_pipeline.cpp:159` 变量索引 range 选择可能影响 QoR |

上一版的 HLS 200-871 时序违例和 HLS 200-1016 关键路径告警已不再出现。当前最主要的
综合约束问题是外层 II=5；其次是变量索引选择的 QoR 风险和对寄存器上电初始化的依赖。

124 条上电初始化告警不等于器件上电状态已经可靠。IP 集成和板级测试仍需验证
`ap_rst`、算法 `reset`、批次边界及状态保持/清除顺序。

## 9. 与上一版报告的变化

上一版报告对应 2026-08-20 16:36～16:40 构建；当前版对应 18:39～18:43 构建。

| 指标 | 上一版 | 当前版 | 变化 |
|---|---:|---:|---:|
| HLS 估算周期 | 7.353 ns | 6.900 ns | -0.453 ns（-6.16%） |
| 有效预算裕量 | -0.053 ns | +0.400 ns | +0.453 ns |
| 外层循环 II | 5 | 5 | 不变 |
| 内层迭代延迟 | 23 | 19 | -4（-17.39%） |
| 顶层延迟 | 662 | 658 | -4（-0.60%） |
| 协同仿真延迟 | 660 | 656 | -4（-0.61%） |
| 快速通路物理实例 | 4 | 4 | 不变 |
| `exp2Lane` 物理实例 | 1 | 4 | +3 |
| DSP | 30 | 60 | +100.00% |
| FF | 25,797 | 20,325 | -5,472（-21.21%） |
| LUT | 26,164 | 31,649 | +5,485（+20.96%） |
| IP 物理端口 | 20 | 15 | -5（-25.00%） |
| Warning | 136 | 137 | +1 |

本次构建的主要收益是 100 MHz HLS 时序从负裕量转为 +0.400 ns，并实现四路独立
`exp2Lane`，测试也新增了四列 `EXP_S2` 功能与握手检查。代价是 DSP 翻倍、LUT
增加约 21%；FF 反而减少约 21%。外层 II 仍为 5，且瓶颈改为 `dc.sram3` 的状态依赖。
接口收敛为单端口 `output_r`，物理端口进一步减少。

## 10. 当前合格性结论与后续工作

| 检查项 | 结论 |
|---|---|
| C 仿真 | 通过：64 个连续 `ACC_SA` 和四列 `EXP_S2` |
| C 综合 | 完成 |
| 100 MHz HLS 时序估算 | **通过：+0.400 ns 计算裕量** |
| 外层目标 II=1 | **未达到：实际 II=5** |
| 四路快速通路 | 通过层级、RTL 和资源对账确认 |
| 四路指数通路 | 通过层级、RTL、资源对账和当前测试确认 |
| RTL 协同仿真 | 通过：单事务 656 周期 |
| IP 导出 | 完成 |
| Vivado 实现 | 未执行/未提供 |
| FPGA 板级验证 | 未执行/未提供 |

当前构建在 HLS 范围内已满足 100 MHz 估算时序，并确认快速和指数通路均为四路物理
并行；但外层循环仍只能每 5 个物理周期启动一个逻辑 tick。下一步应优先分析
`dc.sram3` 在 `accumulator_pipeline.cpp:580/667` 附近的真实读写依赖，在不改变
状态语义的前提下评估拆分、前递或明确依赖距离；同时补充 `EXP_S1`、倒数、连续慢速
请求和多事务测试。最终频率、实现资源、复位可靠性及系统接口仍需 Vivado
布局布线和板级验证。

## 11. 结果文件

- 顶层综合：`solution1/syn/report/accumulator_pipeline_batch_top_csynth.rpt`
- 批循环综合：`solution1/syn/report/accumulator_pipeline_batch_top_Pipeline_VITIS_LOOP_59_1_csynth.rpt`
- 子模块综合：`solution1/syn/report/p_anonymous_namespace_{fastLane0..3,exp2Lane0..3}_csynth.rpt`
- C 仿真：`solution1/csim/report/accumulator_pipeline_batch_top_csim.log`
- RTL 协同仿真：`solution1/sim/report/accumulator_pipeline_batch_top_cosim.rpt`
- 事务明细：`solution1/sim/report/verilog/result.transaction.rpt`
- Solution 日志：`solution1/solution1.log`
- IP 元数据：`solution1/impl/ip/component.xml`
- 导出 IP：`solution1/impl/export.zip`

