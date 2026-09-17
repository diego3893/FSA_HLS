# `fsa_stream` 综合报告

## 1. 报告范围与结论摘要

本报告依据 `build/fsa_stream_build/solution1` 中 2026-09-14 15:37:35 至
15:44:44（UTC+8）生成的 Vitis HLS 产物更新。报告只读取已有 CSim、C 综合、RTL
CoSim、层级、资源和日志文件；没有重新运行 Vitis HLS、Vivado 或板级测试，也没有修改
源码、测试平台、时钟、器件或接口。

当前构建的 CSim、C 综合和 Verilog/xsim RTL CoSim 均通过。9×4 非因果、9×4 因果和
非法长度三笔 RTL 事务分别为 2,569、1,828 和 55 cycles，总执行时间为 4,432 cycles，
未出现此前的 DATAFLOW 死锁或数值不一致。

本次最关键的综合结果是：4×4 SA 主循环已达到 **II=1**，单 tile latency/interval 为
**237/222 cycles**。16 个 PE 均为 latency=9、II=1，4 个 CMP 均为 latency=3、II=1。
顶层资源为 20 BRAM_18K、108 DSP、74,357 FF 和 109,091 LUT。顶层 HLS 估算周期为
7.300 ns，恰好等于扣除 2.700 ns uncertainty 后的有效预算，因此 HLS 层面没有时序
违例，但裕量为 0 ns，仍不能代替 Vivado 布局布线时序。

与旧 `fsa_dma_top` 相同的 9×4 非因果用例相比，当前 RTL CoSim latency 从 27,566
降至 2,569 cycles，降低 90.68%，约为原来的 1/10.73；DSP、FF 和 LUT 也分别减少
64.36%、29.62% 和 15.41%。与 `FSA-main` 的比较只能确认结构与静态微程序语义；
当前工作区没有 `FSA-main` 的同器件综合报告，不能给出可靠的资源、Fmax 或同口径
端到端加速比。

## 2. 综合配置与版本对应性

| 项目 | 当前构建 |
|---|---|
| Vitis HLS | 2024.2，Build 5238294 |
| 工程 / Solution | `build` / `solution1`，Vivado IP Flow Target |
| 综合顶层 | `fsa_stream` |
| 目标器件 | `xcvu37p_CIV-fsvh2892-2-e`（Virtex UltraScale+ HBM） |
| 编译配置 | `SA_ROWS=4`，`SA_COLS=4`，`MAX_SEQUENCE_LENGTH=4096` |
| 数据格式 | Q/K/V 为 FP16，O/Accumulator 为 FP32 |
| 时钟目标 | 10.000 ns，即 100 MHz |
| 时钟不确定度 | 2.700 ns |
| 顶层控制 | AXI4-Lite + `ap_ctrl_hs` |
| 外存接口 | 4 个独立 64-bit AXI4 master：`q_gmem/k_gmem/v_gmem/o_gmem` |
| CSim / C 综合 / CoSim 时间 | 2026-09-14 15:37 / 15:41 / 15:44（UTC+8） |

Tcl 实际选入 13 个 `src/stream/` 实现文件和
`tests/stream/test_fsa_streaming_v2_top.cpp`。参与构建的本地实现文件、测试文件和
Tcl 的修改时间均早于 15:37；其中最新的
`src/stream/systolic_array.cpp` 修改时间为 14:49:16，未发现“本地源码晚于构建”的
迹象。构建元数据保留 Linux 服务器路径，而当前检查的是 Windows 副本，因此时间戳只能
支持版本对应性，不能证明两端逐字节相同；所有数值以生成产物为准。

当前 SA 使用 `PE_TOKEN_LATENCY=9`、guard=7、hop=16，并以
`cycle % PE_HOP_CYCLES` 选择 PE 环形槽。只对 `pe_register` 保留人工依赖提示，
没有恢复曾导致 RTL 数值错误的 `pe_pipeline/cmp_pipeline inter false`。

## 3. 流程结果

