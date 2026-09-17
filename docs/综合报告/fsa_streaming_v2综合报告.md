# `fsa_streaming_v2_top` 综合报告

## 1. 结论摘要

本报告读取 `build/fsa_streaming_v2_build/solution1` 的现有产物编写，没有重新运行
Vitis HLS、Vivado 或板级测试，也没有修改算法源码、测试平台、时钟、器件或接口。

当前版本已经完成并通过 CSim、C 综合和 Verilog/xsim RTL CoSim。对于同口径的
`L=9`、`D=4` 非因果完整 attention，RTL CoSim 延迟由旧 `fsa_dma` 报告中的
**27,566 cycles** 降至 **2,499 cycles**，减少 **25,067 cycles（90.93%）**，
等效为 **11.03 倍加速**。因果 9×4 用例为 **2,465 cycles**。

硬件规模也明显下降：DSP 从 303 降至 148，FF 从 105,648 降至 78,216，LUT 从
128,957 降至 91,976。代价是 BRAM_18K 从 4 增至 18，主要来自 Q/K/V/O 四个独立
AXI master 缓冲以及内部 Scratchpad/Accumulator SRAM。

阵列层级中只有一套 4×4 SA：16 个命名 PE 共 80 DSP，4 个阵列出口 CMP 共 8 DSP，
SA 合计 88 DSP；没有第二套 80-DSP PE 阵列。PE 和 CMP 的局部模块均达到 II=1。
但一次 tile 的 `spatialSystolicArrayTileTick` 仍需 222 cycles、Interval=222，说明
“单 PE 每拍可接收新数据”和“每拍可启动新 tile”不是同一层吞吐指标。

## 2. 综合配置与产物对应性

| 项目 | 当前构建 |
|---|---|
| Vitis HLS | 2024.2，Build 5238294 |
| 综合顶层 | `fsa_streaming_v2_top` |
| Solution | `solution1`，Vivado IP Flow Target |
| 目标器件 | `xcvu37p_CIV-fsvh2892-2-e`（Virtex UltraScale+ HBM） |
| 构建参数 | `SA_ROWS=4`，`SA_COLS=4`，`MAX_SEQUENCE_LENGTH=4096` |
| 数据格式 | FP16 Q/K/V，FP32 O |
| 时钟目标 | 10.00 ns，即 100 MHz |
| 时钟不确定度 | 2.70 ns |
| 有效组合逻辑预算 | 7.30 ns |
| 控制接口 | AXI4-Lite，`ap_ctrl_hs` |
| 数据接口 | 4 个独立 64-bit AXI4 master：`q_gmem/k_gmem/v_gmem/o_gmem` |
| 报告时间 | C 综合 2026-09-05 01:33，CoSim 2026-09-05 01:36（UTC+8） |

本地顶层、核心、DMA、Accumulator 和测试平台的修改时间均早于本次构建；Tcl 在
2026-09-05 01:29 修改，构建随后执行。日志明确分析了对应五个源文件，并且接口、
测试事务和 PASS 标记与本地文件一致，未发现构建落后于本地源码的迹象。产物中记录的
源路径来自 Linux 服务器，而当前检查的是 Windows 副本，因此不能仅凭时间戳证明两端
文件逐字节相同；以下结论以构建产物为准。

## 3. 流程完成情况

| 阶段 | 状态 | 证据与限制 |
|---|---|---|
| C 仿真 | **通过** | 0 error；覆盖 9×4 非因果、9×4 因果和长度 0 非法请求 |
| C 综合 | **完成** | 顶层、Dataflow、DMA、SRAM、SA、CMP、PE 和 Acc 报告齐全 |
| RTL 协同仿真 | **通过** | Verilog/xsim，3 个事务，总执行 4,999 cycles |
| IP 导出 | **未执行** | Tcl 中 `EXPORT_IP=0`；没有可交付的 `component.xml`/`export.zip` |
| Vivado 综合/实现 | 未执行/未提供 | 没有实现后资源、WNS、跨 SLR 布线或实际 Fmax |
| FPGA 板级验证 | 未执行/未提供 | 没有真实 DDR/HBM、AXI-Lite 驱动或板上功能证据 |

