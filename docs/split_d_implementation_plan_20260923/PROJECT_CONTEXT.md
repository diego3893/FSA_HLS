# Project Context — Split-D实施交接

## 1 任务边界

- 在现有`fsa_stream`之外实现独立`fsa_stream_split_d`，不改变生产顶层接口和行为。
- 默认物理阵列`4×4`处理`dim=16`；后续只改参数得到`16×16/dim128`。
- 每PE包含一个FP16工作`reg`和一个FP32 `score_acc`。
- 计算顺序固定为：`dim/D`轮QK累加→完整S后的softmax→`dim/D`轮PV。
- 综合目标是一套参数化`D×D` RawFMA阵列和一套每列Accumulator；不允许用复制阵列换II。

## 2 硬约束

- 保持Q/K/V/O四个64-bit AXI master、AXI-Lite控制、VU37P、10ns时钟和2.7ns uncertainty。
- P驻留PE的FP16 `reg`；禁止阵列外增加完整S/P副本。
- 保留现有RawFMA、PWL和舍入合同；算法、接口与硬件结构问题分开验证。
- CoSim和IP导出默认关闭；本地无Vitis，只运行C++回归。
- 不进行Git操作；同时维护根`PROJECT_CONTEXT.md`。

## 3 当前状态

### 最近build

`build/fsa_stream_split_d_build/solution1`，2026-09-28 00:55--00:58：

- CSim、CSynth通过；未运行RTL CoSim或IP导出。
- 顶层7.300ns；BRAM8、DSP88、FF55987、LUT308699、URAM0。
- `runAccumulatorColumns`已收敛为唯一实例：6拍、II1、8 DSP。
- PE仍为4套：共享`runPeArray`占16 DSP；PV的`block×lane`被自动流水，内部4次`row`完全展开，又生成3套16-DSP阵列。
- 另有8个FP32减法器占16 DSP，顶层合计88 DSP。
- QK和ROW_SUM为II4；PV的II1依赖阵列复制，结构不验收。
- 本轮产物时间戳是新的，但综合预处理源码仍没有PV三级循环的3条`#pragma HLS PIPELINE off`；结果和2026-09-25 build完全相同，说明服务器实际重跑了旧源码，不能用来验收当前候选。

### 当前源码

- PV的`block`、`lane`、`row`三级循环已添加`PIPELINE off`，目标是让四次行累加顺序复用唯一PE阵列。
- `runPeArray`自身II1、Accumulator唯一实例限制、算法、接口和时钟均未改变。
- 本地`4×4/dim16`及`16×16/dim128`端到端回归通过。
- **待验证：**当前候选仍无对应Vitis结果，不能声称PE实例数、资源、II或时序已经收敛。

## 4 核心映射

```text
Q/K/V tile
  -> 唯一D×D PE阵列
       QK: score_acc跨dim/D轮保留
       softmax: S转FP16并把P写回reg
       PV: reg中的P与V执行dim/D轮
  -> 唯一D列Accumulator
  -> normalize/write O
```

- QK/PV必须调用同一`runPeArray`硬件实例。
- 默认4×4阵列应只有16个11×11 PE乘法器。
- 默认4列Accumulator应只有4个24×24乘法器，共8 DSP。
- causal按全局query/key索引判断。

## 5 关键经验

- 函数定义唯一、源码`UNROLL`或ALLOCATION pragma都不能单独证明硬件唯一；必须检查综合层次、RTL实例、运算符和DSP核算。
- 首轮build：4套PE+2套Accumulator，DSP96。
- 第二轮build：Accumulator收敛为1套，但PV自动流水仍复制PE，DSP88。
- 不调用旧tile函数`dim/D`次，否则会丢失跨d状态或提前softmax。
- 不把带seed的score再次加旧score；不提前用Q覆盖P；不每个V tile更新完整O。
- 不恢复全局虚假dependence覆盖、Accumulator反馈DATAFLOW或多输出DMA请求actor。

## 6 当前工作集

- 配置与类型：`include/fsa/stream/split_d/`
- 实现：`src/stream/split_d/fsa_stream_split_d.cpp`
- 测试：`tests/stream/test_fsa_stream_split_d.cpp`
- HLS入口：`hls/fsa_stream_split_d/run_hls.tcl`
- 根入口：`run_hls.sh`
- 当前build：`build/fsa_stream_split_d_build/solution1/`

## 7 下一步与验收

1. 先确认服务器实现文件的PV三级循环包含3条`#pragma HLS PIPELINE off`，再运行`./run_hls.sh fsa_stream_split_d`。
2. 新build先检查综合预处理源码确实带入3条pragma，再确认PV模块内不再有3套额外`runPeArray`，全设计只有一套`D×D` PE阵列。
3. 确认Accumulator仍为唯一8-DSP实例、每PE `score_acc`存在、四AXI接口保持。
4. 读取CSim、总资源、关键循环II和7.300ns有效时序预算。
5. 单阵列结构通过后再处理QK/ROW_SUM的II4；结构通过前不扩engine、不做布局实验。

## 8 验证状态

- [x] 数学、地址、causal、token计数和故障注入参考模型。
- [x] 默认`4×4/dim16`本地端到端测试。
- [x] 仅改参数的`16×16/dim128`本地端到端测试。
- [x] 新增Split-D后的生产`fsa_stream`回归。
- [x] 首轮CSim/CSynth：功能通过，结构失败。
- [x] 第二轮CSim/CSynth：Accumulator唯一化成功，PE仍复制。
- [x] 关闭PV自动流水后的两种参数本地回归。
- [x] 2026-09-28 build审计：产物虽新，但输入仍是旧源码，结构仍为4套PE、DSP88。
- [ ] 当前候选Vitis CSim/CSynth和单阵列验收。
- [ ] RTL CoSim、IP导出、Vivado实现、板测。

## 9 交接摘要

Split-D功能路径已经完成，当前只解决物理实例唯一性。2026-09-28 00:58 build虽为新产物，但综合输入仍是没有3条`PIPELINE off`的旧源码，故仍生成4套PE阵列、DSP88；它不能验收当前候选。本地源码已关闭PV三级循环自动PIPELINE且两种目标参数通过。下一步先正确同步源码并确认pragma进入综合，再重跑Vitis证明只剩一套PE阵列，之后才优化II4。
