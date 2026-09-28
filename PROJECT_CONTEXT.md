# FSA HLS Project Context

> 本文件只保留恢复任务所需的高信号状态。后续工作开始时先读取，并在关键结果或决策变化后原地更新。

## 1 目标与当前优先级

- 生产基线：在Vitis HLS中实现与Chisel FSA语义一致的完整attention核；单次顶层调用完成一次attention，核内DMA搬运Q/K/V/O。
- 当前研究任务：独立实现参数化`D×D` Split-D顶层`fsa_stream_split_d`。
- 默认配置：物理阵列`4×4`、`dim=16`；目标配置仅改参数得到`16×16/dim128`。
- 算法顺序：每个PE含FP16工作寄存器`reg`和FP32 `score_acc`；先执行`dim/D`轮QK累加，完整S后softmax，再执行`dim/D`轮PV。
- 当前第一优先级：2026-09-28 13:50的新build已确认PV outline和第2套PE消失，DSP收敛到40；但PV因`output_acc`假相关只能达到II9，且顶层估算周期7.932ns。当前候选已声明该确定安全的假相关、增加共享函数返回延迟，并把PV交错上下文增至8；下一步用新CSynth验证II和时序。

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

**最近一次有结果的build：**2026-09-28 13:50的扁平PV候选；CSim和CSynth通过，结构收敛，但吞吐和时序不验收；该轮按Tcl设置未运行CoSim。

- 顶层为DSP40、FF29387、LUT122491、BRAM8；旧PV outline消失，唯一16-DSP `runPeArray`、唯一8-DSP `runAccumulatorColumns`和16-DSP减法器的资源结构符合目标。
- QK和ROW_SUM实际II4，PWL实际II1；PV扁平循环目标II1但实际II9。
- PV的`HLS 200-880`明确指出`output_acc`在循环内读写形成携带相关：Accumulator调用及写回要求9拍后才能进入下一迭代。该相关对当前固定调度是假相关，因为一个key tile内每个feature只更新一次，且下一个key tile不会与本循环重叠。
- 顶层估算周期7.932ns，超过7.300ns有效预算；PV关键路径为Accumulator调用7.233ns加写回0.699ns，QK为PE调用7.054ns加反馈写回，ROW_SUM最差为7.893ns。

**当前源码修改：**

- 已删除`hls::task`、`hls_thread_local`、四条命令/结果stream和`run()`层DATAFLOW；Tcl恢复普通`cosim_design -rtl verilog`。
- `runPeArray`是唯一包含`peMacUnit`调用的位置，内部完全展开`D×D`；`runAccumulatorColumns`是唯一包含`accUnit`调用的位置，按列完全展开。两者改为STP II1，并分别设置固定最小/最大延迟4和7，意图在调用返回处增加一级寄存器隔离；实际时序效果必须由新CSynth确认。
- `runController`对上述两个非内联函数各设置`ALLOCATION function ... limit=1`；13:50 build已确认两个模块都只有一个物理实例。
- QK和ROW_SUM循环保留真实反馈并把目标II改为5，以接受共享PE新增的返回延迟，不使用虚假的PE反馈依赖声明。
- PV改为8个feature上下文交错的单一固定边界II1操作循环：每组先发射`D×8`个PE操作，再发射8个Accumulator更新；`output_acc`只在每个feature唯一一次的更新阶段访问，因此仅对该变量声明`inter false`。没有外层feature-group outline，也没有`PIPELINE off`。
- PWL从task批量请求/回收改为有限流水调用：8段扫描期间保持`PE.reg`中的X不变，命中结果暂存到此时已不再保存S的`PE.score_acc`，结束后写回`PE.reg`；没有阵列外P副本和额外PE状态，两种参数本地端到端回归通过。
- 11:03的FRP源码候选仍在相同14160ns死锁；本地没有同步该次`csynth.rpt`和`sim/verilog`，无法确认工具是否真正采用FRP，因此不能把它当作有效硬件修复。
- AMD Vitis HLS文档明确要求含dataflow task和M_AXI的CoSim启用`-enable_tasks_with_m_axi`；该开关在11:17复验中仍死锁，证明原问题是结构闭环。当前已无task，因此Tcl不再使用该开关。
- 11:17 deadlock report确认控制器阻塞于空`pe_result_stream`，KPN阻塞于空PE/Accumulator命令流，没有任何写端因FIFO满而阻塞。按AMD官方判据，这不是FIFO深度不足，而是设计结构问题。
- 当前`runController`同时是task命令生产者和结果消费者，跨`ap_ctrl_chain`控制区与`ap_ctrl_none` KPN形成闭环。AMD混合task/dataflow模型要求task输入由先于task的普通进程产生、task输出由后于task的普通进程消费；同一个控制器承担两端不满足该前向拓扑。
- QK和ROW_SUM直接调用共享PE模块，并显式接受II4真实反馈；不使用虚假的`DEPENDENCE false`。
- PWL一次连续发射8个分段再顺序收回，命中结果直接写回PE.reg，删除了阵列外`D×D` probability副本。
- PV每组交错8个独立feature：每个feature内部仍按key row原顺序累加，只使用`8×D`个FP32临时partial，不改变FMA顺序；16维和128维都能整除8，不增加目标配置的空上下文操作。
- 当前有限调用源码的`4×4/dim16`和`16×16/dim128`均通过端到端本地测试及带`__VITIS_HLS__`/`__HLS_CSIM__`宏的全部相关源文件C++14语法检查。
- 本地使用math stubs完成功能回归；真实Vitis调度、实例共享、时序和RTL行为仍需服务器CSynth/CoSim验证。

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
2. **先结构后性能。**13:50 build已确认单PE/单Accumulator和DSP40；当前只处理PV II9及7.932ns时序，不再改动已经收敛的实例结构。
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