`impl/verilog` 和 `impl/vhdl` 下存在 HLS 生成的 RTL 辅助文件，但这不等价于完成
`export_design`，更不等价于 Vivado 布局布线或板级验证。

## 4. 功能验证范围与 CoSim 事务

测试平台通过真实顶层调用，一次顶层调用完成一次完整 attention，而不是由软件逐 tile
调用 SA 或 Acc。三笔 CoSim 事务与测试代码的调用顺序一一对应：

| 事务 | 场景 | RTL 延迟 | 下一事务启动间隔 | 100 MHz 换算 |
|---:|---|---:|---:|---:|
| 0 | `L=9, D=4, causal=false` | **2,499 cycles** | 2,489 cycles | 24.99 us |
| 1 | `L=9, D=4, causal=true` | **2,465 cycles** | 2,455 cycles | 24.65 us |
| 2 | `sequence_length=0`，快速拒绝 | 55 cycles | NA | 0.55 us |

CoSim 汇总给出的 min/avg/max latency 为 55/1,673/2,499 cycles。这里的平均值混入了
55-cycle 非法请求，不能作为 9×4 计算性能；两个有效 9×4 用例平均为 2,482 cycles。
同理，当前只有两笔有效计算和一笔非法请求，事务 interval 只能作为该测试序列的观测值，
不能直接宣称任意连续请求都具有稳定的 2,455-cycle 顶层 II。

有效用例还检查：返回状态为 OK、完整 9×4 输出与软件 softmax 金标准的绝对误差不超过
0.18、结果有限，以及输出尾部 canary 未被覆盖。非法长度用例检查错误状态并确认 O 不写回。

当前未覆盖最大长度、其他非整 tile 长度、随机/极值/NaN/Inf、AXI backpressure/error、
真实 DDR/HBM、连续大量 start、auto-restart、中断和板级软件流程。

## 5. 时序与吞吐

### 5.1 HLS 时序估算

| 指标 | 当前值 |
|---|---:|
| 目标周期 | 10.000 ns |
| 时钟不确定度 | 2.700 ns |
| 有效预算 | 7.300 ns |
| 顶层 HLS 估算周期 | **7.300 ns** |
| 有效预算裕量 | **0.000 ns** |
| HLS 日志估算 Fmax | 136.99 MHz |

当前 HLS 调度恰好落在有效预算边界，日志没有 II 违例或 `HLS 200-871` 时序违例。
136.99 MHz 是 `1000/7.300` 的 HLS 估算，不是布局布线后的可工作频率。0 ns HLS 裕量
意味着仍必须用 Vivado 实现结果确认 100 MHz 的 WNS。

### 5.2 层级延迟与 II

| 模块/循环 | Latency | Interval / II | 解释 |
|---|---:|---:|---|
| `fsaStreamingDataflow` | 最小 342 | 最小 264 | 顶层 DATAFLOW 网络；动态长度上界非常保守 |
| `systolicArrayProcess` | 最小 263 | 最小 263 | 包含 tile 控制、数据装填和唯一 SA |
| `spatialSystolicArrayTileTick` | **222** | **222** | 一次 tile 波前调用，tile 间当前不重叠 |
| SA 主波前循环 | 218 | Interval 204 | 循环 trip count=203 |
| SA 主波前循环单迭代 | 15 | **II=1** | 达到目标 II=1 |
| 每个 `spatialPeCell` | 9 | **II=1** | 16 个空间实例 |
| 每个 `spatialCmpOutputCell` | 3 | **II=1** | 4 个阵列出口实例 |
| `accumulatorProcess` | 最小 48 | 最小 48 | 动态循环导致静态最大值极保守 |
| Acc 四 lane MAC 循环 | 15 | **II=1** | trip count=5，模块 latency=17、interval=6 |
| Acc 最终输出循环 | 0～65,535 | **II=1** | 动态输出长度 |