| 阶段 | 状态 | 证据与边界 |
|---|---|---|
| C 仿真 | **通过** | 日志含 PASS，0 error；一次调用产生完整 9×4 causal/non-causal 输出 |
| C 综合 | **完成** | 顶层、DATAFLOW、DMA、Scratchpad、Delayer、SA、PE/CMP、Accumulator 报告齐全 |
| RTL 协同仿真 | **通过** | Verilog/xsim，3 笔事务，总执行 4,432 cycles |
| IP 导出 | **未执行** | Tcl 中 `EXPORT_IP=0`，未发现 `component.xml` 或导出压缩包 |
| Vivado 综合/实现 | **未执行或未提供** | 没有实现后资源、WNS、DCP 或 bitstream |
| FPGA 板级验证 | **未执行或未提供** | 没有 Hardware Manager、VIO/ILA 或板上软件结果 |

生成的 HLS RTL 只证明 C 综合完成，不等价于 IP 已打包，更不等价于 Vivado 已完成
综合、布局布线或板级验证。

## 4. 功能验证范围

HLS Tcl 选入的测试平台依次执行：

| CoSim事务 | 场景 | RTL latency | 下一事务 interval | 100 MHz 换算 |
|---:|---|---:|---:|---:|
| 0 | `L=9, D=4, causal=false` | **2,569 cycles** | 2,559 cycles | 25.69 us |
| 1 | `L=9, D=4, causal=true` | **1,828 cycles** | 1,818 cycles | 18.28 us |
| 2 | `sequence_length=0`，非法请求 | **55 cycles** | 不适用 | 0.55 us |

CoSim 汇总的 min/avg/max latency 为 55/1,484/2,569 cycles，interval 为
1,818/2,188/2,559 cycles，总执行时间为 4,432 cycles。平均 latency 混入 55-cycle
快速拒绝事务，不能作为正常 attention 的代表值。

有效用例通过独立软件 softmax 金标准逐元素比较完整 9×4 输出，要求结果有限且绝对误差
不超过 0.18；同时检查状态码和输出尾部 canary。非法长度用例检查
`INVALID_SEQUENCE_LENGTH`，并确认 O 内存未被写入。CSim 日志报告本次向量下
`hls::stream` 最大观测深度为 1,989。

本次没有覆盖最大长度、更多非整 tile 长度、随机/极值/NaN/Inf、长时间 AXI
backpressure、AXI 错误响应、连续大量事务、auto-restart、中断、真实 HBM 或板级软件。

## 5. 时序与吞吐

### 5.1 HLS 时序估算

| 指标 | 数值 |
|---|---:|
| 目标周期 | 10.000 ns |
| 时钟不确定度 | 2.700 ns |
| 有效组合逻辑预算（计算值） | 7.300 ns |
| 顶层 HLS 估算周期 | **7.300 ns** |
| 有效预算裕量（计算值） | **0.000 ns** |
| 估算 Fmax（计算值） | **136.99 MHz** |

有效预算按 `10.000 - 2.700 = 7.300 ns` 计算，估算 Fmax 按
`1000 / 7.300` 计算。Solution 日志没有 `HLS 200-871` 时序约束告警，但 0 ns
HLS 裕量意味着任何 RTL、接口或布局变化都可能重新暴露关键路径。该估算不是 routed WNS。

### 5.2 关键模块和循环

| 模块或循环 | Latency | Interval / II | 说明 |
|---|---:|---:|---|
| 顶层静态估计 | 2 ～ 137,868,871,780 | 3 ～ 137,868,871,781 | 动态长度导致的极保守上界，不代表 9×4 实测 |
| `fsaStreamingDataflow` | 331 ～ 137,868,871,778 | 243 ～ 137,868,871,682 | 顶层 DATAFLOW 网络 |
| `systolicArrayProcess` | 242 ～ 250,611,713 | 同 latency | 单套 SA 处理可变 tile 数 |
| `spatialSystolicArrayTileTick` | **237** | **222** | 固定 4×4 单 tile |
| SA 主循环 | 235 | **II=1** | trip count=221，iteration latency=16 |
| 每个 `spatialPeCell` | 9 | **II=1** | 16 个空间实例 |
| 每个 `spatialCmpOutputCell` | 3 | **II=1** | 4 个空间实例 |
| `accumulatorArithmeticVector` | 10 | **II=1** | 4 个并行 lane |
| Input Delayer 关键循环 | 8 | **II=1** | trip count=7 |
| Output Delayer 关键循环 | 可变 | **II=1** | token 协议流水 |
| Scratchpad 关键循环 | 4 或 8 | **II=1** | Q/K/V banked SRAM 路径 |
| Q/K/V DMA read循环 | 可变，pipeline depth=78 | **II=4** | FIFO 写循环携带依赖仍限制吞吐 |
| O DMA write循环 | 可变，pipeline depth=3 | **II=1** | 写回循环达到目标 |

