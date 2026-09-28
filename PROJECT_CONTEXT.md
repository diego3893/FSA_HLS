# FSA HLS Project Context

> 本文件只保留恢复任务所需的高信号状态。后续工作开始时先读取，并在关键结果或决策变化后原地更新。

## 1 目标与当前优先级

- 生产基线：在Vitis HLS中实现与Chisel FSA语义一致的完整attention核；单次顶层调用完成一次attention，核内DMA搬运Q/K/V/O。
- 当前研究任务：独立实现参数化`D×D` Split-D顶层`fsa_stream_split_d`。
- 默认配置：物理阵列`4×4`、`dim=16`；目标配置仅改参数得到`16×16/dim128`。
- 算法顺序：每个PE含FP16工作寄存器`reg`和FP32 `score_acc`；先执行`dim/D`轮QK累加，完整S后softmax，再执行`dim/D`轮PV。
- 当前第一优先级：已删除混合task反馈环并改为有限调用共享模块；首轮Vitis因ALLOCATION目标缺少`detail::`命名空间而在分析阶段失败，现已按诊断修正，下一步重跑4×4/16全流程。

## 2 硬约束

- 不修改`FSA-main`；现有生产顶层`fsa_stream`的接口和行为保持不变。
- Split-D使用独立顶层`fsa_stream_split_d`，保持Q/K/V/O四个64-bit AXI master bundle、AXI-Lite控制、VU37P器件、10ns时钟和2.7ns uncertainty。
- 只允许一套参数化`D×D` PE RawFMA阵列；QK、softmax相关步骤和PV顺序复用。
- Split-D每个PE只保留一个FP16 `reg`和一个FP32 `score_acc`；禁止阵列外增加完整S/P副本。
- 每列只允许一个Accumulator RawFMA通路；不得用复制PE阵列或Accumulator换吞吐。
- 保持现有RawFMA、PWL精度、特殊值及舍入合同；不得用放宽误差掩盖问题。
- 不恢复已造成RTL错误的`pe_pipeline/cmp_pipeline inter false`；不恢复Accumulator反馈DATAFLOW环或单actor写多个有限DMA请求FIFO。
- 不更改时钟、器件、接口或阵列参数来掩盖综合失败。
- 不进行Git操作。Vitis由用户在服务器运行；本地只做源码检查和C++回归。
- 没有读取对应新build前，不得声称实例数、DSP、II、时序、CoSim或死锁问题已经解决。
- 只维护仓库根目录的`PROJECT_CONTEXT.md`；`docs/`目录中的同名文件为历史快照，停止更新。

## 3 当前状态

### 3.1 Split-D当前候选

**最近一次有结果的build：**双task候选；11:17启用task+M_AXI CoSim开关后仍在13.76ms死锁。当前有限调用候选尚无Vitis build。

- CSim通过且报告`PE=4x4 HEAD_DIM=16 DIM_BLOCKS=4`；CSynth通过；10:42 Verilog RTL CoSim在14160ns触发deadlock detector并失败，随后C post-check报告`Bad TV file`；未运行IP导出、Vivado实现或板测。
- 测试覆盖`L=7` causal/non-causal、`L=1`、`L=16` causal/non-causal及非法长度哨兵。
- 顶层估算周期7.300ns，等于7.300ns有效预算；估算Fmax为136.99MHz，HLS裕量仍为0。
- 顶层资源：BRAM8、DSP40、FF45146、LUT118716、URAM0；达到`PE16+Accumulator8+减法器16`目标。
- PE结构已收敛：唯一`peArrayTask_U0`，16 DSP，函数流水深度6、II1。
- Accumulator结构已收敛：唯一`accumulatorTask_U0`，8 DSP，函数流水深度9、II1；active hierarchy中不再出现第二套Accumulator。
- QK和ROW_SUM命令循环目标II4、实际II2；PWL发射/接收、PV发射/接收及PV输出更新均达到II1。
- CSim观测到所有stream的最大占用深度为7，当前FIFO深度8可容纳该测试负载，但连续事务和RTL反压仍需CoSim验证。
- 日志中的`HLS 200-656`已被RTL CoSim证实：双FLP task在命令/结果环路中发生死锁，因此10:25 build整体不合格；`Bad TV file`是死锁后没有完整输出的后续错误，不是独立数值根因。