顶层 C 综合给出的最大延迟约 68.86 billion cycles，是把 `MAX_SEQUENCE_LENGTH=4096`
的多层动态循环上界组合后的保守值，不代表 9×4。当前 9×4 应以 RTL CoSim 的
2,499/2,465 cycles 为准。

## 6. 当前硬件结构与资源对账

综合出的主路径为：

```text
AXI-Lite 参数与 ap_start
        |
        v
Q/K/V 独立 DMA Read --> Stream --> Scratchpad SRAM --> 单套 4x4 SA + 出口 CMP
                                                       |
                                                       v
                                               Acc/Softmax SRAM
                                                       |
                                                       v
                                             Output Pack --> O DMA Write
```

顶层 `fsaStreamingDataflow` 中只有一个 `systolicArrayProcess_U0` 和一个
`accumulatorProcess_U0`。SA 层级只有一个 `spatialSystolicArrayTileTick`，其内部实例为：

```text
16 × spatialPeCell × 5 DSP = 80 DSP
 4 × spatialCmpOutputCell × 2 DSP = 8 DSP
单套 SA 合计                     = 88 DSP
```

因此当前没有第二套 80-DSP PE 阵列。完整 148 DSP 可继续精确对账：

```text
SA（16 PE + 4 CMP） = 88 DSP
Accumulator         = 52 DSP
K/V DMA 地址计算     =  8 DSP
总计                 = 148 DSP
```

Accumulator 的 52 DSP 包含 4 个 `accExp2PWL`、4 个独立 MAC lane 和 4 路最终归一化
乘法；reciprocal 模块本身报告为 0 DSP。Q DMA 和 O DMA 为 0 DSP。

### 6.1 顶层资源

| 资源 | 使用 | 器件可用 | 精确计算利用率 | 单 SLR 报告利用率 |
|---|---:|---:|---:|---:|
| BRAM_18K | 18 | 4,032 | 0.45% | 1% |
| DSP | 148 | 9,024 | 1.64% | 4% |
| FF | 78,216 | 2,607,360 | 3.00% | 8% |
| LUT | 91,976 | 1,303,680 | 7.06% | 21% |
| URAM | 0 | 960 | 0.00% | 0% |

### 6.2 Dataflow 子模块资源

| 子模块 | BRAM_18K | DSP | FF | LUT |
|---|---:|---:|---:|---:|
| `systolicArrayProcess` | 0 | 88 | 47,905 | 65,668 |
| `accumulatorProcess` | 4 | 52 | 7,632 | 14,854 |
| `scratchpadProcess` | 6 | 0 | 352 | 2,206 |
| `dmaReadQ` | 0 | 0 | 5,507 | 659 |
| `dmaReadK` | 0 | 4 | 5,663 | 882 |
| `dmaReadV` | 0 | 4 | 5,663 | 882 |
| `dmaWriteO` | 0 | 0 | 298 | 662 |
| `outputPackProcess` | 0 | 0 | 179 | 436 |
| Dataflow 合计 | 10 | 148 | 74,643 | 87,658 |

顶层额外 8 个 BRAM 来自四个 AXI master 适配器，每个 2 BRAM；Dataflow 内部 10 BRAM
由 Scratchpad 的 6 BRAM 和 Accumulator SRAM 的 4 BRAM 构成。Dataflow stream/FIFO
本身主要使用 FF/LUT，报告合计 1,437 FF、1,313 LUT，未占 BRAM。

## 7. 接口

顶层调用参数包含 Q/K/V/O 地址、`sequence_length`、`causal` 和返回 `status`。控制面为
AXI4-Lite，事务协议为 `ap_ctrl_hs`；一次 `ap_start` 对应一次完整序列计算。

Q、K、V 各使用独立只读 64-bit AXI4 master，O 使用独立只写 64-bit AXI4 master。
这允许 Q/K/V 读取和 O 写回在 DATAFLOW 中从接口层并行推进；它也比旧 `fsa_dma` 的
单一共享 `gmem` 增加了 AXI 适配器资源。源码允许最大 512-bit 自动拓宽，但本次综合报告
中的实际物理数据端口仍为 64 bit，不能按 512 bit 带宽计算。