SA 主循环的 II=1 表示微程序每拍启动一次新的循环迭代，不表示每拍启动一个新 tile；
tile interval 仍为 222 cycles。顶层使用非流水 `ap_ctrl_hs`，也不能把任一局部
II=1解释成“每拍接收一个完整 attention 请求”。

## 6. 空间并行性与数据通路

综合层级和 RTL 同时显示唯一一个 `systolicArrayProcess_U0`，其 tile 层次内有
16 个 `spatialPeCell_<row>_<col>` 和 4 个
`spatialCmpOutputCell_<col>`。DATAFLOW 中还有唯一的
`accumulatorProcess_U0`，其中 `accumulatorArithmeticVector` 为 4 个独立列 lane。

DSP 对账如下：

```text
16 × PE  × 5 DSP = 80 DSP
 4 × CMP × 2 DSP =  8 DSP
单套 4×4 SA      = 88 DSP

4 × Acc lane × 5 DSP = 20 DSP
顶层合计             = 108 DSP
```

当前 Output Delayer 不再使用 DSP。综合层级、实例数和 DSP 对账共同证明当前是单套 4×4
SA，而不是复制多套阵列换吞吐。

## 7. 资源与存储映射

### 7.1 顶层资源

| 资源 | 使用 | 器件可用 | 精确利用率（计算值） | HLS 单SLR利用率 |
|---|---:|---:|---:|---:|
| BRAM_18K | 20 | 4,032 | 0.50% | 1% |
| DSP | 108 | 9,024 | 1.20% | 3% |
| FF | 74,357 | 2,607,360 | 2.85% | 8% |
| LUT | 109,091 | 1,303,680 | 8.37% | 25% |
| URAM | 0 | 960 | 0.00% | 0% |

### 7.2 DATAFLOW 主要实例

| 实例 | BRAM_18K | DSP | FF | LUT |
|---|---:|---:|---:|---:|
| `systolicArrayProcess_U0` | 0 | 88 | 50,024 | 65,438 |
| `accumulatorProcess_U0` | 8 | 20 | 12,286 | 27,053 |
| `scratchpadProcess_U0` | 4 | 0 | 278 | 1,618 |
| `dmaReadQ/K/V_U0` 合计 | 0 | 0 | 3,128 | 3,163 |
| `outputDelayerProcess_U0` | 0 | 0 | 136 | 408 |
| DATAFLOW 总计（含 FIFO/控制） | **12** | **108** | **70,784** | **104,773** |

顶层额外 8 个 BRAM 来自 4 个 AXI master 适配器，每个占 2 BRAM。DATAFLOW 内部 12 个
BRAM 由 Scratchpad 的 4 个和 Accumulator 的 8 个组成。由于还没有布局布线，无法确认
25% 单SLR LUT 估算、高扇出选择网络和 AXI/FIFO 互连的最终收敛情况。

## 8. 顶层接口

| C/C++端口 | 方向 | 物理接口与宽度 | 配置与作用 |
|---|---|---|---|
| `q_address` | 输入 | `m_axi_q_gmem`，64-bit | 16 outstanding read，burst≤64，读取Q |
| `k_address` | 输入 | `m_axi_k_gmem`，64-bit | 16 outstanding read，burst≤64，读取K |
| `v_address` | 输入 | `m_axi_v_gmem`，64-bit | 16 outstanding read，burst≤64，读取V |
| `o_address` | 输出 | `m_axi_o_gmem`，64-bit | 8 outstanding write，burst≤64，写回O |
| `sequence_length` | 输入 | AXI4-Lite，32 bit | 序列长度 |
| `causal` | 输入 | AXI4-Lite，1 bit | 因果掩码 |
| `status` | 输出 | AXI4-Lite，8 bit | 返回状态 |
| `return` | 控制 | AXI4-Lite + `ap_ctrl_hs` | start/done/idle/ready和中断 |