**当前源码修改：**

- 已删除`hls::task`、`hls_thread_local`、四条命令/结果stream和`run()`层DATAFLOW；Tcl恢复普通`cosim_design -rtl verilog`。
- `runPeArray`是唯一包含`peMacUnit`调用的位置，内部完全展开`D×D`并保持FLP II1；`runAccumulatorColumns`是唯一包含`accUnit`调用的位置，按列完全展开并保持FLP II1。
- `runController`对上述两个非内联函数各设置`ALLOCATION function ... limit=1`，使QK、softmax、ROW_SUM、PV、running-sum和normalize顺序复用同一硬件模块；必须由新CSynth确认约束没有被自动outline绕过。
- QK和ROW_SUM循环保留真实反馈目标II4；PV仍按4个feature上下文交错且循环目标II1；没有`PIPELINE off`。
- PWL从task批量请求/回收改为有限流水调用：8段扫描期间保持`PE.reg`中的X不变，命中结果暂存到此时已不再保存S的`PE.score_acc`，结束后写回`PE.reg`；没有阵列外P副本和额外PE状态，两种参数本地端到端回归通过。
- 11:03的FRP源码候选仍在相同14160ns死锁；本地没有同步该次`csynth.rpt`和`sim/verilog`，无法确认工具是否真正采用FRP，因此不能把它当作有效硬件修复。
- AMD Vitis HLS文档明确要求含dataflow task和M_AXI的CoSim启用`-enable_tasks_with_m_axi`；该开关在11:17复验中仍死锁，证明原问题是结构闭环。当前已无task，因此Tcl不再使用该开关。
- 11:17 deadlock report确认控制器阻塞于空`pe_result_stream`，KPN阻塞于空PE/Accumulator命令流，没有任何写端因FIFO满而阻塞。按AMD官方判据，这不是FIFO深度不足，而是设计结构问题。
- 当前`runController`同时是task命令生产者和结果消费者，跨`ap_ctrl_chain`控制区与`ap_ctrl_none` KPN形成闭环。AMD混合task/dataflow模型要求task输入由先于task的普通进程产生、task输出由后于task的普通进程消费；同一个控制器承担两端不满足该前向拓扑。
- QK和ROW_SUM直接调用共享PE模块，并显式接受II4真实反馈；不使用虚假的`DEPENDENCE false`。
- PWL一次连续发射8个分段再顺序收回，命中结果直接写回PE.reg，删除了阵列外`D×D` probability副本。
- PV每组交错4个独立feature：每个feature内部仍按key row原顺序累加，只增加`4×D`个FP32临时partial，不改变FMA顺序；命令和结果循环目标II1。
- 当前有限调用源码的`4×4/dim16`和`16×16/dim128`均通过端到端本地测试及带`__VITIS_HLS__`/`__HLS_CSIM__`宏的全部相关源文件C++14语法检查。
- 当前Windows仍缺少Xilinx浮点仿真链接库，普通本地可执行文件无法链接；双task的RTL行为和连续事务安全性尚需Vitis RTL CoSim验证。

### 3.2 已验收生产基线

生产顶层`fsa_stream`最新完整基线为2026-09-21 01:16的hop5+22位CLZ build：

- CSim、CSynth、Verilog RTL CoSim及C post-check通过，无死锁和数值错误。
- 9x4 non-causal/causal为1498/1142 cycles；SA主循环II1，Tile latency/interval为106/101。
- 顶层资源BRAM20、DSP32、FF71336、LUT132486；单16 PE、4 CMP、单4-lane Accumulator保持。
- SA估算7.120ns，顶层7.300ns；Q/K/V DMA关键读循环II1，Accumulator vector 7拍/II1/DSP8。
- 尚无IP导出、Vivado实现后时序或板级证明。