## 8. 与 `fsa_dma综合报告` 的同口径对比

旧基线取自 `docs/综合报告/fsa_dma综合报告.md` 中记录的最新 `fsa_dma_top`：同一器件、
10 ns 目标、`SA_ROWS=4`、`SA_COLS=4`，且 RTL CoSim 用例同为一次完整 9×4 非因果计算。

| 指标 | 旧 `fsa_dma` | 当前 streaming v2 | 变化 |
|---|---:|---:|---:|
| 非因果 9×4 RTL CoSim latency | 27,566 | **2,499** | **-25,067（-90.93%），11.03× 加速** |
| 非因果 9×4 @100 MHz | 275.66 us | **24.99 us** | -250.67 us |
| 因果 9×4 RTL CoSim latency | 未测试 | **2,465** | 新增覆盖 |
| 非法长度 RTL CoSim | 未测试 | **55** | 新增覆盖 |
| BRAM_18K | 4 | **18** | +14（+350.00%） |
| DSP | 303 | **148** | -155（-51.16%） |
| FF | 105,648 | **78,216** | -27,432（-25.97%） |
| LUT | 128,957 | **91,976** | -36,981（-28.68%） |
| URAM | 0 | 0 | 不变 |
| HLS 估算周期 | 7.300 ns | 7.300 ns | 不变，均为 0 ns 有效裕量 |
| 外部 AXI master | 1 个共享 64-bit | 4 个独立 64-bit | 带宽并行性增强，适配器面积增加 |
| 顶层结构 | current-next 请求调度 | DATAFLOW + stream | 前后级可并行，控制依赖显著减少 |
| RTL CoSim 覆盖 | 1 个非因果事务 | 2 个有效事务 + 1 个非法事务 | 覆盖增强 |
| IP 导出 | 未执行 | 未执行 | 均无可交付 IP 包 |

性能提升的根因不是只把某个循环标为 II=1，而是改变了抽象层级：旧路径在单共享 core
前以 current-next 状态机逐 logical step 调度，请求循环实际 II=39，且 Q/K/V/O 共用一个
AXI master；当前版本把 DMA、Scratchpad、SA、Acc 和写回拆成 DATAFLOW 进程，用 stream
解开阶段间控制依赖，同时将四类外存访问拆到独立 AXI master。

面积下降同样不是减少了 SA 数量而已。旧报告的唯一完整数据通路仍占 300 DSP，其中
SA stage 为 280 DSP；当前唯一 4×4 SA 只占 88 DSP，并能从 16 PE + 4 CMP 精确对账。
这说明旧路径内部的组合/状态推进写法使运算器被大规模展开或复制，而当前空间层级更接近
预期的一套物理阵列。BRAM 上升则是用 SRAM 和独立 AXI 缓冲换取流式并行性的直接代价。

## 9. 警告与风险

Solution 日志共有 **160 条 warning、0 条 error**：

| 告警代码 | 数量 | 判断 |
|---|---:|---|
| `RTGEN 206-101` | 85 | 多为只读/只写 AXI 未使用方向悬空置零、RTL 重命名和同步低有效复位提示 |
| `ANALYSIS 214-52` | 40 | 工具确认 PE 寄存器和 Acc SRAM 的相关性为 false；当前 II=1 已实现，但需回归验证其正确性 |
| `SYN 201-103` | 26 | 模板实例名称合法化，主要影响 RTL 命名 |
| `BIND 205-102` | 4 | Acc SRAM 的资源核约束可能被更简单实现替代；实际结果仍映射为 4 BRAM |
| `HLS 200-1020` | 1 | 工具将 `dmaWriteO` start FIFO 深度从默认值增至 6，以改善性能/避免死锁 |
| `HLS 200-1995` | 1 | Compile/Link 后设计规模 202,442 instructions，影响综合时间与维护性 |
| `HLS 214-189` | 1 | DMA 内循环已完全展开，因此 pipeline 指令被移除，通常无功能风险 |
| `HLS 214-358` | 1 | 位扩展参与数组索引，可能降低 QoR |
| `SYNCHK 200-23` | 1 | Scratchpad 中可变索引 range selection 可能降低 QoR |