`max_widen_bitwidth=512` 是允许自动拓宽的上限；本次综合生成的物理 AXI data 端口仍为
64 bit，不能按 512 bit 估算实际带宽。四个 master 只有在系统集成时映射到合适的独立
HBM bank/伪通道，才能保留接口层面的并行优势。

## 9. 与 `fsa_dma_top` 的对比

### 9.1 对比口径

`fsa_dma_top` 的当前构建产物不在工作区，本节基线来自已有报告
`docs/综合报告/fsa_dma综合报告.md`，其记录的产物时间为
2026-08-24 17:42:40 至 17:52:27。两边均使用 Vitis HLS 2024.2、同一
`xcvu37p_CIV-fsvh2892-2-e`、4×4、100 MHz目标和2.7 ns uncertainty。

延迟只比较两边都覆盖的 `L=9, D=4, causal=false` 完整输出事务。
`fsa_dma_top` 只测一笔非因果事务；当前 `fsa_stream` 还额外测试了因果和非法长度。
由于源码架构、AXI端口数和测试平台不同，资源与延迟差异应理解为两个顶层实现的整体差异，
不能归因于某一条 pragma。

### 9.2 同口径结果

| 指标 | `fsa_dma_top` | 当前 `fsa_stream` | 当前相对变化 |
|---|---:|---:|---:|
| 9×4 non-causal RTL CoSim | 27,566 cycles | **2,569 cycles** | **-24,997，-90.68%，约10.73×加速** |
| HLS估算周期 | 7.300 ns | **7.300 ns** | 相同，均为0 ns有效裕量 |
| BRAM_18K | 4 | **20** | +16，+400% |
| DSP | 303 | **108** | -195，-64.36% |
| FF | 105,648 | **74,357** | -31,291，-29.62% |
| LUT | 128,957 | **109,091** | -19,866，-15.41% |
| 外存接口 | 1个共享64-bit AXI master | 4个独立64-bit AXI master | 当前可并行搬运Q/K/V/O |
| 计算结构 | 1套约300-DSP共享数据通路 | 1套88-DSP SA + 20-DSP Acc | 当前算术资源更低 |
| 主要局部瓶颈 | 请求调度循环II=39 | DMA read循环II=4 | 瓶颈已转移 |

当前版本用更多 BRAM 换取显式 Scratchpad/Accumulator bank 和四个 AXI 适配器，但显著减少
算术及逻辑资源，并把同一非因果事务的 RTL latency 降低约一个数量级。两者 HLS 时序都只
达到0 ns裕量，因此性能提升不等于实现后时序风险已经消失。

## 10. 与 `FSA-main` 的对比

### 10.1 可确认的结构对应关系

| 项目 | `FSA-main` Chisel | 当前 `fsa_stream` | 结论 |
|---|---|---|---|
| 阵列 | 单套参数化R×C SA | 单套4×4 SA | 一致 |
| PE状态 | 唯一 `PE.reg` 保存Q/S/P | 唯一 `pe_register` 保存Q/S/P | 语义一致 |
| 比较器 | 每列一个CMP | 4列共4个CMP | 一致 |
| Softmax | score、max/diff、scale、8段exp2、row sum | 同阶段顺序 | 一致 |
| Accumulator | 每列lane更新L/O并归一化 | 4个lane，latency=10、II=1 | 结构对应 |
| 片上存储 | banked Scratchpad + AccRAM | 4 BRAM + 8 BRAM | 结构对应 |
| 输入输出对齐 | Input/Output Delayer | 独立DATAFLOW actor | 功能对应 |
| 控制 | 两个FSM读取逐周期ExecutionPlan，可重叠指令 | 221次静态tile微程序循环 | 调度表达不同 |
| PE运算节拍 | Chisel按逐拍控制传播 | PE latency=9，以16拍hop发射token | 当前HLS存在额外hop |