## 4 架构模型

### 4.1 Split-D

```text
Q/K/V AXI
  -> tile缓存
  -> 一套D×D PE阵列
       QK: dim/D轮累加到每PE的score_acc
       完整S -> max/scale/PWL softmax，P写回PE.reg
       PV: dim/D轮顺序复用同一阵列
  -> 每列唯一Accumulator
  -> normalize
  -> O AXI
```

- QK/PV使用同一`runPeArray`；`runPeArray`内部完全展开`D×D`坐标，默认4×4为16个混合精度RawFMA。
- `score_acc`只属于PE本地状态；P驻留`PE.reg`。
- Accumulator按列展开，默认4列共4个FP32 RawFMA，每个使用2 DSP。
- causal判断使用全局query/key索引。

### 4.2 生产基线

- 单`R×C` SA、C个CMP、单C-lane Accumulator；QK与PV顺序复用。
- 四个专用AXI bundle、64-bit存储字、三路独立DMA请求actor、唯一Scratchpad owner。
- PE槽为`cycle%5`，CMP槽为`cycle%4`；只保留已验证安全的PE寄存器调度表达。

## 5 关键决策

1. **单阵列优先。**任何II改善必须在一套PE阵列和一套Accumulator条件下成立；资源复制获得的II1不验收。
2. **先结构后性能。**当前先确认PV不再复制PE；通过后才处理QK和ROW_SUM的II4。
3. **保留精度合同。**PE使用FP16×FP16+FP32 RawFMA；Accumulator使用FP32×FP32+FP32 RawFMA；softmax/PWL与现有位精确参考一致。
4. **以生成物为证据。**函数定义唯一、源码`UNROLL`、ALLOCATION pragma或总DSP任一单项都不足以证明单阵列；必须交叉检查层次、RTL实例、运算符和循环调度。
5. **本地测试与HLS验收分开。**C++通过只证明功能，不证明综合结构、II、时序或RTL正确性。
6. **共享必须在同一综合层级内显式成立。**AMD文档说明函数共享要求调用位于同一层级；当前自动outline把QK/ROW_SUM/PV拆到不同层级，顶层`ALLOCATION`不能稳定跨层合并。
7. **不再把关闭流水当作最终结构。**推荐把所有PE操作收敛到一个唯一调用点或持久PE执行actor，PE本体保持II1；ROW_SUM/PV的真实反馈依赖通过多上下文交错调度隐藏，而不是复制阵列或关闭整个循环流水。

## 6 已排除方案

| 方案 | 失败结果 | 约束 |
|---|---|---|
| Accumulator请求/响应反馈DATAFLOW | RTL CoSim死锁/Bad TV | 不恢复旧反馈环 |
| 未启用task+M_AXI CoSim支持 | 加开关后11:17仍死锁，证明它只修正CoSim接口支持，不修复控制闭环 | 保留官方开关，但不再视为架构修复 |
| 仅切换FRP流水风格 | 11:03仍在相同14160ns死锁；且新报告未同步，无法确认FRP是否被采用 | 不作为独立解决方案，已恢复FLP |
| 同一普通控制器同步请求/响应`hls::task` | 11:17报告确认`runController -> peArrayTask -> runController`闭环，只有读空、没有写满 | 删除task反馈环；不用加FIFO或关闭deadlock detector掩盖 |
| 单DMA actor顺序写Q/K/V有限FIFO | 跨通道head-of-line死锁 | 固定为三路独立请求actor |
| 全局屏蔽PE/CMP ring依赖 | C++通过但RTL稳定产生48个错误 | 禁止恢复`pe_pipeline/cmp_pipeline inter false` |
| Split-D只靠函数唯一或ALLOCATION | 首轮4套PE+2套Acc；第二轮仍4套PE | 必须控制调用循环的自动流水/展开并查RTL |
| PV外层自动II1 | 完全展开4次row，复制为4套PE | 显式关闭PV三级循环自动PIPELINE，01:37 build已证明PV不再复制 |
| ROW_SUM自动II4 | 单独生成第2套16-DSP PE阵列 | 关闭ROW_SUM自动PIPELINE后已消除该本地实例 |
| `run()`层级单实例限制 | 自动outline后约束未覆盖PV子模块，仍有2套PE | 已停止该pragma路线，改为唯一静态PE task |
| 生产基线hop4 | II1但11.003ns时序失败 | 当前保留已验收hop5+CLZ |

