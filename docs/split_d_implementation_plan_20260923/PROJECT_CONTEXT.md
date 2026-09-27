# Project Context — Split-D 方案修订交接

## 1. Mission / User Requirements

按用户指定的 WEEK10/FSA_HLS 最新本地副本，修订20260921的split-d详细实施方案。寄存器放PE里，不拘泥固定拍数，实验需能对应TAPA-CS部分优化。所有本次生成物在本新目录，保留旧方案/源码。

## 2. Hard Constraints

- 本次交付是方案及模型，不是HLS实现。未修改仓库、旧方案或其PROJECT_CONTEXT；未做Git操作。
- 当前仓库主路径为stream，四bundle64位、Q cache、独立DMA请求、唯一Acc actor、RawFMA、compact SRAM均保留。
- 新variant允许增加PE FP32 score；一个PE只一套RawFMA，reg复用Q/S/N/X/P；算法状态与邻接寄存器在PE中。
- 原时钟10ns、uncertainty2.7ns、part不变；cosim/export默认关闭并遵循AGENTS的执行约束。
- maintaining-project-context按仓库内skills副本读取；AGENTS给的C:/Users/30130路径缺失。用户输出目录要求优先，故只在交付目录维护此上下文。

## 3. Current State

**Confirmed:** 当前源码审计已完成；00–04/06方案已重写；参考模型72配置/8故障注入通过。当前项目头文件缺ap_int.h，未附build。最新综合对比报告记录7.300ns/32DSP/132486LUT，晚于原项目上下文，但本次未独立核验原始报告。

**Assessment:** split-d最小可行路径是复用RawFMA的c32种子累加，并在同一KV生命周期跨d保持score；局部布线/流水化仍需综合与实现证明。E8可行性未知。

**Main issue:** 尚未进行实际新HLS实现和硬件验证。这不妨碍本次方案交付完成；未来实施从P0开始。

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

本目录00–06、reference_split_d.py、validate_delivery.py、两个验证JSON、source_manifest.json。本次文档任务已完成；后续代码实施尚未启动。

## 9. Next Actions

1. 实施者先读00/06，再按03 P0固定基线和补齐工具headers。
2. P1参数/布局，P2仅score链，P3softmax/PV，P4端到端stream，逐阶段验证。
3. E1综合后决定是否扩engine和做布局实验，不跳到E8。

## 10. Validation Status

- [x] 当前stream源码/旧方案/8FSA/论文相关章节审阅。
- [x] 数学/地址/token计数/故障注入自检。
- [x] UTF-8、Markdown相对链接、输入SHA-256与交付清单检查（见delivery_validation.json）。
- [ ] 仓库C++回归及新HLS实现测试。
- [ ] 新CSynth/RTL/Vivado/板测。

## 11. Environment

Windows PowerShell；Python版本见delivery_validation.json。源根为上级FSA_HLS；不使用Git标识，以SHA-256追踪。目标工具/器件沿用现有Vitis2024.2/VU37P，不表示本地可执行这些工具。

## 12. Context Handoff Summary

此次只修订并交付方案，文件全在20260923新目录。旧计划最重要的过时点已替换：主路径、Q缓存/流量、DSP、PE共享算术、banked SRAM、最新报告。后续按02实现，按03推进；已跑Python模型，没跑HLS。不要从旧20260921混入另一套寄存器/partial-add定义。
