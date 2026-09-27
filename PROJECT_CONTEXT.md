# FSA HLS Project Context

> 本文件只保留恢复任务所需的高信号状态。后续工作开始时先读取，并在关键结果或决策变化后原地更新。

## 1 目标与当前优先级

- 生产基线：在Vitis HLS中实现与Chisel FSA语义一致的完整attention核；单次顶层调用完成一次attention，核内DMA搬运Q/K/V/O。
- 当前研究任务：独立实现参数化`D×D` Split-D顶层`fsa_stream_split_d`。
- 默认配置：物理阵列`4×4`、`dim=16`；目标配置仅改参数得到`16×16/dim128`。
- 算法顺序：每个PE含FP16工作寄存器`reg`和FP32 `score_acc`；先执行`dim/D`轮QK累加，完整S后softmax，再执行`dim/D`轮PV。
- 当前第一优先级：停止继续堆叠`PIPELINE off`，改为显式单PE执行入口；在真正只有一套`D×D` PE阵列和一套每列Accumulator的前提下恢复可流水阶段的吞吐。

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

**最近一次已读取build：**`build/fsa_stream_split_d_build/solution1`，产物时间2026-09-28 02:14--02:16；综合输入已确认包含ROW_SUM及PV的4条`#pragma HLS PIPELINE off`。

- CSim、CSynth通过；未运行RTL CoSim、IP导出、Vivado实现或板测。
- 测试覆盖`L=7` causal/non-causal、`L=1`、`L=16` causal/non-causal及非法长度哨兵。
- 顶层估算周期7.300ns，等于7.300ns有效预算，HLS裕量为0。
- 顶层资源：BRAM8、DSP56、FF38050、LUT183272、URAM0。
- `runAccumulatorColumns`已收敛为唯一实例：6拍、II1、8 DSP，即4列各一个2-DSP FP32 RawFMA。
- ROW_SUM的`PIPELINE off`生效：循环变为非流水outline并调用外部共享`runPeArray`，自身DSP为0。
- PE结构仍未最终通过：Vitis把PV循环单独outline后，在PV子模块内部生成一套16-DSP `runPeArray`；key-tile主模块另有一套16-DSP `runPeArray`。全设计仍有2套`4×4` PE阵列，共32 DSP。
- 另有8个FP32减法器占16 DSP，合计顶层56 DSP。
- QK特征循环仍为II4；ROW_SUM及PV现在顺序执行但仍需解决跨outline资源共享。

**当前源码修改：**

- 已删除Split-D实现中的全部`PIPELINE off`和`runPeArray ALLOCATION`试验。
- 新增唯一常驻`hls::task` actor `peArrayTask`；整个Split-D源码只有该actor中的一个`peMacUnit`调用点，内部完全展开`D×D`并声明II1。
- 首次综合该task候选时，Vitis已确认PE内部按4×4及RawFMA内部24轮完全展开，但在pre-synthesis以`HLS 214-389`失败：`hls::task`不在显式DATAFLOW区域。现已在顶层`run()`补充`#pragma HLS DATAFLOW`；修复后的Vitis build尚未运行。
- QK和ROW_SUM通过命令/结果流阻塞使用PE，并显式接受II4真实反馈；不使用虚假的`DEPENDENCE false`。
- PWL一次连续发射8个分段再顺序收回，命中结果直接写回PE.reg，删除了阵列外`D×D` probability副本。
- PV每组交错4个独立feature：每个feature内部仍按key row原顺序累加，只增加`4×D`个FP32临时partial，不改变FMA顺序；命令和结果循环目标II1。
- 命令/结果stream深度均为8，足以容纳完整PWL批次及4路PV批次；这是静态task的一进一出协议，不是此前已失败的Accumulator DATAFLOW反馈环。
- `4×4/dim16`和`16×16/dim128`均已通过全部相关源文件的C++14语法检查，也通过带`__VITIS_HLS__`/`__HLS_COSIM__`/`__HLS_CSIM__`宏的入口语法检查。
- 当前Windows仍缺少Xilinx浮点仿真链接库，普通本地可执行文件无法链接；数值、task并发、结构、II、资源及时序均需下一次Vitis CSim/CSynth验证。

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
| Accumulator请求/响应反馈DATAFLOW | RTL CoSim死锁/Bad TV | 不恢复反馈环 |
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

1. 用户在服务器重新运行`./run_hls.sh fsa_stream_split_d`，先确认`HLS 214-389`消失，并确认Vitis 2024.2接受DATAFLOW区域中的`hls::task`及聚合命令/结果stream。
2. CSynth必须确认`peArrayTask`只有一个实例、内部只有16个PE RawFMA/DSP；全设计目标约40 DSP，但stream引入的FF/BRAM/LUT变化必须以新报告为准。
3. 检查QK与ROW_SUM实际II是否为4、`peArrayTask`是否II1，并检查PV命令发射/结果回收循环是否II1及每tile总延迟是否优于`PIPELINE off`版本。
4. 同时核对唯一8-DSP Accumulator、四AXI接口、数值结果和7.300ns有效时序预算；如果CSim或综合因task/宽stream失败，保留同一命令协议，改为显式控制驱动DATAFLOW actor并重新验证死锁。
5. 结构和CSynth通过后再运行RTL CoSim；IP导出、Vivado实现和板测仍不在当前阶段。

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

### 未完成

- 当前单task候选的Vitis CSim/CSynth、单PE阵列、PV吞吐及总延迟验收。
- Split-D RTL CoSim、IP导出、Vivado实现和板测。
- 生产基线IP导出、Vivado实现后时序和板测。

## 10 环境与复现

- 本地：Windows PowerShell；无Vitis/Vivado，只做C++检查。
- 服务器：Vitis HLS 2024.2，器件`xcvu37p_CIV-fsvh2892-2-e`。
- 时钟：10.0ns，uncertainty 2.7ns；不得放宽。
- Split-D服务器命令：`./run_hls.sh fsa_stream_split_d`。
- 生产基线命令：`./run_hls.sh fsa_stream`。
- 不使用Git标识当前状态；旧`source_manifest.json`和`delivery_validation.json`是历史快照，不代表当前源码。

## 11 交接摘要

当前任务是把Split-D做成真正的一套参数化`D×D`阵列：默认4×4处理16维，未来16×16处理128维。旧02:16 build为2套PE、DSP56且依赖`PIPELINE off`，已经判定不是最终结构。当前源码改为唯一常驻`hls::task` PE actor：只有一个`peMacUnit`调用点，QK/ROW_SUM接受II4，PWL批量II1，PV按4个独立feature交错发射并保持原FMA次序；全部`PIPELINE off`已删除。首次Vitis csynth因task不在DATAFLOW区域而失败，现已在`run()`补齐`#pragma HLS DATAFLOW`并通过两种参数的本地语法检查。下一步重新运行`./run_hls.sh fsa_stream_split_d`，优先验证CSim、task/stream综合、唯一16-DSP PE阵列、PV II1、约40 DSP目标和7.300ns有效时序。