### 10.2 静态周期对比

`FSA-main/src/main/scala/fsa/ExecutionPlan.scala` 在4×4、8段PWL、reciprocal latency=14
时给出的独立指令周期为：

| `FSA-main`指令 | 独立总周期 |
|---|---:|
| `LOAD_STATIONARY` | 5 |
| `ATTENTION_SCORE` | 28 |
| `ATTENTION_VALUE` | 12 |
| `ATTENTION_LSE_NORM_SCALE` | 16 |
| `ATTENTION_LSE_NORM` | 5 |

一次 score+value 独立周期合计为40拍。当前HLS单个SA tile interval为222拍，数值上是
40拍的5.55倍；但两者不是完全相同的事务边界：HLS tile还包含Q/K/V Delayer消费、
9拍PE返回和16拍token hop，而Chisel允许在 `conflictFree` 周期重叠相邻matrix指令。
因此5.55倍只能说明HLS调度仍比原逐拍微程序保守，不能当成端到端实测加速比。

若完全忽略FSA指令间重叠、DMA、队列和片外存储，按独立指令简单求和，9×4静态计算为：

| 模式 | 计算方式 | 非重叠静态和 |
|---|---|---:|
| non-causal | 3个query tile × (5+16+5) + 9个QK tile × (28+12) | 438 cycles |
| causal | 3个query tile × (5+16+5) + 6个有效QK tile × (28+12) | 318 cycles |

这些是从源码微程序推导的计算值，不是 `FSA-main` RTL/FPGA实测。当前HLS对应的
2,569/1,828 cycles还包含DMA和顶层协议，因此不能直接用438/318计算真实速度差。

### 10.3 可用的板级示例及限制

`FSA-main/README.md` 记录了一次U55C板级示例：
`seq_q=16, seq_kv=16, config=EmptyU55CConfig`，execution time为9,414 cycles，
其中max bubble 6,535、max active 179、DMA active 233 cycles。该示例与当前HLS的
9×4、`xcvu37p`、xsim CoSim在序列长度、配置、器件、存储系统和验证阶段上均不同，
所以报告不计算二者加速比。

当前工作区没有 `FSA-main` 的同配置综合资源、估算周期、Vivado WNS或9×4事务报告。
因此以下项目只能记为“无法确认”，不能用当前HLS数据代填：

- `FSA-main` 4×4在 `xcvu37p` 上的DSP/BRAM/FF/LUT；
- 同一时钟约束下的HLS/Vivado时序；
- 同一9×4输入、相同DMA和HBM环境下的端到端cycles；
- 同一误差阈值下的逐元素结果比较。

## 11. 警告与风险

Solution 日志共有181条warning、0条error。主要分组如下：

| 告警代码 | 数量 | 影响判断 |
|---|---:|---|
| `RTGEN 206-101` | 85 | AXI未使用方向置零、RTL重命名和复位提示；集成脚本应使用生成端口名 |
| `SYN 201-103` | 34 | 模板函数名合法化；脚本不应依赖原C++符号名 |
| `ANALYSIS 214-52` | 32 | 工具接受16个PE寄存器的人工false dependency；周期表变化后必须重跑RTL CoSim |
| `HLS 200-880` | 9 | Q/K/V DMA read FIFO写依赖使目标II=1最终为II=4 |
| `HLS 200-960` | 8 | 动态/非完美嵌套循环无法flatten，保留额外控制开销 |
| `BIND 205-102` | 4 | 存储绑定可能在更简单实现可用时被忽略；应以实际BRAM报告为准 |
| `HLS 200-1020` | 3 | 工具自动加深部分start FIFO以改善性能或避免死锁 |
| `HLS 200-1449` | 3 | DMA read既有前驱又读取caller输入，可能降低DATAFLOW吞吐 |
| `HLS 200-1018` | 1 | 建议继续加深 `control_to_spad` FIFO |
| `HLS 214-358` | 1 | SA数组索引含bit extension逻辑，可能恶化QoR |
| `HLS 200-1995` | 1 | Compile/Link后设计规模为292,807条指令 |

