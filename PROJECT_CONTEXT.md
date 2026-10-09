# FSA HLS Project Context

> 本文件只保留恢复任务所需的高信号状态。后续工作开始时先读取，并在关键结果或决策变化后原地更新。

## 1 目标与当前优先级

- 生产基线：在Vitis HLS中实现与Chisel FSA语义一致的完整attention核；单次顶层调用完成一次attention，核内DMA搬运Q/K/V/O。
- 当前研究任务：独立实现参数化`D×D` Split-D顶层`fsa_stream_split_d`。
- 默认配置：物理阵列`4×4`、`dim=16`；目标配置仅改参数得到`16×16/dim128`。
- 算法顺序：每个PE含FP16工作寄存器`reg`和FP32 `score_acc`；先执行`dim/D`轮QK累加，完整S后softmax，再执行`dim/D`轮PV。
- **当前阶段1/2已完成（2026-10-10）**：被测`25d6dda`官方CSim/CSynth/CoSim通过，数学24/24、严格位模式20/20、26/26事务，7.300ns、DSP40/FF31923/LUT125123/BRAM8、II5/5/1/1保持基线。验收见`docs/综合报告/Split-D阶段1与2验收_20261010.md`，阶段1/2一轮修正后收敛；后续阶段3另有新授权。
- **16×16/128暂时搁置**（用户2026-10-09既有指示）。历史P1的CoSim通过当时7事务，但有效长度最大5，不能外推为满tile/多tile全面通过；顶层7.934ns、阶段II8/8/5/5未达标。当前P3a形状的16×16尚未验证，不能假定也有latency3。
- 4×4改为整阵列流水体和常量写回索引后，HLS估算周期由7.893ns改善至7.300ns；尚缺完整关键路径对照，选择逻辑为当前解释，不推广成16×16唯一根因。
- **2026-10-09当前执行方案：**`docs/Split-D修正与TAPA启发的分阶段修改验收方案_20261009.md`。
- **阶段2实例与存储已核对**：新RTL唯一PE阵列/唯一Accumulator，16条PE及4条Acc RawFMA；DSP16＋8＋8个fsub×2=40。算法无额外可回读S/P备份，但HLS有多组阶段寄存器，容量及用途已列入新验收报告；不能宣称物理状态只存在一组。保持共享层级。
- **上轮失败已修复**：旧`46cbbd0`因强制7/8精确可达而CSim失败；新测试使用独立RNE量化和相邻输入不可达证明。两种causal共46条PWL探针＋2条不可达证明，在官方CSim和CoSim均通过。失败记录保留为历史。
- **基线及新证据已归档**：旧`9c49789`20项未改输入/O/周期在新官方流程逐项一致；新24个有效事务全部QKV/O、非法canary和26事务周期存于`docs/evidence/split_d_25d6dda_4x4/`。完整C/RTL O逐字节一致；阶段3已将全部24项期望内嵌，新的分块硬件尚待官方验收。
- **当前阶段3（2026-10-10用户“继续3”授权）**：两个4×2空间query块候选已本地实现，数学24/24、严格19/24（已知5项宿主桩库差异，exit1）；待Git同步和4×4/head16官方CSim→CSynth→CoSim及实例/并发取证。未设迭代上限；阶段4/5、16×16、IP、Vivado、板测不在范围。死代码仅修正说明，未清理。

## 2 硬约束

- 不修改`FSA-main`；现有生产顶层`fsa_stream`的接口和行为保持不变。
- **Git规则已修订（2026-10-09）**：AGENTS.md现为“默认不进行git操作；用户明确授权后，在撤回前默认进行git操作”。先前记录的用户Git授权不再与该文件矛盾；Git授权不包含新的构建或实现授权。2026-10-10用户已明确授权同步本次测试补强并运行一轮完整4×4流程。
- 远端构建/CoSim/Vivado/板测的授权**不随git授权自动获得**；每次仍需明确轮次与范围。
- Split-D使用独立顶层`fsa_stream_split_d`，保持Q/K/V/O四个64-bit AXI master bundle、AXI-Lite控制、VU37P器件、10ns时钟和2.7ns uncertainty。
- 只允许总计`D×D`条PE RawFMA。阶段3允许按query列空间划分为两个`D×B`块，每块内QK/softmax/PV顺序复用，合计不得复制完整阵列；每个query列仍只允许一个Accumulator及倒数通路。
- Split-D每个PE只保留一个FP16 `reg`和一个FP32 `score_acc`；禁止阵列外增加完整S/P副本。
- 每列只允许一个Accumulator RawFMA通路；不得用复制PE阵列或Accumulator换吞吐。
- 保持现有RawFMA、PWL精度、特殊值及舍入合同；不得用放宽误差掩盖问题。
- 不恢复已造成RTL错误的`pe_pipeline/cmp_pipeline inter false`；不恢复Accumulator反馈DATAFLOW环或单actor写多个有限DMA请求FIFO。
- 不更改时钟、器件、接口或阵列参数来掩盖综合失败。
- 当前NM37/Vitis HLS 2024.2/7事务testbench下，16×16 RTL CoSim从`## run all`开始的正常墙钟区间为15至45分钟，60分钟为硬超时；超过60分钟未完成7/7和C post-check即停止并判性能/可验证性不合格。连续20分钟无任何进度变化可提前超时。
- 远端测试的唯一标准入口是仓库根目录`./run_hls.sh fsa_stream_split_d`；16×16只用`FSA_SPLIT_D_PE_DIM=16 FSA_SPLIT_D_HEAD_DIM=128`切换参数。未经用户明确要求，不得创建wrapper、替代Tcl、自建testbench或其他测试脚本。
- 上一轮远端闭环已结束。后续智能体不得沿用旧授权继续推送或测试；开始新一轮前以用户最新授权的轮数和范围为准，每轮结束必须暂停汇报，提前全部合格则提前结束。
- 没有读取对应新build前，不得声称实例数、DSP、II、时序、CoSim或死锁问题已经解决。
- 只维护仓库根目录的`PROJECT_CONTEXT.md`；`docs/`目录中的同名文件为历史快照，停止更新。
- 用户已把本文件的外部上下文维护长期委派给智能体：发现过期、缺失或矛盾内容时直接原地修订，不必每次征询；但只能写入可验证证据，不得凭推测写入结论，也不得为了产生更新而空改。
- 远端授权边界：2026-10-10最新授权为“迭代，完成阶段1和2”，未设轮数上限，包含4×4/head16标准CSim/CSynth/CoSim及Git同步。阶段1/2已结束。最新用户“继续3”授权阶段3的Git同步与4×4/head16标准闭环迭代，未设轮数上限；当前日志`docs/修改日志/2026-10-10_0134_split_d_stage3_修改日志.md`。阶段4/5、16×16、IP、Vivado与板测未授权。

## 3 当前状态

### 3.1 Split-D当前候选

**当前状态，以本段为准：**阶段1/2在`25d6dda`正式全流程验收完成；该未分块硬件是阶段3对照。阶段3当前未提交候选为输入分发→两个独立4×2模板worker→单输出汇聚的前向DATAFLOW，深度2的header及深度8的Q/KV/O FIFO。块内Q/K/V数组和原算术顺序保留。全部24有效事务已内嵌位基线；本地B2/B4数学24/24、严格19/24、exit1。尚无新官方CSim/综合/CoSim或并发证据，不能替代正式基线。下面至3.2节旧候选说明均为历史。

