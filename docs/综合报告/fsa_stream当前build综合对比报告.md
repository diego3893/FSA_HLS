# `fsa_stream` 当前 Build 综合与对比报告

## 1. 报告范围与证据口径

本报告读取当前 `build/fsa_stream_build/solution1` 的已有产物编写，没有重新运行
Vitis HLS、Vivado 或板级测试，也没有修改算法源码、测试平台、器件、时钟或接口。

报告包含两组对比：

1. 当前 `fsa_stream` 与“手写 FMA 前”的 `fsa_stream`；
2. 当前 `fsa_stream` 与 `fsa_dma`。

三组数据的证据等级如下：

| 对象 | 数据来源 | 可复核性 |
|---|---|---|
| 当前 `fsa_stream` | 当前 `solution1` 的 CSim、CSynth、CoSim、RTL 和日志 | **可由当前工作区直接复核** |
| 手写 FMA 前 `fsa_stream` | 2026-09-17 14:09 的已验证基线记录；该 build 后来被覆盖 | 历史已验证数据，原始 build 当前不可逐项重读 |
| `fsa_dma` | 留存的 `docs/综合报告/fsa_dma综合报告.md` | 完整历史报告可复核，但原 `build/fsa_dma_build` 当前不存在 |

“手写 FMA 前”采用 DMA 已达到 II=1 后、PE/Accumulator 尚使用标准 HLS 浮点运算的最后一版，
因此能较好隔离手写 FMA 带来的变化。各架构接口和并行度不同，跨架构加速比只表示当前
`L=9、D=4` 测试事务的 RTL Co-sim 周期差，不能直接外推到任意序列长度或真实板上带宽。

## 2. 当前综合配置

| 项目 | 当前 build |
|---|---|
| Vitis HLS | 2024.2，Build 5238294 |
| 综合顶层 | `fsa_stream` |
| Solution | `solution1`，Vivado IP Flow Target |
| 目标器件 | `xcvu37p_CIV-fsvh2892-2-e` |
| 配置 | `SA_ROWS=4`，`SA_COLS=4`，`MAX_SEQUENCE_LENGTH=4096` |
| 数据格式 | FP16 Q/K/V，FP32 O |
| 时钟目标 | 10.000 ns，即 100 MHz |
| 时钟不确定度 | 2.700 ns |
| 有效组合逻辑预算 | 7.300 ns |
| 控制接口 | 32-bit AXI4-Lite，`ap_ctrl_hs` |
| 数据接口 | 4 个独立 64-bit AXI4 master：Q/K/V/O |
| 当前构建产物时间范围 | 2026-09-21 01:07:09 至 01:16:52（UTC+8） |

当前预处理源码可确认 `PE_HOP_CYCLES=5`，PE 和 Accumulator 使用手写原始位级 FMA，
CMP 使用独立减法/比较路径。源码修改时间早于本次 CSim 和综合时间，未发现当前 build
明显落后于当前源码的迹象。

## 3. 当前流程结果

| 阶段 | 状态 | 证据与限制 |
|---|---|---|
| C 仿真 | **通过** | 同一次测试完成 `L=9、D=4` 的 non-causal、causal 和非法长度三个事务，0 error |
| C 综合 | **完成** | 顶层、DATAFLOW、SA、CMP、Accumulator、DMA 和关键循环报告存在 |
| RTL 协同仿真 | **通过** | Verilog/xsim；C 后检查无 mismatch、无 deadlock |
| IP 导出 | **未执行** | `EXPORT_IP=0`；无 `component.xml`、导出 zip 或 `.xo` |
| Vivado 综合/实现 | 未执行/未提供 | 没有实现后资源、WNS、布线或实际 Fmax |
| FPGA 板级验证 | 未执行/未提供 | 没有真实 DDR/HBM、驱动或板上结果 |

测试平台会计算完整 softmax 金标准，逐元素检查结果有限且绝对误差不超过 0.18；同时检查
O 输出尾部 canary 未被覆盖。覆盖了 non-causal、causal、最后一个非整 tile，以及
`sequence_length=0` 的拒绝路径；尚未覆盖长序列、NaN/Inf、AXI backpressure/error、
连续多次 start 和真实存储器行为。

## 4. 当前时序、延迟与吞吐

### 4.1 HLS 时序估算

| 指标 | 当前值 |
|---|---:|
| 目标周期 | 10.000 ns |
| 时钟不确定度 | 2.700 ns |
| 有效预算 | 7.300 ns |
| 顶层 HLS 估算周期 | **7.300 ns** |
| 有效预算裕量 | **0.000 ns** |
| 估算周期的倒数 | 约 136.99 MHz |
| SA tile 估算周期 | 7.120 ns |
| Accumulator arithmetic vector 估算周期 | 7.299 ns |