当前没有 `HLS 200-871`、CoSim deadlock或数值错误。最需要继续处理的是DMA read II=4、
顶层0 ns时序裕量、PE依赖提示对固定微程序的脆弱性，以及尚未覆盖的AXI backpressure。

## 12. 当前合格性结论

| 检查项 | 结论 |
|---|---|
| CSim功能验证 | **通过：9×4非因果、9×4因果、长度0拒绝** |
| C综合 | **完成：顶层及关键子模块报告齐全** |
| RTL CoSim | **通过：Verilog/xsim，3笔事务，无死锁和数值错误** |
| 单次调用完成完整attention | **在当前9×4 causal/non-causal用例中达到** |
| 单套4×4 SA | **确认：16 PE + 4 CMP，88 DSP** |
| SA主循环II=1 | **达到** |
| PE/CMP/Acc局部II=1 | **达到** |
| Tile或顶层事务II=1 | **未达到：tile interval=222，顶层为非流水ap_ctrl_hs** |
| 100 MHz HLS时序估算 | **边界满足：有效裕量0.000 ns** |
| 相对 `fsa_dma_top` | **同用例latency降低90.68%，DSP/FF/LUT均降低** |
| 相对 `FSA-main` | **结构和阶段顺序基本对应；同口径资源/端到端性能无法确认** |
| IP导出 | **未执行** |
| Vivado实现与板级可用性 | **尚未证明** |

综上，当前版本已经达到“现有测试下C/RTL功能一致、单套4×4 SA、SA主循环II=1、同口径
性能显著优于旧 `fsa_dma_top`”的阶段目标。当前最主要的剩余性能问题是DMA read II=4；
最主要的交付风险是0 ns HLS时序裕量、缺少实现后时序和有限的CoSim覆盖。

## 13. 后续工作

1. 单变量处理Q/K/V DMA read FIFO写依赖，目标从II=4降到II=1，并完整复测
   CSim、综合和RTL CoSim。
2. 增加非整tile长度、最大长度、随机/极值、连续事务和AXI backpressure/error测试。
3. 保持 `cycle%16` 静态PE slot；禁止恢复已经造成48个RTL错误的
   `pe_pipeline/cmp_pipeline inter false`。
4. 在不改变100 MHz和2.7 ns uncertainty的前提下增加HLS时序裕量，然后执行Vivado
   综合与布局布线确认WNS。
5. 若要严谨量化与 `FSA-main` 的差距，应在同一4×4配置、相同9×4输入、相同内存模型
   和相同时钟下生成其RTL仿真或Vivado结果，再补充同口径表格。

项目约定的HLS重跑命令为 `./run_hls.sh fsa_stream`；本报告没有执行该命令。

## 14. 关键结果文件

以下路径均相对于 `build/fsa_stream_build/solution1`：

- 顶层综合：`syn/report/fsa_stream_csynth.rpt`
- DATAFLOW：`syn/report/fsaStreamingDataflow_csynth.rpt`
- SA进程：`syn/report/systolicArrayProcess_csynth.rpt`
- 单tile：`syn/report/spatialSystolicArrayTileTick_csynth.rpt`
- PE：`syn/report/peMacUnit_csynth.rpt`
- CMP：`syn/report/spatialCmpOutputCell_0_s_csynth.rpt`
- Accumulator：`syn/report/accumulatorProcess_csynth.rpt`
- CSim：`csim/report/fsa_stream_csim.log`
- RTL CoSim：`sim/report/fsa_stream_cosim.rpt`
- CoSim事务：`sim/report/verilog/result.transaction.rpt`
- Solution日志：`solution1.log`
- 生成RTL：`syn/verilog/`

对比来源：

- `docs/综合报告/fsa_dma综合报告.md`
- `../FSA-main/src/main/scala/fsa/ExecutionPlan.scala`
- `../FSA-main/src/main/scala/fsa/Configs.scala`
- `../FSA-main/README.md`