**第二次静态核查补充：**分块B不能替代全局D/H；需保留FP32 max→FP16转换→SUB_MAX→SCALE→PWL量化顺序、K/V外存读取次数和总D条倒数通路。当前reference是double精确exp的数学参考，不证明重构位级一致；后续在既有测试中同时验收独立误差与固定输入的原始O位模式基线。HLS PE先舍入FP32再转FP16/FTZ，而Chisel分别从raw输出舍入，两者完整合同一致性仍待定向证据。广播/汇聚需证明有限缓冲等待关系，前向图不能单独证明无死锁。

**4×4/16已冻结（2026-09-30）：**被测commit`cf1198715c087f274f738ae283a0b6d529e3e5cf`（PE阵列按bank分组、bank内用固定规模一维节点数组）。规范命令`./run_hls.sh fsa_stream_split_d`完整流程通过：CSim PASS、CSynth顶层**7.300ns**、BRAM8/**DSP40**/FF31486/LUT125477、`runPeArray`与`peBankMacUnit`均Final II=1、QK/ROW_SUM achieved5/target5、PWL/PV achieved1/target1、无`too complicated`、RTL CoSim **9/9 PASS**且C post-check通过。相对旧基线FF/LUT各增约1.7k/0.8k（来自bank节点寄存器），时序、DSP与II均未回退。

**最近一次有结果的build：**2026-09-29 10:44的Q/K内联候选，被测commit为`83b9b51585cbc3ed4002f596b2c437a8afae8523`。4×4/16的CSim、CSynth和RTL CoSim全部通过；CoSim完成9/9且C post-check通过。

- 顶层为DSP40、FF29732、LUT124651、BRAM8；共享PE/Accumulator及16个减法器的资源结构符合目标。
- QK和ROW_SUM达到目标II5，PWL和PV扁平循环均达到II1；`stagePeArrayResult`和`stageAccumulatorResult`均为latency1/II1且DSP0。
- 最新已测build使用8路PV交错；`pv_sum RAW distance=8 true`为真实反馈，`output_acc inter false`只处理已确认安全的假相关。
- 顶层估算周期为7.300ns，正好满足7.300ns有效预算；最新顶层最坏延迟为427162627 cycles。

**当前源码修改：**

- **拆分后待验证状态（Confirmed，已由`2c94ef1`的CSynth验证）：**拆分只做了代码搬家，未主动改变算法、pragma、循环顺序或函数签名；8个关键函数各只有1处定义（`runPeArray`、`runAccumulatorColumns`在`split_d_compute.cpp`，`loadElemTile`、`loadValueTile`在`split_d_dma.cpp`），`run_hls.tcl`已列入全部5个实现文件。Q/K/V加载的`#pragma HLS INLINE`在移入独立翻译单元后**仍然生效**，4×4/16 CSynth已确认没有生成独立加载模块。
- commit`2c94ef1`（拆分本体）：4×4/16拆分回归**已通过**。远端CSim PASS、CSynth PASS（16:02:39–16:09:26），顶层Target 10.00ns/Estimated7.300ns/Uncertainty2.70ns，BRAM8、DSP40、FF29732、LUT124651，最坏延迟427162627 cycles，与拆分前基线**逐项相同**；RTL循环QK/ROW_SUM achieved5/target5、PWL/PV achieved1/target1；`stagePeArrayResult`/`stageAccumulatorResult`为latency1/II1/DSP0。跨翻译单元的Q/K/V强制`INLINE`**仍然生效**（无`loadElemTile`/`loadValueTile`子报告与RTL模块）。本轮范围只到CSynth，拆分后RTL CoSim未执行。
- 16×16/128已完成CSim和CSynth：CSim通过，估算周期7.300ns，生成一套256PE和一套16列Accumulator；但`runPeArray`及其QK、ROW_SUM、PWL、PV调用循环流水失败，CSynth约耗时2小时41分。RTL CoSim运行到4/7后按用户要求停止，未完成C post-check，数据状态为未验收。
- 以下条目是**拆分前**候选的历史过程记录（对应当前`PROJECT_CONTEXT.md`里已不存在的单文件源码），保留用于避免重走已排除的路线：已删除`hls::task`、`hls_thread_local`、四条命令/结果stream和`run()`层DATAFLOW，Tcl恢复普通`cosim_design -rtl verilog`；`runPeArray`/`runAccumulatorColumns`保持STP II1并在内部调用独立STP II1/latency1结果级，16:33 CSynth确认分级切断调用返回写回路径使顶层回到7.300ns；`runController`对两者各设`ALLOCATION function ... limit=1`，13:50 build确认各只有一个物理实例；QK和ROW_SUM保留真实反馈并把目标II改为5，不使用虚假依赖声明；PV为8个feature上下文交错的单一固定边界II1循环，`output_acc`声明`inter false`、`pv_sum`声明`RAW distance=8 true`，无外层outline也无`PIPELINE off`；`loadElemTile`由`INLINE off`改为强制`INLINE`，feature0与last-feature basis诊断在RTL中通过，确认Q/K加载错位已修复；PWL由task批量请求/回收改为有限流水调用，8段扫描期间保持`PE.reg`中的X、命中结果暂存到已不再保存S的`PE.score_acc`，无阵列外P副本。
- 11:03的FRP源码候选仍在相同14160ns死锁；本地没有同步该次`csynth.rpt`和`sim/verilog`，无法确认工具是否真正采用FRP，因此不能把它当作有效硬件修复。
- AMD Vitis HLS文档明确要求含dataflow task和M_AXI的CoSim启用`-enable_tasks_with_m_axi`；该开关在11:17复验中仍死锁，证明原问题是结构闭环。当前已无task，因此Tcl不再使用该开关。
- 11:17 deadlock report确认控制器阻塞于空`pe_result_stream`，KPN阻塞于空PE/Accumulator命令流，没有任何写端因FIFO满而阻塞。按AMD官方判据，这不是FIFO深度不足，而是设计结构问题。
- 当前`runController`同时是task命令生产者和结果消费者，跨`ap_ctrl_chain`控制区与`ap_ctrl_none` KPN形成闭环。AMD混合task/dataflow模型要求task输入由先于task的普通进程产生、task输出由后于task的普通进程消费；同一个控制器承担两端不满足该前向拓扑。
- QK和ROW_SUM直接调用共享PE模块，并显式接受II5真实反馈；不使用虚假的PE反馈`DEPENDENCE false`。
- 当前未测候选每组交错8个独立feature：每个feature内部仍按key row原顺序累加，只使用`8×D`个FP32临时partial，不改变FMA顺序；16维和128维都能整除8，不增加目标配置的空上下文操作。
- 历史候选曾只完成本地math stubs回归；当前本地编译入口和官方26事务结果见第10节及最新阶段记录，旧“拆分后不能本地编译”结论作废。

### 3.2 已验收生产基线

生产顶层`fsa_stream`最新完整基线为2026-09-21 01:16的hop5+22位CLZ build：