1. 运行`./run_hls.sh fsa_stream_split_d`，先检查当前返回分级和8路PV交错候选的CSim、CSynth；Tcl暂时设为`RUN_COSIM 0`，避免在性能未验收前耗时跑RTL。
2. 新CSynth必须确认`runPeArray`和`runAccumulatorColumns`仍各只有一个物理实例、DSP保持约40、两模块II1、PV扁平循环达到II1、QK/ROW_SUM接受真实反馈II且有效周期不超过7.300ns。重点确认`HLS 200-880 output_acc`消失，以及新增LATENCY是否真正切断调用到写回的组合路径。
3. 若LATENCY没有改善时序，不继续堆pragma，改为显式输出寄存器/分级调度结构。CSynth通过资源、吞吐和时序验收后，再把`RUN_COSIM`改回1；RTL CoSim必须完成全部功能用例、无deadlock且C post-check通过。
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
- 2026-09-28 11:57/13:12有限调用build：CSim/CSynth完成，但PV外层生成单独outline并复制PE，结果DSP56、LUT183989、7.893ns；CoSim长时间0/6后中断，该版不验收。
- 当前扁平PV候选的`4×4/dim16`和`16×16/dim128`端到端本地回归均通过，两种参数的Vitis宏环境C++14语法检查均通过。
- 2026-09-28 13:50扁平PV build：CSim/CSynth通过，旧PV outline消失，PE/Accumulator收敛为各一套，DSP40；但PV实际II9且顶层7.932ns，性能和时序不合格。
- 当前返回分级/8路PV交错候选已完成`4×4/dim16`和`16×16/dim128`端到端本地回归，两种参数的Vitis宏环境C++14语法检查均通过；尚待服务器CSynth验证pragma语义和调度结果。

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

当前任务是把Split-D做成真正的一套参数化`D×D`阵列：默认4×4处理16维，未来16×16处理128维。13:50 build已经证明有限调用加扁平PV能保持单PE、单Accumulator和DSP40，但PV因`output_acc`假相关只有II9，顶层周期7.932ns。当前候选只对这个确定安全的变量相关声明`inter false`，把共享PE/Accumulator入口改为STP并尝试增加一级返回延迟，QK/ROW_SUM接受真实II5，PV扩为8个feature上下文以覆盖更长反馈延迟；不使用`PIPELINE off`，不屏蔽PE真实反馈。两种参数的本地端到端回归和Vitis宏语法检查已通过。下一步重跑Vitis，验收DSP40、PV II1和周期不超过7.300ns；若LATENCY没有切断组合路径，则改为显式输出寄存器结构，之后才恢复RTL CoSim。
