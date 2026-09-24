# Project Context — Split-D 方案修订交接

## 1. Mission / User Requirements

在现有`fsa_stream`之外实现独立`D×D`Split-D研究顶层。用户最终确认：首版物理阵列`4×4`、逻辑`dim=16`；每PE新增一个FP32 score寄存器，执行`dim/D`轮S累加，完整S后做softmax，再执行`dim/D`轮PV。后续只修改参数得到`16×16/dim128`。

## 2. Hard Constraints

- 方案阶段已结束，当前进入独立HLS实现；现有`fsa_stream`作为回归基线，不修改其接口和行为。
- 当前仓库主路径为stream，四bundle64位、Q cache、独立DMA请求、唯一Acc actor、RawFMA、compact SRAM均保留。
- 新variant每个PE包含一个FP16工作`reg`和一个FP32`score_acc`；目标综合结构为一套参数化`D×D`RawFMA阵列，不允许不同阶段复制阵列。
- 原时钟10ns、uncertainty2.7ns、part不变；cosim/export默认关闭并遵循AGENTS的执行约束。
- 按仓库AGENTS同时维护根`PROJECT_CONTEXT.md`和本实施上下文；不进行Git操作。

## 3. Current State

**Confirmed:** 当前环境已有Vitis headers和历史build，但本地没有Vitis/Vivado。新增独立`fsa_stream_split_d`、参数/PE状态、端到端testbench和HLS Tcl。默认`4×4/dim16`已通过目标`L=16`及边界配置，只改参数后的`16×16/dim128`也通过本地端到端C++测试；旧`fsa_stream`4×4回归继续通过。

**Assessment:** 当前功能代码通过单一`runPeArray`复用PE算术，并在每个`PeState`中跨全部特征块保存`score_acc`。是否真正综合为一套`D×D`RawFMA、score是否为局部寄存器，仍需CSynth层次和资源证明。

**Main issue:** 本地功能实现已完成首轮，但未运行Vitis CSim/CSynth。不得据本地通过声称PE实例数、资源、II、时序或物理局部性已验收。

**Historical artifacts:** `source_manifest.json`和`delivery_validation.json`保留方案交付时的历史快照，不随实施静默刷新；当前源码和本上下文已变化，旧hash不再表示当前实现状态。

## 4. Architecture / Important Files

02为规范性架构，03为逐阶段任务，04为实验设计，06列出被替换的旧结论，01给源码依据，05给本次验证范围。

核心：D独立于R；ND个d块递减走，seed旧score进底部FMA，顶部结果赋回key-owner PE；全部d完成才CMP/softmax；P保持跨NV；每KV D+2 token；Acc拥有D+1行。BQ与BK独立，causal按全局索引。Q/K/V仍完整D缓存但用R宽packed行。

## 5. Decisions

### D001 — 以当前stream为基线（Active）

理由/证据：当前调用链已经具备四bundle、Q跨KV缓存、共享RawFMA。旧legacy审计不能继续作为新增优化依据。含义：515MiB是示例当前基线，770MiB为历史。

### D002 — Seeded QK与单elem reg（Active，待硬件验证）

理由/证据：peMacUnit已有FP32 c输入，可直接延续部分score；Q与P时间上互斥。含义：无需默认新增每PE float加法器或独立P寄存器，需新增带tag的本地seed/return链并等待完成。

### D003 — 保留当前精度合同（Active）

理由：改变score/SCALE/P精度会污染资源和精度对照。含义：数学模型与位精确RawFMA/PWL oracle必须分开；FTZ和HLS half cast不可混同。

### D004 — 优先物理消融（Active）

理由：split-d本身只是时间/空间交换。含义：B0/B1/B2/B3固定同一计算配置，分别验证布局与跨区pipeline；HBM共享另做消融。

## 6. Experiments / Results

运行 `python reference_split_d.py`：72标准配置，8种故障全部检出，mask/history、alpha、打包、pending-mask广播抽象、片上布局和causal流量检查通过。详见reference_results.json。不是HLS/FP32/PWL/周期模型。

交付结构/hash验证见delivery_validation.json，复现脚本validate_delivery.py不会静默覆盖已有输入hash快照。

## 7. Things Not To Repeat / Open Problems

- 不调用旧tile函数ND次，避免每d丢状态/提前softmax。
- 不把带seed结果再次加oldscore；不每VT更新整个O；不提前装Q覆盖P。
- 不把pe_register旧false dependence直接移植；不恢复Acc请求/响应DATAFLOW环。
- 不用旧17.5DSP/PE外推，或只看DSP宣称E8能装下。
- 仍待验证：PE源代码所有权是否落实物理局部性，seed/return链代价、PWL精度、有限FIFO死锁、LUT扩展、真实HBM绑定。

## 8. Current Working Set

实施文件位于`include/fsa/stream/split_d/`、`src/stream/split_d/fsa_stream_split_d.cpp`、`tests/stream/test_fsa_stream_split_d.cpp`和`hls/fsa_stream_split_d/run_hls.tcl`。本目录继续保存规范、模型和交接上下文。

## 9. Next Actions

1. 在Vitis运行新顶层的CSim/CSynth，保持10ns、2.7ns uncertainty和当前VU37P。
2. 核对`D×D`RawFMA实例数、每PE FP32 score寄存器、没有阵列外S/P RAM、四AXI接口、资源、II和时序。
3. 若工具复制`runPeArray`或生成额外浮点阵列，先修正共享结构；E1验收前不扩engine和不做布局实验。

## 10. Validation Status

- [x] 当前stream源码/旧方案/8FSA/论文相关章节审阅。
- [x] 数学/地址/token计数/故障注入自检。
- [x] UTF-8、Markdown相对链接、输入SHA-256与交付清单检查（见delivery_validation.json）。
- [x] 仓库旧顶层4×4回归及新Split-D本地C++端到端测试。
- [x] `4×4/dim16`与参数化`16×16/dim128`功能测试。
- [ ] 新顶层Vitis CSim/CSynth和结构验收。
- [ ] 新CSynth/RTL/Vivado/板测。

## 11. Environment

Windows PowerShell；Python版本见delivery_validation.json。源根为上级FSA_HLS；不使用Git标识，以SHA-256追踪。目标工具/器件沿用现有Vitis2024.2/VU37P，不表示本地可执行这些工具。

## 12. Context Handoff Summary

用户最终将研究结构收敛为参数化`D×D`阵列：默认`D=4、dim=16`，目标`D=16、dim=128`。本地功能代码和两种参数测试已完成，旧顶层回归未受影响。下一步不是继续扩功能，而是用Vitis证明只有一套`D×D`PE阵列、每PE一个FP32 score寄存器，并读取资源、II和时序。