- CSim、CSynth、Verilog RTL CoSim及C post-check通过，无死锁和数值错误。
- 9x4 non-causal/causal为1498/1142 cycles；SA主循环II1，Tile latency/interval为106/101。
- 顶层资源BRAM20、DSP32、FF71336、LUT132486；单16 PE、4 CMP、单4-lane Accumulator保持。
- SA估算7.120ns，顶层7.300ns；Q/K/V DMA关键读循环II1，Accumulator vector 7拍/II1/DSP8。
- 尚无IP导出、Vivado实现后时序或板级证明。

## 4 架构模型

### 4.1 Split-D未分块基线与阶段3候选

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
- 阶段3候选将上图逻辑阵列沿query列拆为两个4×2块，各有本地Q/max/L/O/PV反馈；分发器的K/V外存读取次数保持原值，广播给两块，单汇聚器按全局query写O。块内接口为D×B，D/H及token/feature步长不变。物理并行、总算术资源、FIFO活性和新增缓冲须由新生成物验收。

### 4.2 生产基线

- 单`R×C` SA、C个CMP、单C-lane Accumulator；QK与PV顺序复用。
- 四个专用AXI bundle、64-bit存储字、三路独立DMA请求actor、唯一Scratchpad owner。
- PE槽为`cycle%5`，CMP槽为`cycle%4`；只保留已验证安全的PE寄存器调度表达。

## 5 关键决策

1. **单阵列优先。**任何II改善必须在一套PE阵列和一套Accumulator条件下成立；资源复制获得的II1不验收。
2. **先补测试与结构证据，再优化物理实现。**P3a功能与HLS指标已通过，当前没有确认新的RTL数值错误；先修测试漏报及覆盖，再查共享实例。冻结算术合同，后续以query分块与通信流水做单因素对照。
3. **保留精度合同。**PE使用FP16×FP16+FP32 RawFMA；Accumulator使用FP32×FP32+FP32 RawFMA；softmax/PWL与现有位精确参考一致。
   当前结构修改固定已验收HLS算术路径，不能用0.03数学误差通过替代位级保持证明，也不能据此声称Chisel所有舍入/特殊值均一致。涉及公共算术时，另核对既有RawFMA测试和受影响生产基线。
4. **以生成物为证据。**函数定义唯一、源码`UNROLL`、ALLOCATION pragma或总DSP任一单项都不足以证明单阵列；必须交叉检查层次、RTL实例、运算符和循环调度。
5. **本地测试与HLS验收分开。**C++通过只证明功能，不证明综合结构、II、时序或RTL正确性。
6. **共享必须有实例证据。**当前新RTL已证明六个调用点共享唯一阵列，不因源码跨层调用而改共享层级。后续query块应借鉴生产stream的模块通信与显式token流水，保留块内状态/反馈，不退回旧pe_step事务模型，不复活旧同步请求/响应task闭环。
7. **区分服务间隔和反馈延迟。**单元latency可大于II；真实反馈需满足依赖距离×II覆盖完整反馈路径延迟，并满足资源服务能力。物理通信插级后重新核对PV的distance8，不用假依赖掩盖问题。

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
| 把PV交错从8路增至16路 | 2026-09-29 01:15 CoSim的结束时刻和首事务`max_error=0.199228`与8路版完全相同；独立`L=1`仍全0 | 8路反馈距离不足不是当前根因；下一有效候选若无其他理由应恢复8路 |
| 生产基线hop4 | II1但11.003ns时序失败 | 当前保留已验收hop5+CLZ |

## 7 当前工作集

- Split-D配置与类型：`include/fsa/stream/split_d/`
- Split-D顶层：`src/stream/split_d/fsa_stream_split_d.cpp`
- Split-D主控制器：`src/stream/split_d/split_d_controller.cpp`
- Split-D计算模块：`src/stream/split_d/split_d_compute.cpp`
- Split-D DMA模块：`src/stream/split_d/split_d_dma.cpp`
- Split-D内部接口：`include/fsa/stream/split_d/split_d_internal.hpp`
- Split-D测试：`tests/stream/test_fsa_stream_split_d.cpp`
- 已验收基线：`docs/综合报告/Split-D阶段1与2验收_20261010.md`、`docs/evidence/split_d_25d6dda_4x4/`。当前阶段3日志：`docs/修改日志/2026-10-10_0134_split_d_stage3_修改日志.md`；本地检查：`build/local_split_d_check/stage3_round1.txt`及`stage3_n1.txt`。
- Split-D HLS入口：`hls/fsa_stream_split_d/run_hls.tcl`
- 根运行入口：`run_hls.sh`
- 当前分阶段方案：`docs/Split-D修正与TAPA启发的分阶段修改验收方案_20261009.md`；旧20260930计划与DeepSeek交接用于历史参考，冲突处以新方案的核查结果为准。
- Split-D历史交接：`docs/split_d_implementation_plan_20260923/PROJECT_CONTEXT.md`（停止维护，不作为当前状态来源）
- Split-D迁移交接：`docs/Split-D_FSA_DeepSeek迁移交接_20260929.md`（当前任务的第2优先级来源）
- `hls/fsa_stream_split_d/fsa_stream_split_d_build/`：服务器已由`25d6dda`成功构建覆盖；顶层报告01:16:19、CoSim报告01:19:51，标准流程01:12:31—01:20:01。原始报告、TV快照及RTL哈希已归档；后续运行仍会覆盖。
- 本地`build/fsa_stream_split_d_build/solution1/`是**拆分前**的旧生成物（最新`csynth.rpt`为2026-09-28 16:51，仍含已删除的`peArrayTask`/`accumulatorTask`/`KPN`模块），只能作为历史基线，**不得**用于描述当前源码。

默认参数为`FSA_SPLIT_D_PE_DIM=4`、`FSA_SPLIT_D_HEAD_DIM=16`；目标参数为`16/128`。

## 8 下一步

1. 阶段1/2验收已完成。阶段3已授权：提交本次六个源文件/测试、日志与上下文（保留用户AGENTS.md改动），推送精确commit，服务器只用官方入口运行4×4/head16完整流程。严格24/24与数学24/24均须通过。
2. 保留唯一PE/Acc共享层级。空间分块时须继续说明阶段寄存器、反馈上下文及缓冲容量；不把算法字段复用误称为物理资源复用。低风险死代码清理仍需独立范围和新4×4验证。
3. 新生成物检查两个worker独立启动、各唯一8PE/2Acc/2倒数，总PE16/Acc4/倒数4；QK/ROW_SUM II≤5、PWL/PV II≤1、HLS≤7.300ns，CoSim26/26及逐word位基线。保留D步长、全局causal、量化顺序、8上下文PV和原K/V读取次数；记录新增缓冲容量与周期变化。失败则先读取证据和搜索官方解法，再继续同一日志的新轮次。
4. 获得物理验证授权后先导出正式IP并明确OOC/完整集成载体，再对照自动布局、粗粒度约束和距离感知流水；检查实际AXI宽度、反馈、背压、setup与hold、DRC及端到端时间，不把OOC或HLS估算写成板测结果。
5. 16×16仍暂缓。恢复时先检查当前单元II配置、15/16/17/32/33边界和物理分块，详见新方案阶段5。H=128的QK是128次逐feature求值，旧64拍估算漏算lane，作废。

## 9 验证状态

### 已完成