## 7 当前工作集

- Split-D配置与类型：`include/fsa/stream/split_d/`
- Split-D实现：`src/stream/split_d/fsa_stream_split_d.cpp`
- Split-D测试：`tests/stream/test_fsa_stream_split_d.cpp`
- Split-D HLS入口：`hls/fsa_stream_split_d/run_hls.tcl`
- 根运行入口：`run_hls.sh`
- Split-D历史交接：`docs/split_d_implementation_plan_20260923/PROJECT_CONTEXT.md`（停止维护，不作为当前状态来源）
- 当前build：`build/fsa_stream_split_d_build/solution1/`

默认参数为`FSA_SPLIT_D_PE_DIM=4`、`FSA_SPLIT_D_HEAD_DIM=16`；目标参数为`16/128`。

## 8 下一步

1. 运行`./run_hls.sh fsa_stream_split_d`，检查有限调用候选的CSim、CSynth和RTL CoSim。
2. 新CSynth必须确认`runPeArray`和`runAccumulatorColumns`各只有一个RTL实例、DSP约40、两模块II1、PV调度II1且有效周期不超过7.300ns；若ALLOCATION被自动outline绕过，必须根据新层次重构，不能接受资源复制。
3. 新RTL CoSim必须完成全部功能用例、无deadlock且C post-check通过。
4. 4×4/16通过后，再用环境参数运行16×16/128，检查256个PE、16列Accumulator、资源及时序按参数扩展。

## 9 验证状态

### 已完成