当前没有 II 违例、调度失败或 CoSim deadlock。最值得继续跟踪的是：顶层 HLS 时序裕量
为 0 ns、SA 主波前控制扇出较大、数组索引 QoR 告警仍存在，以及 tile 调用本身仍是
222-cycle 非重叠事务。它们不影响本次 CSim/CoSim 通过，但可能影响布局布线和进一步吞吐。

## 10. 当前合格性判断

| 检查项 | 结论 |
|---|---|
| 一次顶层调用完成完整 9×4 | **达到，非因果和因果均通过 RTL CoSim** |
| 顶层参数含序列长度与 Q/K/V/O 地址 | **达到** |
| 完整 DMA/SRAM/SA/Acc 核 | **达到** |
| 单套 SA | **达到：唯一 4×4 SA，16 PE + 4 CMP，共 88 DSP** |
| CMP 处于阵列输出侧 | **综合层级确认 4 个 `spatialCmpOutputCell` 与 16 PE 同属唯一 tile tick** |
| PE/CMP 局部 II=1 | **达到** |
| Acc MAC 与最终输出循环 II=1 | **达到** |
| 9×4 性能相对旧 DMA | **11.03× 加速（非因果同口径）** |
| HLS 100 MHz 时序估算 | **边界满足，0 ns 有效裕量** |
| Vivado 实现/板级可用性 | **尚未证明** |

## 11. 后续建议

1. 首先导出 IP 并执行 Vivado 综合、布局布线，确认 100 MHz WNS、真实资源、单 SLR
   放置以及 SA 高扇出控制路径；这是当前最重要的缺失证据。
2. 在不复制 SA 的前提下，研究把 tile tick 的 222-cycle 调用开销进一步贴近 FSA 的
   逐拍波前模型；判断标准应是 tile latency/interval，而不是只看 PE 内循环 II=1。
3. 消除 `HLS 214-358` 和 `SYNCHK 200-23` 的数组索引 QoR 告警，再检查 LUT、扇出和时序。
4. 增加多种长度、最大长度、长时间 AXI backpressure 和连续多事务 CoSim，得到可靠的
   顶层 steady-state interval，而不是从当前三笔混合事务外推。
5. 若系统互连允许，把四个 AXI master 映射到独立 HBM bank；否则四口在互连处重新汇聚，
   HLS 中的并行性不一定能转化为板上带宽。

## 12. 关键结果文件

以下路径均相对于 `build/fsa_streaming_v2_build`：

- 顶层综合：`solution1/syn/report/fsa_streaming_v2_top_csynth.rpt`
- DATAFLOW：`solution1/syn/report/fsaStreamingDataflow_csynth.rpt`
- SA：`solution1/syn/report/systolicArrayProcess_csynth.rpt`
- 单 tile 波前：`solution1/syn/report/spatialSystolicArrayTileTick_csynth.rpt`
- SA 主循环：`solution1/syn/report/spatialSystolicArrayTileTick_Pipeline_VITIS_LOOP_706_3_csynth.rpt`
- Acc：`solution1/syn/report/accumulatorProcess_csynth.rpt`
- Scratchpad：`solution1/syn/report/scratchpadProcess_csynth.rpt`
- CSim：`solution1/csim/report/fsa_streaming_v2_top_csim.log`
- RTL CoSim：`solution1/sim/report/fsa_streaming_v2_top_cosim.rpt`
- CoSim 事务：`solution1/sim/report/verilog/result.transaction.rpt`
- Solution 日志：`solution1/solution1.log`

旧路径对比基线：`docs/综合报告/fsa_dma综合报告.md`。