- 旧`9c49789`日志与RTL已核对：26/26 Pass、66426 cycles；唯一PE阵列/唯一列Accumulator；DSP16＋8＋16，原26项周期已保存。以下旧运行条目仅作历史证据。
- 本轮`25d6dda`本地编译成功、数学24/24、严格15/20、exit1；5项宿主stub位差保留。官方相同固定输入严格20/20；数学24/24、26/26 CoSim、完整C/RTL O一致；资源/II/估算周期保持旧基线。上轮`46cbbd0`失败日志为历史，不作为当前验证状态。

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
- 2026-09-28 16:15 build：CSim/CSynth通过，DSP40；QK/ROW_SUM达到II5，PV由II9改善到II4，但`pv_sum`仍被按distance 1分析，顶层仍为7.932ns。固定LATENCY没有切断组合返回路径，因此该版不合格。
- 2026-09-28 16:33 build：4×4/16的CSim和CSynth通过；单PE/单Accumulator、DSP40，QK/ROW_SUM II5，PWL/PV II1，周期7.300ns，无II/时序错误诊断。显式结果级和真实distance 8方案通过CSynth验收。
- 2026-09-29 00:42 build：输出写回展平后，CSim/CSynth通过并保持DSP40、QK/ROW_SUM II5、PWL/PV II1、7.300ns；RTL CoSim从此前7小时20分仍0/6改善为192845ns完成6/6，确认AXI写响应卡死已解决，但C post-check在首个`L=7, causal=0`用例出现`max_error=0.199228`并报`Bad TV file`，数值验收仍失败。
- 2026-09-29 01:15 build（commit `b0ab080`）：PV交错由8改16后，本地4×4/16和16×16/128通过；远端4×4/16 CSim/CSynth通过，DSP40、BRAM8、FF30467、LUT124857、QK/ROW_SUM II5、PWL/PV II1、7.300ns。RTL CoSim仍在192845ns完成6/6，但首事务仍`max_error=0.199228`，证明增加PV反馈距离无效。TV分析还显示独立`L=1`事务整片RTL输出内存全0，`L=7`因果仅单key的query0精确通过，多key查询均有误差。
- 2026-09-29 01:43 build（commit `58eecaa`）：恢复8路PV并加入诊断用例；CSim/CSynth通过，DSP40、BRAM8、FF29415、LUT123434、7.300ns，QK/ROW_SUM II5、PWL/PV II1。CoSim 8/8于208505ns完成，无deadlock，但7个有效事务均失败。TV输入正确；输出证明V tile滞后一事务，综合层次显示K/V在key-loop outline中共用一个`loadElemTile`子模块。
- 2026-09-29 02:04 build（commit `6c80d49`）：K/V拆成独立加载子模块后，CSim/CSynth与目标II、7.300ns继续通过，CoSim 8/8于205005ns完成且无deadlock；首个单key事务通过，但其余6个数值用例失败。全1-V事务输出0，随后basis-V事务输出约0.5，证明独立V子模块仍在后续顶层事务使用前一笔V。
- 2026-09-29 02:14 build（commit `842ef77`）：V加载强制内联后，独立`loadValueTile` RTL模块消失；CSim/CSynth通过，DSP40、BRAM8、FF29745、LUT124445、QK/ROW_SUM II5、PWL/PV II1、7.300ns。CoSim 8/8于225795ns完成，无deadlock；单key和全1-V通过，证明V事务错位已修复。basis-V仍输出均匀0.5/0.5而非0.562177/0.437823，随机用例最大误差0.054至0.075，数据仍未验收。本次达到3轮上限并停止。
- 历史commit`842ef77`的4×4/16 RTL CoSim曾完成8/8但数值失败；后续commit`83b9b515`内联Q/K加载后，4×4/16已完成9/9全验收。16×16/128已完成CSim和CSynth，但流水失败且CoSim只完成4/7。

### 未完成

- 阶段3官方全流程及空间并发/资源尚未验收。本地5项stub位差逐运算归因、跨阶段物理寄存器复用优化、IP及实现时序未完成；全部24项官方期望已内嵌，阶段1/2正式验收已完成。
- **16×16/128 的目标II与完整验收：暂时搁置**（用户2026-10-09指示只做4×4）。
- Split-D 的 IP 导出、Vivado 实现后时序、板测（两档都未做）。
- 生产基线 IP 导出、Vivado 实现后时序和板测。
- 代码清理：`peBankMacUnit` 在 P3a 后已无调用点（定义在 `split_d_compute.cpp:51`），需清理或复用；`PE_ARRAY_II` 在16×16仍为5，是否改1待16×16恢复后按是否复发`204-65`决定。

## 10 环境与复现

- 本地：Windows PowerShell；`tools/local_split_d_check.cmd`使用仓库头文件、MinGW及`FSA_LOCAL_MATH_STUBS/HLS_NO_XIL_FPO_LIB`。阶段3本地B2及B4单块控制：编译成功，数学24/24，严格位模式19/24，整体exit=1（相同5项桩库差异）。桩库结果不替代Vitis/RTL；仍无本地Vitis/Vivado验收证据。
- 服务器：Vitis HLS 2024.2，器件`xcvu37p_CIV-fsvh2892-2-e`。
- 时钟：10.0ns，uncertainty 2.7ns；不得放宽。
- Split-D服务器命令：`./run_hls.sh fsa_stream_split_d`。
- 生产基线命令：`./run_hls.sh fsa_stream`。
- 分支`fsa_split_D`；2026-10-10被测commit`25d6dda678104d8a856292b83c1c48607a1148fd`已推GitHub并由服务器fast-forward拉取，运行前及取证时HEAD一致。后续仅文档提交不算HLS被测版本；远端成功生成物对应25d6dda。
- 旧`source_manifest.json`和`delivery_validation.json`是历史快照，不代表当前源码。

## 11 交接摘要

当前保持单阵列Split-D。阶段1/2在`25d6dda`官方全流程验收完成：24/24数学、20/20严格基线、26/26 CoSim、7.300ns/DSP40/II5/5/1/1。PWL RNE可达性错误已修复，唯一PE/Acc及S/P用途已审计；HLS保留阶段寄存器，不能声称物理仅一组状态。验收/证据见第7节。阶段3已授权并完成本地候选与24项严格基线扩展，官方空间分块验收待执行；16×16继续暂缓。

第二次核查已修订方案：保留D/H和全局索引、K/V读取量及现有量化链；补倒数通路、位模式基线、广播/汇聚等待关系、IP导出与OOC/全系统时序等级。范围以方案第13节为准，不声称所有独立模块已逐行审计。失败构建读取本轮临时build，旧成功目录可能仍存在，不得误引用。

**2026-10-10最新状态（覆盖以上）：**本次“迭代完成阶段1/2”一轮收敛，25d6dda全流程通过；其后用户“继续3”已授权4×4空间分块闭环，当前日志0134；不沿用旧调用1/1上限，不进入阶段4/5或16×16。无IP、Vivado实现或板测证据。

## 11.1 本地git推送的环境坑（2026-09-30，已修复）