当前在 HLS 估算中恰好满足 100 MHz 约束，日志没有 `HLS 200-871` 时序违例。
136.99 MHz 仅为 `1000/7.300` 的组合路径换算值，不是布局布线后的可运行 Fmax；
由于有效裕量为 0 ns，不能据此宣称已经满足 150 MHz 或 200 MHz。

### 4.2 RTL Co-sim 事务

| 事务 | Latency | 相邻有效事务 Interval | 100 MHz 下时间 |
|---|---:|---:|---:|
| non-causal，`L=9` | **1,498 cycles** | 1,488 cycles | 14.98 us |
| causal，`L=9` | **1,142 cycles** | 1,132 cycles | 11.42 us |
| 非法长度 | 55 cycles | 不用于正常吞吐评价 | 0.55 us |
| 三事务总执行 | **2,675 cycles** | — | 26.75 us |

Co-sim 汇总的 latency 最小/平均/最大为 55/898/1,498 cycles；两个正常事务的 interval
为 1,132/1,488 cycles。顶层仍是一次启动处理一个完整 attention 的非流水化
`ap_ctrl_hs` 事务，因此局部循环 II=1 不等价于顶层每周期接收一个完整请求。

### 4.3 关键部件的 II 与 Latency

| 部件/层次 | Latency | II / Interval | 说明 |
|---|---:|---:|---|
| 手写 raw FMA 单元 | 5 | II=1 | PE/Accumulator 使用的位级浮点核心 |
| PE | 无独立层级值 | — | 已内联；以 SA 循环调度评价，不虚构独立 PE 报告 |
| CMP（4 个并行实例） | 3 | II=1 | 专用减法/符号比较路径，不调用 FMA |
| `spatialSystolicArrayTileTick` | **106** | **Interval=101** | 4×4 tile 级吞吐 |
| SA 主循环单次迭代 | 6 | II=1 | `PE_HOP_CYCLES=5` 后的反馈调度 |
| `accumulatorArithmeticVector` | 7 | II=1 | 4 个并行 lane |
| Accumulator arithmetic lane | 6 | II=1 | 每 lane 2 DSP |
| Q/K/V DMA 内层读取循环 | 动态边界 | **II=1** | 报告流水深度 75；trip count 随长度变化 |
| Input Delayer 内层循环 | 动态边界 | **II=1** | 由控制流和 tile 数决定总延迟 |
| Output Delayer 内层循环 | 动态边界 | **II=1** | 由控制流和 tile 数决定总延迟 |
| Scratchpad 关键循环 | 动态边界 | **II=1** | SRAM 访问局部循环已流水化 |

## 5. 当前资源和硬件并行度

| 资源 | 使用 | 器件可用 | 计算利用率 |
|---|---:|---:|---:|
| BRAM_18K | 20 | 4,032 | 0.50% |
| DSP | 32 | 9,024 | 0.35% |
| FF | 71,336 | 2,607,360 | 2.74% |
| LUT | 132,486 | 1,303,680 | 10.16% |
| URAM | 0 | 960 | 0.00% |

DATAFLOW 主体使用 12 BRAM、32 DSP、67,763 FF、128,168 LUT；其余 8 BRAM 主要来自
4 个 AXI master 适配器。DSP 可对账为：

```text
SA：16 套 PE MAC 等效乘法路径 × 1 DSP + 4 个 CMP × 2 DSP = 24 DSP
Accumulator：4 个 arithmetic lane × 2 DSP = 8 DSP
顶层总计：24 + 8 = 32 DSP
```

PE 在生成层级中已内联，所以这里表述为“16 套 PE MAC 等效乘法资源”，而不是声称存在
16 个命名 PE RTL 模块。当前设计保留 4×4 空间并行，没有通过串行复用来换取低 DSP。

## 6. 与手写 FMA 前 `fsa_stream` 的对比

两版使用同一 4×4 配置、100 MHz 目标、四 AXI master 和相同三事务测试口径；基线已经
完成 DMA II=1，因此表中主要反映 PE/Accumulator 手写 FMA、PE hop 缩短及其调度影响。

### 6.1 功能与周期