- Split-D本地`4×4/dim16`端到端测试。
- 同一源码仅改参数后的`16×16/dim128`端到端测试。
- 新增Split-D后的旧`fsa_stream`回归。
- Split-D首轮Vitis：功能通过，4套PE、2套Accumulator、DSP96，结构失败。
- Split-D第二轮Vitis：功能通过，Accumulator收敛为1套，但PV仍复制PE，DSP88，结构失败。
- 当前关闭PV自动流水后的两种参数本地回归。
- 2026-09-28 00:58 build：CSim/CSynth通过，但综合输入仍是未含PV `PIPELINE off`的旧源码；结果仍为4套PE、DSP88。
- 2026-09-28 01:37 build：确认PV pragma生效，PV不再复制PE；CSim/CSynth通过，DSP由88降至56，但ROW_SUM自动流水仍生成第2套PE阵列。
- 新增ROW_SUM `PIPELINE off`后的`4×4/dim16`及`16×16/dim128`完整语法编译通过。
- 2026-09-28 02:16 build：ROW_SUM已改为外部共享PE，但PV outline改为拥有本地PE；总数仍为2套、DSP56，结构仍不合格。
- 曾完成key-tile局部`ALLOCATION`候选的两种参数语法检查；随后因架构上仍依赖`PIPELINE off`而放弃。
- 单`hls::task` PE执行器候选完成；无`PIPELINE off`，源码仅一个`peMacUnit`调用点，两种参数及Vitis CSim宏环境语法检查通过。
- task候选首次Vitis综合已进入csynth并确认4×4 PE展开，随后因顶层缺少显式DATAFLOW区域触发`HLS 214-389`；源码已补充该pragma，本地重新完成两种参数和Vitis CSim宏环境语法检查。
- 2026-09-28 08:03 build：CSim/CSynth通过，唯一PE task为16 DSP且II1，QK/ROW_SUM实际II2，PWL/PV实际II1；但PV outline复制出第二套Accumulator，总DSP48，结构仍不合格；另有`HLS 200-656`潜在RTL死锁告警。
- 当前双task候选已把Accumulator也改为唯一常驻actor；无`PIPELINE off`和ALLOCATION pragma，源码只有一个`peMacUnit`及一个`accUnit`调用点，两种参数和Vitis CSim宏环境语法检查通过。
- 2026-09-28 10:25 build：CSim/CSynth通过；唯一PE task为16 DSP/II1，唯一Accumulator task为8 DSP/II1，减法器16 DSP，总DSP40；QK/ROW_SUM II2，PWL/PV II1，周期7.300ns。结构和HLS吞吐目标通过，尚需RTL CoSim排除双task潜在死锁。
- 2026-09-28 10:42 CoSim：在14160ns由`AESL_deadlock_report_unit.v`终止，报`HLS 200-742` deadlock，C post-check因输出不完整报两次`Bad TV file`；双FLP候选失败。
- 2026-09-28 11:03 CoSim：FRP源码候选仍在相同14160ns死锁并报相同Bad TV；本地未同步该次综合/仿真生成物，无法确认FRP是否实际生效。
- 2026-09-28 11:17 CoSim：启用`-enable_tasks_with_m_axi`后仍在13.76ms检测到死锁；控制器等空`pe_result_stream`，KPN等空PE/Accumulator命令流，无写满阻塞。已确认是混合控制区闭环，不是FIFO容量问题。
- 当前有限调用候选已删除双task闭环；4×4/16与16×16/128本地端到端测试通过，两种参数的普通及Vitis宏语法检查通过，尚无Vitis CSynth/CoSim结果。
- 当前有限调用候选首轮Vitis源码分析失败：ALLOCATION pragma中的`runPeArray`和`runAccumulatorColumns`未限定命名空间；已改为`detail::runPeArray`和`detail::runAccumulatorColumns`。普通g++会忽略pragma语义，因此此前本地语法检查不能覆盖此类HLS专用诊断。

### 未完成

- Split-D RTL CoSim、IP导出、Vivado实现和板测。
- Split-D `16×16/dim128`参数下的Vitis CSim/CSynth与资源、时序验收。
- 生产基线IP导出、Vivado实现后时序和板测。

## 10 环境与复现

- 本地：Windows PowerShell；无Vitis/Vivado，只做C++检查。
- 服务器：Vitis HLS 2024.2，器件`xcvu37p_CIV-fsvh2892-2-e`。
- 时钟：10.0ns，uncertainty 2.7ns；不得放宽。
- Split-D服务器命令：`./run_hls.sh fsa_stream_split_d`。
- 生产基线命令：`./run_hls.sh fsa_stream`。
- 不使用Git标识当前状态；旧`source_manifest.json`和`delivery_validation.json`是历史快照，不代表当前源码。

## 11 交接摘要

当前任务是把Split-D做成真正的一套参数化`D×D`阵列：默认4×4处理16维，未来16×16处理128维。双task版本虽在10:25 CSynth达到DSP40和目标II，但11:17已确认RTL控制闭环死锁。当前源码已删除全部task/FIFO反馈，改为两个非内联II1共享模块：唯一`peMacUnit`调用位置组成D×D PE阵列，唯一`accUnit`调用位置组成D列Accumulator，并在`runController`限制各一实例；QK/ROW_SUM保留真实反馈间隔，PV保留4上下文II1交错，无`PIPELINE off`。4×4/16和16×16/128本地端到端测试已通过。下一步运行4×4/16 Vitis全流程，重点验证ALLOCATION在自动outline后仍保持单实例、DSP约40、PV II1、7.300ns和RTL CoSim通过，再验证16×16/128。