- **症状**：本地`git push`必然失败，报`ssh.exe: *** fatal error - couldn't create signal pipe, Win32 error 5`，在DSH的`workspace-write`沙箱下必须每次申请升级才能推送。
- **根因**：**msys运行时**问题，不是"push要经由sh"。git默认使用的`C:\Program Files\Git\usr\bin\ssh.exe`依赖`msys-2.0.dll`，该DLL在每个msys进程启动时创建内部signal pipe，而该spawn路径不在DSH已修补DACL的范围内，因此在`workspace-write`下必然ACCESS_DENIED。同一台机器上`Git\usr\bin\sh.exe`报同一条错，是同一根因。
- **修法（已落地）**：把git的ssh换成**原生Win32 OpenSSH**，用**裸路径**（不带引号、不带参数）：
  ```bash
  git config --global core.sshCommand C:/Windows/System32/OpenSSH/ssh.exe
  ```
  原生ssh直接`CreateProcess`，不经过msys，因此不再触发signal pipe。**注意**：写成`GIT_SSH_COMMAND='...ssh.exe -o BatchMode=yes'`这类带引号或带参数的写法会被判成需要POSIX shell包装的variant，又会落回msys路径而失败——必须用裸路径。
- **验证**：`git ls-remote origin`与一次真实`git push`（`adc49cb..645be9f`）均在**未申请任何权限**的情况下成功。
- **注意**：`.gitconfig`不进版本库，换机器或重装需重设；DSH沙箱对工作区外写入（`C:\Users\30130\.gitconfig`）仍需一次升级授权才能设置本项。

## 12 本项目可用的DSH skill

本项目的流程已作为DSH skill安装在用户级`C:\Users\30130\.dsh\skills\`，源件保存在`skills/`（源件不随会话变化，安装副本由源件改写得到）：

- `fsa-hls-remote-iteration`：远端Vitis闭环迭代（第10节单轮闭环的操作化版本），调用后可获得该轮的远端测试与日志维护流程。
- `vitis-hls-build-report`：读取build目录生成中文综合报告（第9节报告约定的操作化版本）。
- `vivado-hls-ip-board-test`：由HLS IP生成NM37上板测试包。
- `maintaining-project-context`：维护本文件。
- `rewrite-scientific-workflow`：论文/研究叙事组织。

调用方式：在输入框键入`/`并从建议中选择，或直接键入`/skill-name`。用户侧候选来自宿主`skills/list`（按`user-invocable`过滤）；模型侧则靠会话skill目录按任务自动匹配。远端迭代、综合报告等流程优先直接调用对应skill，不要凭记忆重述其规则。

维护提示：源件在`skills/`，安装副本是**改写后的拷贝**，改动源件不会自动生效；更新源件后必须重新执行"拷贝→按DSH约定改写→安装到`~\.dsh\skills`→删除临时副本"。已做的改写只有两处：`fsa-hls-remote-iteration`的宿主名`Codex`→`DSH`，`vivado-hls-ip-board-test`的描述由742字压缩到470字（DSH目录描述上限500字，超出会被截断并丢失触发词）；其余3个skill的正文与源件逐字节相同。所有安装副本都已丢弃`agents/openai.yaml`（ChatGPT/Codex专用界面元数据）。`fsa-hls-remote-iteration`的`SKILL.md`和`references/fsa-hls-workflow.md`已在2026-09-29按用户要求改写，两份（源件与安装副本）逐字节一致。

## 13 远端迭代的一轮怎么走

2026-09-29用户重定了`fsa-hls-remote-iteration`的一轮结构，之后按此执行：一轮 = **一次"push → 远端测一次"**，从本地提交推送开始，到读完该commit的远端数据并给出结论为止。

1. 推送本地commit（含本轮日志与`PROJECT_CONTEXT.md`更新）；2. 远端SSH+显式加载`~/.bashrc`、`git pull --ff-only`、核对远端HEAD等于该commit；3. 远端Vitis测试；4. 读结果（报告、日志、RTL、资源）；5. 判断是否合格；6. 不合格则**先联网搜索解决方案**并记录来源与结论；7. 本地更改并做本地验证；8. 回到第1步，该修改成为下一轮被测commit。

- 终止条件：验收全部通过、达到用户设定最大轮数、出现阻塞、或需要用户作出新的设计决定；每轮结束都暂停并向用户汇报。
- 第6步的联网搜索已纳入该skill的站立授权，不必每轮单独询问。Git同步按AGENTS.md最新规则及用户授权执行；2026-10-10本次一轮授权包含补强同步。
- 与本文件第2节一致：最新任务为用户“继续3”授权阶段3迭代，未设轮数上限；每轮读完结果并汇报后判断下一轮，不沿用旧调用的上限，不扩大至阶段4/5。

## 14 服务器测试的官方协议（2026-09-29用户规定）

- **只允许仓库自带入口，且只有这两条命令**：4×4/16用`./run_hls.sh fsa_stream_split_d`；16×16/128用`FSA_SPLIT_D_PE_DIM=16 FSA_SPLIT_D_HEAD_DIM=128 ./run_hls.sh fsa_stream_split_d`。
- **禁止自定义测试脚本、wrapper、替代Tcl、临时testbench**；禁止改`run_hls.sh`或`hls/fsa_stream_split_d/run_hls.tcl`的流程来跳过CSim、CSynth或CoSim，也不得用收窄测试范围的方式规避某一步。
- 运行前用Bash加载`~/.bashrc`（远端该文件source AMD/Xilinx 2024.2的`settings64.sh`，**只有交互式bash才读**），并确认服务器拉取的是本轮**精确commit**。
- 只读命令（`grep`/`rg`/`sed`/`find`）可以读build与报告，但不得用来生成替代流程。
- 服务器上既有的无关未跟踪文件（`evidence/`、`logs/`、`vitis_hls.log`、`vivado*`）不得删除或修改。
- **CoSim墙钟门槛**：从xsim输出`## run all`开始计时，到7/7事务、RTL仿真退出和C post-check结束为止。15–45分钟为正常；45–60分钟为警告但仍继续等待；超过60分钟仍未完成7/7与C post-check即**立即中断**，判定CoSim超时、性能/可验证性不合格；连续20分钟无任何事务或内部进度变化可提前按超时处理。
- 超时后必须保存并报告当前build证据：已完成事务数、最后进度、是否出现deadlock、C post-check是否执行；数据只能标为**未验收**，不得写成通过，也不得写成数值失败。
- 2026-09-29违反与纠正记录：本轮迭代中我曾用临时Tcl（`set RUN_COSIM 0`）收窄范围，并写过多个自定义启动/轮询脚本与`#ifdef`诊断打印。这些做法已被上述协议取代，**不得再用**；远端工作树与`build/`事后已恢复干净。

## 15 16×16失败的根因证据与架构调研方向（2026-09-30）

> 2026-10-09核查修正：本节保留历史日志和候选推论。以下“调用延迟必须≤II”“H=128的QK约64拍”“arrays句子证明204-65根因”“D×P统一key/head归约”“分次调用仍保证256物理PE”均不作当前结论；解释与执行顺序以新分阶段方案为准。P3a的已核实数字仍有效，单实例与实现时序另需证据。

### 15.00000 4×4/16 已达标：P3a 形状（2026-10-09 二次确认，含阶段1测试台）

**结论（Confirmed）**：被测 commit **`9c49789`**（P3a 形状 + 阶段1扩展测试台），规范命令 `./run_hls.sh fsa_stream_split_d`，2026-10-09 22:41:59–22:49:06（总 7分07秒），**全部判据合格**：