| 指标 | 手写 FMA 前 | 当前 build | 变化 |
|---|---:|---:|---:|
| CSim / RTL Co-sim | 通过 / 通过 | 通过 / 通过 | 功能状态保持 |
| non-causal latency | 2,569 | **1,498** | -1,071（-41.69%），**1.715×** |
| causal latency | 1,828 | **1,142** | -686（-37.53%），**1.601×** |
| 三事务总执行周期 | 4,432 | **2,675** | -1,757（-39.64%），**1.657×** |
| SA tile latency | 237 | **106** | -131（-55.27%） |
| SA tile interval | 222 | **101** | -121（-54.50%），tile 吞吐约 **2.198×** |
| SA 主循环 | 迭代 latency 16，II=1 | **迭代 latency 6，II=1** | II 保持 1，内部深度减少 10 拍 |
| CMP | 3 / II=1 | 3 / II=1 | 无退化 |
| Accumulator arithmetic vector | 10 / II=1 | **7 / II=1** | latency -3（-30%） |
| 顶层估算周期 | 7.300 ns | 7.300 ns | 均为 0 ns 有效裕量 |

原版 PE 是可见的标准 HLS 浮点路径，报告 latency/II 为 9/1；当前 PE 已内联，不能把
raw FMA 的 5/1 直接冒充完整 PE 的独立报告值。更可靠的端到端证据是 SA 主循环迭代深度
从 16 降至 6，tile interval 从 222 降至 101。

### 6.2 资源

| 资源 | 手写 FMA 前 | 当前 build | 变化 |
|---|---:|---:|---:|
| BRAM_18K | 20 | 20 | 0 |
| DSP | 108 | **32** | -76（-70.37%） |
| FF | 88,661 | **71,336** | -17,325（-19.54%） |
| LUT | 108,077 | **132,486** | +24,409（+22.58%） |
| URAM | 0 | 0 | 0 |

手写 FMA 将浮点规格化、舍入和前导零检测等逻辑更多地映射到 LUT/组合逻辑，因而显著
降低 DSP 和 FF，同时增加 LUT。总体结果不是单纯“减少所有资源”，而是以约 22.6% 的
LUT 增长换得约 70.4% 的 DSP 减少和明显的周期缩短。当前顶层时序仍卡在 7.300 ns，
说明延迟/吞吐改善没有转化为额外的顶层时序裕量。

## 7. 与 `fsa_dma` 的对比

`fsa_dma` 是单个共享 64-bit AXI master、单套共享 4×4 计算数据通路；当前 `fsa_stream`
使用四个独立 AXI master 和多进程 DATAFLOW。两者并非同一微架构，以下对比用于说明架构
取舍，不应被解释为只由某一个算术单元带来的收益。

### 7.1 功能、接口与周期

| 指标 | `fsa_dma` | 当前 `fsa_stream` | 结论 |
|---|---:|---:|---|
| RTL Co-sim 覆盖 | 1 个 non-causal `L=9` 事务 | non-causal、causal、非法长度 | 当前覆盖更完整 |
| non-causal latency | 27,566 cycles | **1,498 cycles** | -26,068（-94.57%），当前约 **18.402×** 快 |
| 顶层事务 interval | 单事务，NA | 1,132/1,488 cycles | 当前可观测两个正常事务间隔 |
| 外部数据主口 | 1 个共享 64-bit AXI master | **4 个独立 64-bit AXI master** | 当前带宽并行度更高 |
| 请求调度 | 循环 II=39 | 多进程 DATAFLOW，DMA/关键局部循环 II=1 | 当前消除了共享调度器瓶颈 |
| 局部 SA | stage latency 16，II=1 | 主循环迭代 latency 6，II=1 | 两者局部 II 均为 1，当前路径更浅 |
| 顶层估算周期 | 7.300 ns | 7.300 ns | 均为 0 ns 有效裕量 |

18.402× 是同为 `L=9、D=4、non-causal` 的 RTL Co-sim 周期比，具备测试尺度上的参考价值；
但 `fsa_dma` 只有一个该类事务，且两版的 AXI 端口数量、调度方式和计算单元实现均不同，
不能把该倍数视为任意长度下的稳定系统加速比。

### 7.2 资源

| 资源 | `fsa_dma` | 当前 `fsa_stream` | 变化 |
|---|---:|---:|---:|
| BRAM_18K | 4 | **20** | +16（+400%） |
| DSP | 303 | **32** | -271（-89.44%） |
| FF | 105,648 | **71,336** | -34,312（-32.48%） |
| LUT | 128,957 | **132,486** | +3,529（+2.74%） |
| URAM | 0 | 0 | 0 |