| 判据 | 实测 |
|---|---|
| CSim / CSynth / RTL CoSim | PASS / 通过（2分47秒） / **Pass**（**26/26 事务**，总 66426 cycles，C post-check通过） |
| **顶层估算周期** | **7.300ns**（ap_clk 10.00 / estimated 7.300 / uncertainty 2.70） |
| 资源 | DSP **40**、BRAM 8、FF **31923**、LUT **125123** |
| QK 累加循环 | `runPeAccumulateTile` 的 `VITIS_LOOP_148_1_VITIS_LOOP_149_2`：Target 5 → **Final 5** |
| ROW_SUM 循环 | `runPeRowSum` 的 `VITIS_LOOP_204_1`：Target 5 → **Final 5** |
| PV/扁平循环 | `VITIS_LOOP_363_28`：Target 1 → **Final 1**；`VITIS_LOOP_294_20`：1 → 1 |
| `runPeArray` | Final **II=1**、Depth 4、**latency 3** |
| `200-880` / `200-875` / `too complicated` | **0 / 0 / 0** |
| CoSim 逐事务 | Latency min/avg/max = 50 / 2564 / 8007 cycles；Interval = 45 / 2655 / 7997 |
| 各阶段耗时 | CSim 36s、CSynth 2分47秒、CoSim 3分10秒 |

**与旧记录（`426ff6c`，9事务）的关系**：本次事务数由 9 增至 **26**（新增边界、定向与非法长度用例），**顶层时序、资源、`runPeArray` II/latency、各循环 II 全部逐项不变** → 同时证明"测试台扩展未改变综合硬件"，并**替换掉**了被 2026-10-01 来源不明 build 覆盖的 `syn/report`，提供新鲜的 4×4 物理资源证据。

**阶段2补充（2026-10-10）**：25d6dda新构建再次确认唯一PE/Acc、16/4尾数乘法及8个fsub，完成RawFMA算术链与S/P存储审计；算法无额外备份，物理阶段寄存器用途已说明。详见新验收报告；本节9c49789数据保留作历史。

**证据出处**：远端 `/tmp/s1_4x4_summary.log` 与 `/tmp/s1_4x4.log`（含 `COMMIT_OK` 与 `HEAD=9c49789…`）；文档 `docs/阶段1本地改动审查结果_20261009.md` §3.5。**注意不要引用 `syn/report` 的历史版本**——本次运行已把它覆盖为自洽的新鲜报告，但后续任何运行都会再次覆盖它。


**历史记录（`426ff6c`，2026-09-30 13:52，9事务）**：同一 P3a 形状的首次达标，六项判据与上表**逐项相同**（7.300ns、DSP40、BRAM8、FF31923、LUT125123、QK/ROW_SUM Final II 5、PV Final II 1、`runPeArray` latency3/II1、`200-880/875/too complex` 均 0、CoSim Pass），只是事务数为 9。证据：远端 `/tmp/p3a_4x4_summary.log`、`/tmp/p3a_4x4.log`（`HEAD=426ff6c…`）。

**机理（Decision + Reason + Evidence）**：把 `runPeArray` 的 rolled bank 循环（16次调用、结果经 node 数组写回）替换为**整个阵列在同一个流水体内原地求值**，使 `result[row][col]` 的写回索引由循环变量变为**编译期常量**。证据：同一 4×4 配置下，rolled 形状为 7.893ns（P0 `11b10c9`、P1 `f9f0638` 两次实测一致），P3a 为 **7.300ns**；DSP 与所有 II 均未退化（资源仅 FF/LUT 小幅变化）。
**Implication（2026-10-09修正）**：4×4周期改善与形状变化有对照证据，但**关键路径归因仍待细化**。调用latency与II不能等同，16×16的服务间隔和完整反馈延迟尚未测；不能从4×4的3拍推导16×16达标。

**用户指示（2026-10-09）**：**只做4×4，暂时不做16×16的验证**；来源不明的2026-10-01 build不追查，后续自测时覆盖。

### 15.0000 服务器连通性阻塞（2026-09-30 13:50 起，2026-10-09 已解除）

- 阻塞期间 `ssh FSA-FPGA-NM37-tailBox` 报 `Connection timed out during banner exchange`；沙箱外 `tailscale status` 显示别名指向的 `desktop-4h6raeb / 100.114.138.109` 为 `offline`（`tx 9984 rx 0`），本机 `diego-legion` 在线 → **是目标主机掉线，tailnet 正常**。
- **2026-10-09 已恢复**：同一命令返回 `REACHABLE / lenovo-ThinkStation-P920`。服务器基本状态：远端HEAD `426ff6c`、tracked工作树干净、无残留`vitis_hls`/`xsim`、`vitis-run`可用、磁盘712G可用。
- 环境提示：在DSH沙箱内跑 `tailscale status` 会报 `open \\.\pipe\ProtectedPrefix\Administrators\Tailscale\tailscaled: Access is denied`（沙箱禁止命名管道），**不代表服务异常**，该诊断必须在沙箱外执行。

### 15.000 P1 结果：依赖搬家不足以解决 II（2026-09-30 13:30）

P1 把 QK 与 ROW_SUM 的内层累加搬入 `runPeAccumulateTile` / `runPeRowSum`（commit `f9f0638`），让控制器每 tile 只调用一次、内部数组每迭代私有。**实测结果**：

| 配置 | 顶层估算 | II | 200-880 | 200-875 | CoSim |
|---|---|---|---|---|---|
| 4×4（`f9f0638`） | 7.893ns（与P0相同） | 5/5/1/1 | 0 | 0 | **9/9 PASS**（FF32652/LUT125762/DSP40） |
| 16×16（`f9f0638`） | 7.934ns（与P0相同） | 仍为 **8**（目标5） | **4** | **8** | **7/7 PASS**（FF177842/LUT550164/DSP160，CSynth 21分58秒） |

**P1 判定：中性与无回退。** 4×4 与 16×16 的数据、时序、II 与 P0 完全一致（16×16 的 CoSim 延迟/间隔也与 P0 相同：avg 15462 / 总 108175 cycles）。即"把内层累加搬入函数 + 数组每迭代私有"**不足以**改善 II。

**重要副产物：性能指标需要重新理解。** 两条违例的原始文字显示：

- `200-875` 报的是 **II=6 和 II=7 都不可行**（不只是 5），因为"流水迭代延迟=8 与调用不可重叠"矛盾；
- 也就是说受限的是**流水迭代延迟 8 拍**，而不是 `Target II` 这个数字。

因此 P3 的判据要从"II 数字"改成**每次 tile 的总延迟**：QK 每 tile 总延迟 ≈ `PE_DIM × DIM_BLOCKS × max(迭代延迟, 发射间隔)`。现状（16×16）：每 lane 一次调用、每次 ~8 拍、8×1=8 次调用 → **每 tile ≈64 拍**。若能把 bank 求值压到 1–2 拍并让 lane 发射间隔为 1，则可降到 **8–16 拍**，即 4–8× 提升——这与"每拍有效 MAC 从 32–51 提到 256"是同一件事。

**关键发现：两条违例现在物理上不可满足。** 归属已从控制器搬进新函数内部（改动范围变小）：

```text
[200-880] in module 'runPeAccumulateTile' (loop VITIS_LOOP_175_1_176_2):
  carried dependence (II=5, distance=1) store 'operand_c_write_ln196' -> load 'operand_c_load_1'
[200-880] in module 'runPeRowSum' (loop VITIS_LOOP_231_1):
  carried dependence (II=5, distance=1) store 'row_sum_write_ln255' -> load 'row_sum_load_1'
[200-875] II = 6/7 infeasible due to multiple pipeline iteration latency = 8
  and incompatible II = 5 of 'call' operation to 'runPeArray' (split_d_compute.cpp:200)
```

即：**只要流水体内还有一次需要 8 拍完成的 `runPeArray` 调用，II=5 就不可能达成**——工具明确说明 II=6/7 与"调用不可重叠"矛盾。P1 的"搬家"策略因此被证伪（对 II 而言），但它把改动范围收敛到了两个函数的内部，**对 P3 是有利的**。

**P3 的设计因此更明确**：必须让 `runPeArray` 的**迭代延迟 ≤ 5 拍**（当前 8 拍），或者把该调用从流水体内消除。已在做的最低成本切入点是：在一次 `runPeArray` 求值内完成全部 `DIM_BLOCKS` 轮累加（把块循环移入函数），使 `runPeArray` 每 tile 只被调用一次。

### 15.00 当前最佳状态与唯一剩余差距（2026-09-30 12:50，含P0复测）

**数据已经全对，两档都通过CoSim；唯一不合格项是时序（两档都是）。**

P0 在正式默认参数下复测（commit `11b10c9`，规范命令，12:47 完成）：

| 配置 | CSim | CSynth | RTL CoSim | 顶层估算周期 | II（QK/ROW_SUM/PWL/PV） | 资源 |
|---|---|---|---|---|---|---|
| **4×4/16** | PASS | 通过（2分52秒） | **9/9 PASS**（C post-check通过） | **7.893ns** ✗ | **5/5/1/1** ✓ | DSP 40、FF 31737、LUT 125288、BRAM 8 |
| **16×16/128** | PASS | 通过（25分37秒，`runPeArray` Final II=5，无`204-65`） | **7/7 PASS**（C post-check通过） | **7.934ns** ✗ | **8/8/5/5** ✗ | DSP 160、FF 164470、LUT 545729、BRAM 8 |

**P0 的关键修正**：4×4 的 II 与资源全达标，但**时序同样是 7.893ns**——`cf11987`（单bank + 独立`stagePeArrayResult`）曾经达到的 7.300ns **在当前 rolled 形状下没有复现**。因此：

- 4×4 与 16×16 **共享同一个时序根因**：rolled bank 循环使 `result[row][col]` 的写回索引成为循环变量，写回路径上多了选择逻辑（约 +0.59ns）。
- 两档的**唯一差距都是时序**；16×16 另外还有 II 未达标（8/8/5/5）。

**关键机理（四组对照，已钉死）**：

| 版本 | bank机制 | 4×4顶层 | 16×16顶层 |
|---|---|---|---|
| `cf11987` | 单bank + 独立`stagePeArrayResult`寄存级 | **7.300ns** ✓ | 综合失败(`204-65`) |
| `3a92f73` | rolled bank，**无**寄存级 | 7.893ns ✗ | 7.934ns ✗（数值已正确） |
| `caedc93` | rolled bank，bank内`node_staged`寄存级 | 7.893ns ✗ | 7.934ns ✗（数值已正确） |
| `9a6a100` | 再给bank函数加`LATENCY min=1 max=1` | **20.851ns** ✗✗ | 已中断（更差） |

**II 未达标的官方原因（16×16，Vitis原文）**：
```text
WARNING: [HLS 200-880] Unable to enforce a carried dependence constraint (II = 5, distance = 1)
  between 'store' ('operand_c_6_write_ln182') and 'load' ('operand_c_6_load_1') on 'operand_c'.
WARNING: [HLS 200-875] II = 6 is infeasible due to multiple pipeline iteration latency = 8
  and incompatible II = 5 of 'call' operation to 'runPeArray'.
```

**P1 采用的改法（已定，见计划书 §4.2）**：把内层累加（`DIM_BLOCKS` 轮）与 ROW_SUM 的 `D` 行累加**移入 `runPeArray` 内部**，使 QK 内层循环不再含有延迟 8 拍的函数调用、也不再跨迭代读写 `operand_c`。这是同时消除 `200-880` 与 `200-875` 的最直接结构改法。

### 15.0 结构性阻塞解除过程（2026-09-30 02:30）

**已解阻塞**：移除 bank 循环上的 `#pragma HLS UNROLL` 后（commit `ccc0f6a`），16×16 的 `runPeArray` 不再报 `SCHED 204-65`，CSynth 由"≥100分钟未完成"降到 **26分50秒**，顶层估算仍 **7.300ns**，RTL CoSim **跑满 7/7 事务**（此前 4/7 即卡）。`runPeArray` 的 Final II=5（4×4 为 1），QK/ROW_SUM 循环 Final II=**9**（目标 5，未达标）。16×16 资源：BRAM8、**DSP160**、FF**164470**、LUT**545729**。

**当前失败点**：CoSim 的 C post-check 失败，7个用例中4个超差：

| 用例 | L | max_error |
|---|---|---|
| single-key-first | 1 | 通过 |
| two-key-ones-v | 2 | 通过 |
| two-key-basis-v | 2 | 0.144584（输出2/3、1/3 vs 期望0.522083、0.477917） |
| two-key-basis-v-last-feature | 2 | 0.144584（同上） |
| primary-noncausal | 5 | 0.427467（max_at=[0][103]） |
| primary-causal | 5 | 0.216993（max_at=[3][8]） |
| full-tile | 16 | 通过 |

**4×4仍全部通过**：`ccc0f6a`（rolled bank）4×4 CSynth 2分54秒、7.300ns、CoSim 9/9 PASS。

**已排除的假设**：把 bank 粒度临时改成2行（commit `2f568aa`）或**1行**（`2b67966`，使4×4的bank循环也迭代4次），4×4 都**完整通过**（CSim+CSynth+RTL CoSim 9/9）。因此"rolled bank 循环次数/对共享`computed`的写冒险"**不是**根因——故障**只在 `PE_DIM=16` 出现**，属于规模相关的RTL语义差异。

**收敛线索**：16×16下失败的4个用例集中在**部分tile**（L=2双key、L=5非因果/因果），而 L=1 单key与 **L=16 整tile**都通过；`two-key-*` 在评分阵列版本(`c8db70c`)也曾失败。部分tile路径（`score_valid`掩码与尾处理）与整tile/MACC规模都值得优先检查。

**下一步的两个候选修法**（尚未验证，每次16×16约45分钟）：
1. 去掉 `runPeArray` 内跨迭代共享的 `computed` 中间数组，让 bank 结果直接写入 `result` 的对应行，消除"部分写入即被读走"的可能；
2. 去掉 `runPeArray` 上的 `PIPELINE` 声明（或把 `PE_ARRAY_II` 提到 ≥ 其实际时延），避免调用者循环（QK II=5 / PV II=1）与函数流水重叠，从而在共享 `pe`/`result` 数组上产生调用间冒险。

### 15.1 已确证的直接证据

16×16（`PE_DIM=16`）在CSynth调度阶段失败，Vitis原文（4×4下同一告警出现0次）：