`fsa_dma` 的 303 DSP 来自唯一 300-DSP 数据通路和 3-DSP 执行计划；当前版本在保留 4×4
空间并行的同时仅使用 32 DSP。当前多出的 16 BRAM 主要对应四 AXI 适配器及流式 scratchpad
缓冲，LUT 则与 `fsa_dma` 基本处于同一量级，增加约 2.7%。因此当前架构的核心交换是：
增加 BRAM 和物理 AXI 端口数量，换取更低 DSP、较低 FF 和大幅缩短的小规模事务周期。

## 8. 当前警告与风险

当前 Solution 日志共有 **156 条 warning、0 条 error**。

| 告警代码 | 数量 | 影响判断 |
|---|---:|---|
| RTGEN 206-101 | 85 | 主要是 RTL 命名、初始化和复位提示 |
| ANALYSIS 214-52 | 32 | `pe_register` 依赖被显式判为 false；与 SA II=1 调度相关，需持续做功能回归 |
| SYN 201-103 | 18 | 模板/匿名函数名称合法化，主要影响 RTL 名称 |
| HLS 200-960 | 8 | 若干外层循环不能 flatten；不等于内层 II 失败 |
| BIND 205-102 | 4 | SRAM 指定资源在更简单实现可用时可能被忽略，应以最终存储映射为准 |
| HLS 200-1020 | 3 | 工具自动增加 start FIFO 深度以提高性能/避免死锁 |
| HLS 200-1449 | 3 | Q/K/V DMA reader 同时依赖前驱和调用者输入，可能限制 dataflow 吞吐 |
| HLS 200-1018 | 1 | 建议将 `control_to_spad` FIFO 深度提高到 3 |
| HLS 200-1995 | 1 | Compile/Link 后 384,252 条指令，提示设计规模较大 |
| HLS 214-358 | 1 | SA 数组索引存在位扩展逻辑，可能影响性能 |

当前日志没有 HLS 时序违例，Co-sim 也没有重现之前的 deadlock 或数值 mismatch；但
`HLS 200-1018`、`HLS 200-1449` 和 0 ns 时序裕量仍应视为后续长序列与 Vivado 实现阶段
的重点风险，而不是因为当前短测试通过就忽略。

## 9. 综合结论

当前 `fsa_stream` 已在当前验证范围内达到以下结果：

- CSim、CSynth 和三事务 RTL Co-sim 均通过；
- Q/K/V DMA、SA 主循环、CMP、Accumulator 和 Delayer 的关键局部循环均保持 II=1；
- 相对手写 FMA 前的同架构基线，non-causal/causal 延迟分别降低 41.69%/37.53%，
  SA tile interval 从 222 降至 101，DSP 从 108 降至 32；
- 相对 `fsa_dma` 的同尺度 non-causal 测试，周期从 27,566 降至 1,498，约 18.402×；
- 代价是相对手写 FMA 前增加 22.58% LUT，相对 `fsa_dma` 增加 16 BRAM 和 3,529 LUT，
  并使用四个独立 AXI master；
- HLS 时序仅在 7.300 ns 有效预算边界上通过，裕量为 0 ns；尚无 IP 导出、Vivado
  布局布线或板级证据，因此当前不能宣称已经满足 150/200 MHz 或可直接上板。

从当前 HLS 证据看，手写 FMA 对内部 latency、tile interval 和 DSP 使用均取得明显收益，
且没有造成已测试功能或 II 的退化；下一步最有价值的验证不是继续依据 HLS 估算推断频率，
而是导出 IP 后完成 Vivado 实现，检查真实 WNS、关键路径、AXI 基础设施和跨区域布线。

## 10. 主要结果文件

以下路径相对于 `build/fsa_stream_build/solution1`：

- 顶层综合：`syn/report/fsa_stream_csynth.rpt`
- 综合 XML：`syn/report/fsa_stream_csynth.xml`
- DATAFLOW 层级：`syn/report/fsaStreamingDataflow_csynth.rpt`
- SA tile：`syn/report/spatialSystolicArrayTileTick_csynth.rpt`
- Accumulator arithmetic vector：`syn/report/accumulatorArithmeticVector_csynth.rpt`
- C 仿真：`csim/report/fsa_stream_csim.log`
- RTL Co-sim 汇总：`sim/report/fsa_stream_cosim.rpt`
- Co-sim 事务：`sim/report/verilog/result.transaction.rpt`
- Solution 日志：`solution1.log`

历史 `fsa_dma` 证据保存在 `docs/综合报告/fsa_dma综合报告.md`。当前 Solution 没有
`impl/ip/component.xml`、IP 导出 zip、`.xo`、Vivado 实现或板级测试产物。