```text
INFO:    [SCHED 204-11] Starting scheduling ...
WARNING: [SCHED 204-65] Unable to satisfy pipeline directive for function 'runPeArray':
         control-flow is too complicated to be pipelined.
INFO:    [HLS 214-131] Inlining function 'fsa::peMacUnit(half, half, float, bool)' into 'runPeArray'
```

**根因判定（2026-09-30更正）：不是"MAC数量"。** 早期`ecdc987`/`c8db70c`的日志里出现过`Unrolling ... factor of 4 / 4 / 16`（那是`PE_BANK_ROWS=4`时的64个MAC），但`cf11987`已把bank改成`PE_DIM<=4 ? PE_DIM : 1`，D=16时是**1 bank × 16个`peMacUnit`**。然而D=16失败、D=4（同样16个MAC、单bank16个PE）却通过并达到7.300ns——**所以16个MAC本身没问题**。

质变在于**跨函数边界的数组规模**：`pe`/`operand_b`/`operand_c`/`result`/`computed`都是`PE_DIM×PE_DIM`，D=16时元素数是D=4的16倍，`complete dim=0`分区后展开为数千个独立信号与选择器。这与UG1399中"This issue is typically caused by arrays"一致。

**待补的直接证据**：用一次16×16 CSynth的RTL层次/端口宽度报告确认上述端口规模的量级。

已排除的做法（都试过且无效）：
- 在`runPeArray`内部按行bank分组（`ecdc987`）——4×4下CSim直接功能失败（输出全0），因为独立bank函数用一维数组形参；改为行基址分组后4×4恢复（`c6a222a`）。
- 给`runPeArray`声明多周期流水间隔（`PIPELINE II=5`，`fd7cd12`）——16×16仍报同样的`204-65`；且固定5拍会让4×4回退（顶层7.434ns、QK/ROW_SUM II退化到10、PWL/PV退化到5），故必须按规模自适应。
- 用固定规模一维"节点数组"让整个`D×D`阵列不跨函数边界（`cf11987`）——4×4通过并冻结，但16×16仍报`204-65`。
- **bank循环带`#pragma HLS UNROLL`**——这是关键教训：bank循环被展开后，所有bank又合成同一个巨型单拍体，等于没有拆分。此前所有"bank分组"尝试都犯了这个错。

### 15.2 架构调研结论（文献调研，未改代码）

设计律：**并行的维度应当是互相独立的query行；归约的维度应当是同一行内的key/head元素。** 方阵把"query行"和"key列"都放到物理维度上，导致row-max/row-sum必须跨物理PE边界，控制流与数据依赖交织。

- 文献直接支持：SWAT(DAC'24)批评SALO的方阵——"the systolic array's square structure requires square tiling ... This tiling is suboptimal for row-wise SoftMax operations"（[SWAT](https://ar5iv.labs.arxiv.org/html/2405.17025)）。
- 推荐方向（置信度medium）：把`D×D`方阵改成**`D×P`矩形阵列，P沿head维铺开**，归约移到**行尾小归约树**；每个PE退化为"1个fp16操作数寄存器 + 1个fp32累加器 + 纯MAC内循环"，无分支、无跨PE依赖。`D=16,P=16`仍是256 PE，head=16时一周期出完整点积，head=128时变成`HD/P=8`次串行累加进同一累加器，**开销不随D平方增长**。
- 阵列尺寸可由外部位宽反推的官方经验：Vitis BLAS L2 GEMM"the size is set according the external memory datawidth"，512-bit接口对应16×16（[AMD Vitis BLAS](https://xilinx.github.io/Vitis_Libraries/blas/2022.1/user_guide/L2/L2_gemm_content.html)）。
- 单阵列复用有先例：SALO在同一32×32阵列内跑完QK→exp→行求和→归一化→PV，每PE只有1个MAC+1个累加寄存器，QK用output-stationary、PV用weight-stationary（[SALO](https://ar5iv.labs.arxiv.org/html/2206.14550)）。
- 不建议把split-K/flash-decoding作为主要手段：它解决GPU上SM填不满的并行度问题，单FPGA单阵列动机不成立，且引入partial `(m,l,O)`合并与浮点累加顺序不确定。
- 另一条与本项目生产基线一致的思路：回到**systolic/streaming**形态（`fsa_stream`的R×C阵列+C个CMP+单C-lane Accumulator已通过完整CoSim），让数据按拍流过阵列，而不是"单拍调度整个阵列"。

### 15.3 下一步的最低成本验证（先C综合，7.300ns不变）

**按修正后的根因，优先级改为：**

- **B（先做，改动最小）**：让`runPeArray`里的**bank循环真正成为流水迭代维度**——即去掉bank循环上的`#pragma HLS UNROLL`，使每拍只调度一个bank。这与已失败的"行bank分组"有本质区别：之前bank循环仍被`UNROLL`，16个bank又合成同一个巨型单拍体。D=16时若`PE_BANK_ROWS=1`则每拍1个bank、bank内16个PE；需检查RTL实例数确认工具没有把bank函数outline成多份（那会违反单阵列约束）。
- **A（medium-high，与B可叠加）**：把`runPeArray`改成"每PE每拍做固定小事的紧凑循环流水"——流水体内只有读数组 + 1次fp16×fp16→fp32 MAC + 写数组，无分支、无内层循环，PE状态仍是唯一那份。注意QK要II≤5则每拍MAC数需≥52（256/5），因此风险回到**时序**：7.300ns由256 PE的门级深度与布线决定，不会因为改结构自动变松。
- **C（medium，必须叠加A/B）**：`D×P`矩形阵列 + 行尾归约树（见15.2）。单独做矩形化不能保证消除204-65；SALO是ASIC实现，HLS可流水性无证据。
- **E（退路，高置信）**：退回`fsa_stream`的streaming/systolic形态（已完整CoSim通过，且与官方库同形），代价是放弃"每PE一个`score_acc`"的结构。

**消融序列（定位"锅在数组规模还是在softmax"）：** V0单PE纯MAC → V1只有QK的阵列（无softmax）→ V2加行尾row-max树 → V3加online rescale + exp2 → V4加PV复用同阵列 → V5全kernel+DMA。V1与V3的对比是关键判据。另需从综合报告的层次/实例数确认阵列没有被隐式复制。

风险提示：PV要求V以转置顺序流入，可能BRAM端口爆炸；QK需把`K[j][d*P+p]`广播给整行，实质需要P个读端口；fp32累加路径能否打7.300ns只能靠综合报告确认。

**官方证据链（HLS侧调研确认）：** UG902 Code Style指出"When a loop or function is pipelined, ... unrolls all loops in the hierarchy below"，且"If there is a loop with variable bounds in this hierarchy, it prevents pipelining"；UG1399中"This issue is typically caused by arrays"。公开的≥256 PE Vitis HLS 2D阵列实现**没有**找到先例（spcl/gemm_hls用1D PE链+`Stream<>`+DATAFLOW，ViT-Accelerator用逐拍位移的16×16，AutoSA用tiling factor+strip-mine）。

未获取的证据：`204-65`无官方条目；`hls-guidance/200-880`与AMD社区帖正文均只拿到标题；FARE（ACM/SIGDA FPGA 2026，DOI 10.1145/3748173.3779572）全文因403未读到，未对其做任何事实陈述。
