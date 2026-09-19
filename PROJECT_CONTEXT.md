# FSA HLS Project Context

> 本文件是 `FSA_HLS` 的持久任务状态。后续工作开始时先完整读取，并在关键决策、代码阶段或新验证结果后原地更新。

## 1. Mission

- 最终目标：在 Vitis HLS 中实现与 Chisel FSA 架构和时序语义一致的完整 attention 核，并在不复制计算阵列的前提下降低延迟、提高吞吐。
- 顶层一次调用完成一次完整 attention；例如 `L=9, D=4` 的 Q/K/V 只能调用一次 `fsa_stream`。
- 顶层接收 Q/K/V/O 地址、序列长度和 causal 模式，核内 DMA 自动搬运。
- 当前最高优先级：在保持单套 SA 和原 FSA 数据流的条件下降低 KV tile 间隔与 PE 多周期流水带来的放大延迟。

## 2. Hard Constraints

- Chisel `FSA-main/src/main/scala/fsa/` 是功能、结构和时序语义的主要参考，不修改该目录。
- 只维护新路径：`src/stream/`、`include/fsa/stream/`、`tests/stream/`、`hls/fsa_stream/`；PE latency第一阶段允许使用独立验证目录 `hls/pe_raw_fma/`，但在验收前不得接入正式SA。
- 顶层固定为 `fsa_stream`；不改器件、100 MHz 时钟约束、2.7 ns uncertainty、AXI 位宽或顶层协议。
- `SA_ROWS`、`SA_COLS` 必须参数化，不能写死 4x4。
- 只有一套 `R x C` SA；QK 与 PV 顺序复用；每个 PE 只有一个 MacUnit 和一个元素精度 `PE.reg`。
- 每列一个 CMP；每列一个 Accumulator MAC/exp2/reciprocal 通道。不得用复制 SA、PE 算术阵列或 Accumulator 向量换吞吐。
- S/P 继续驻留在 `PE.reg`；不得新增完整 score/probability 数组。
- 保持 Scratchpad/AccRAM 的 bank/sub-bank、64-bit 物理字、full/narrow 访问与单写端口语义。
- 保持 InputDelayer/OutputDelayer、CMP causal counter、在线 softmax、PWL 段顺序和 `PROP_MAX_DIFF -> EXP_S1 -> EXP_S2 -> L/O` 顺序。
- 不使用 `accumulator_pipeline` 架构。
- 不进行 Git 操作。按当前AGENTS.md，用户要求实现/修复时可修改并运行相称的本地C++验证；Vitis由用户在服务器手动执行。

## 3. Durable User Requirements

- 工作区：`C:\Users\30130\Desktop\workstation\FlashAttention`
- 仓库：`C:\Users\30130\Desktop\workstation\FlashAttention\FSA_HLS`
- 用户提供的开发分支：`perf/fsa_streaming_v3`（未用 Git 重新核实）。
- 顶层头文件：`include/fsa/stream/fsa_stream.hpp`
- 顶层实现：`src/stream/fsa_stream.cpp`
- HLS 入口：`hls/fsa_stream/run_hls.tcl`
- 服务器命令：`./run_hls.sh fsa_stream`
- 需要长期自动维护本文件，位置固定为 `FSA_HLS/PROJECT_CONTEXT.md`。
- 没有读取对应新构建前，不得声称当前源码的 DSP、BRAM、II、时序、CoSim 或死锁已经解决。
- 未经明确要求，不修改 `docs/fsa_streaming_v2综合报告.md` 或 `docs/fsa_stream综合报告.md`。

## 4. Current State

### Latest server build（PE边界内联，RTL已通过，时序未通过）

- 构建目录：`build/fsa_stream_build/solution1`；综合产物时间2026-09-19 23:17、CoSim产物时间23:21，确认对应坐标PE边界内联源码。
- Vitis CSim、C综合和Verilog RTL CoSim全部通过；C post-check通过，无死锁、无数值错误。9x4 non-causal/causal分别为1778/1310 cycles，非法长度55 cycles，三事务总执行3123 cycles；相对2026-09-17 RTL基线2569/1828分别减少30.79%/28.34%，总执行减少29.53%。
- SA内部循环trip count=133、iteration latency=9、achieved II=1；TileTick latency 142、interval 134。相对23:25候选276/268分别减少134拍（48.55%/50.00%），相对已验收hop16基线237/222分别减少95/88拍（40.08%/39.64%）。`HLS 200-880`已消失。
- 内联后原始PE结果在ST9写回，消除了此前ST11写与distance8消费者ST1/ST2读的冲突。`systolicArrayProcess` latency/interval均为147，顶层DATAFLOW最小latency/interval为232/148。
- Tile/SA仍使用DSP24：报告中有16条独立11x11 PE乘法通路，每条DSP1；4个CMP均latency=3、II=1、DSP2，共8 DSP。没有增加PE/CMP硬件实现数量。
- 顶层资源BRAM18K 20、DSP44、FF90352、LUT142213、URAM0。相对23:25 build，BRAM/DSP不变、FF +2775（3.17%）、LUT -1828（1.27%）；DMA/SRAM/Delayer/Accumulator均未见II或资源结构退化。
- Q/K/V DMA读循环、Input/Output Delayer和Scratchpad关键循环均II=1；Accumulator arithmetic latency=10、II=1、DSP20，第一阶段DMA成果保持。
- 关键失败是时序：估算周期9.608 ns，超过10.0-2.7=7.300 ns有效预算2.308 ns并触发`HLS 200-871`。关键路径已进入内联的Raw FMA位域处理和11x11乘法链。按原始10 ns时钟换算的104.08 MHz不能视为当前约束通过。

### Latest RTL-verified baseline（第二阶段修改前基线）

- **注意：本节build已验证第一阶段DMA，但早于当前tile流水源码。** 构建目录：`build/fsa_stream_build/solution1`；产物时间2026-09-17 14:01--14:09。可作为第二阶段的直接基线，不能用于证明当前tile源码的II、层次、资源或RTL正确性。
- Vitis HLS 2024.2；器件 `xcvu37p_CIV-fsvh2892-2-e`；目标 10.0 ns，uncertainty 2.7 ns。
- CSim 通过：一次顶层调用完成 9x4 causal 与 non-causal attention。
- Verilog/xsim CoSim 通过，无死锁：
  - 9x4 non-causal：2569 cycles，transaction interval 2559；
  - 9x4 causal：1828 cycles，transaction interval 1818；
  - 非法长度：55 cycles；
  - 三事务总执行：4432 cycles。
- C post-check通过，`pe_register inter false`在当前 `%16` slot表达下未造成RTL数值错误；DMA DATAFLOW死锁也未复现。
- 顶层估算周期 7.300 ns，恰好等于扣除2.7 ns uncertainty后的7.300 ns有效预算；本次无 `HLS 200-871`，但HLS裕量为0，仍无Vivado实现后时序证明。
- 顶层资源：BRAM18K 20、DSP 108、FF 88661、LUT 108077、URAM 0。相对第一阶段修改前，BRAM/DSP不变，FF增加14304（+19.24%），LUT减少1014（-0.93%）；FF增长全部来自三个DMA读流水模块。
- `spatialSystolicArrayTileTick`：latency 237、interval 222；主循环221次，iteration latency=16，最终II=1。
- 相对13:15移植前基线，non-causal/causal分别加速7.14x/6.75x，三事务总周期降低85.57%；相对9月9日2737/1954 cycles基线也分别快6.14%/6.45%。
- PE/CMP自身均为II=1，latency分别为9/3；Accumulator arithmetic latency=10、II=1；SA tile仍为237/222，Scratchpad和Delayer未退化。Q/K/V DMA扁平读循环均达到目标II=1（修改前为II=4）。
- 层次仍是单4x4 SA：16 PE + 4 CMP；单 `systolicArrayProcess`、单 `spatialSystolicArrayTileTick`、单 `accumulatorProcess`、单 `accumulatorArithmeticVector`，Accumulator为4个算术lane。
- Accumulator中没有FP32 `fsub`实例；每lane仅见一组共享算术路径的FP32 `fadd`/`fmul`。`accumulatorArithmeticVector` latency=10、II=1。
- 没有 IP export、Vivado implementation 或板级验证结果。

### Current source

- PE latency优化第二阶段已把正式 `peMacUnit` 接到阶段一验收的混合精度Raw FMA；普通MAC与exp2仍共用唯一11x11尾数乘法通路。最新build确认内联后的坐标特化`spatialPeCell`为5拍、II=1、DSP1，不复制单PE算术通路。
- `PE_TOKEN_LATENCY=5`，`PE_SCHEDULER_GUARD_CYCLES=3`，因此 `PE_HOP_CYCLES=8`；内联后SA主循环达到II=1，2/4/5/7数据流改造、DMA三请求actor和安全依赖约束均保留。
- 当前显式建模 `CMP_TOKEN_LATENCY=3`，并保留 Chisel CMP->PE 的一级 Pipe，因此 `CMP_HOP_CYCLES=4`。
- 4x4、PWL=8 时，当前hop=8源码 `SA_TILE_CYCLES=133`；最新Vitis确认trip count=133、iteration latency=9、主循环II=1，TileTick latency/interval为142/134。
- PE slot继续使用commit `8b7aab7bb669a1b781bffb9b67a30897c45b20a7`的 `cycle%PE_HOP_CYCLES`表达；hop=8时是静态低3位bank选择，不使用显式回绕PE计数器。CMP的4槽计数器保持不变。
- 当前仅恢复该commit已有的 `#pragma HLS DEPENDENCE variable=pe_register inter false`。微程序保证SCALE/PWL/ROW_SUM/PV的读发生在对应写回后；11:49也已证明单独移除这条pragma不会改变48错集合。
- `pe_pipeline`/`cmp_pipeline` 不恢复全局 `inter false`；其多拍子函数提交顺序必须保留。此前对这两者的覆盖已被RTL证明会造成48个数值错误。
- 新增4槽 `CmpToPeStage` 环形通道：前三拍容纳当前HLS CMP输出流水，最后一拍对应Chisel `pipe_no_reset(cmp.io.d_output)`；UPDATE score回流以及PROP_MAX/PWL/ROW_SUM经过该通道，SCALE/PV仍直接注入。
- `spatialCmpOutputCell` 保持独立的 II=1、latency=3 流水函数，避免把CMP与PE组合串联；微程序已为score回流、SUB_MAX依赖、PWL和ROW_SUM/PV入口重新对齐。
- 每个 KV tile 已恢复 Q/K/V 三个 InputDelayer phase；Q 从原 Scratchpad Q buffer 重放，每个 KV tile 都重新执行逻辑 `LOAD_STATIONARY`。
- SA已从周期循环第0拍直接消费带phase/tag的Delayer beat：Q直接写唯一 `PE.reg`，不再形成 `q_tile`；K phase结束即启动QK，V phase与QK发射重叠。当前采用8拍hop的tagged wave，K/V仍保留跨hop operand cache，尚不是Scala逐PE mesh的逐线直连。
- OutputDelayer保留为独立actor，但当前tagged SA输出本已按列对齐，因此不再将每个完整token重新串行化成 `SA_COLS` 拍；循环目标为token II=1。
- Scratchpad仍是唯一banked SRAM owner；Q以显式last完成，K/V由同一个II=1循环公平仲裁并以last分别完成。当前tile的行被推入FIFO后，owner即可在SA计算期间装入下一双缓冲区。
- DMA保留统一内部 `DmaReadRequest` descriptor以及每个写流的 `request_id/kind/transfer_last` 完成语义，但请求由Q/K/V三个单输出actor独立产生，消除了跨通道描述符FIFO的反压耦合；顶层仍保留Q/K/V/O四个专用AXI bundle，O写outstanding为8。
- `systolicArrayProcess`已恢复无PIPELINE的query/key顺序嵌套循环，删除被Vitis忽略的`ALLOCATION` pragma；最新build确认不再生成压平控制的两个30位乘法器/6 DSP。
- 当前候选进一步分离控制与算术结果：`PeWave`只保留控制和非归约操作的旁路partial；无默认初始化的`PeResult`环保存FMA原始输出。QK从下一行、ROW_SUM/PV从上一行的结果槽直接选累加输入；所有旧值在任何PE结果写回之前先捕获。FMA后不再通过reduce/valid旁路mux写回partial。hop=8、单坐标PE调用和真实依赖均保留，没有新增反馈DATAFLOW FIFO。
- 23:21 RTL检查点已确认坐标PE边界内联版本无死锁、无数值错误；完全展开的ROW/COL模板仍对应16条11x11乘法通路，结果由ST11提前至ST9、SA达到II1，硬件数不增加。
- 当前源码已在该RTL检查点之后做单变量时序修改：`prepareExp2`不再把精确小数部分重新规格化/打包成FP16后交给FMA再次解包，而是把等价的`significand * 2^lsb_exponent`直接送入`addFiniteProduct`。普通MAC、FMA舍入、PWL piece、缩放、hop=8、调用点和唯一11x11乘法表达均不变；目标是切断当前exp2输入选择到DSP乘法的9.608 ns关键路径，不增加流水拍。

### Main issue

- 当前 `%8` Raw FMA内联构建已把SA主循环从II=2恢复到II=1，并通过RTL CoSim。
- 第一阶段DMA已经验收：Q/K/V均为II=1，CSim/RTL CoSim通过，端到端周期和计算/存储部件未退化；代价是三个DMA读模块共增加14304 FF，仍仅占器件3.40%。
- 撤销完整tile循环的PIPELINE/II约束后，编译规模问题已解决；内联把原始结果环写回从ST11提前到ST9，解决了distance8递归并达到II1。
- 当前唯一已知阻塞项是时序：已验证build仍为9.608 ns，超过7.300 ns有效预算。源码中的直接小数域时序候选已通过本地功能回归，但尚无新Vitis II、latency、资源或时序结果；第二阶段未验收，不进入第三阶段。

## 5. Architecture / Mental Model

```text
Q/K/V AXI DMA
  -> 64-bit narrow write
  -> banked Scratchpad SRAM
  -> full-row read
  -> InputDelayer
  -> single R x C SA + C CMP
       QK -> max/diff -> scale -> 8-piece exp2 -> row sum -> PV
  -> OutputDelayer
  -> C-lane Accumulator + banked AccRAM
  -> normalize/write O
  -> AXI DMA
```

- 当前 tile 映射：head dimension `d=R`，query block `Br=C`，key/value block `Bc=R`。
- non-causal 的 tile 对数量为 `ceil(L/C)^2`；causal 为 `1+...+ceil(L/C)`。
- Chisel 4x4、PWL=8 的独立指令静态周期：LOAD 5、SCORE 28、VALUE 12、RECIPROCAL 16、NORM 5。
- 当前Raw FMA HLS PE cell为latency=5、II=1；KV瓶颈来自hop=8环形结果槽的跨迭代提交/读取回路，不是单个PE的启动间隔。
- 4x4、PWL=8、3拍CMP流水加一级CMP->PE寄存器时，当前guard=3、hop=8源码 `SA_TILE_CYCLES=133`；已包含输入消费，不再额外等待完整Q/K/V tile。

## 6. Important Files

| File | Purpose | Important notes |
|---|---|---|
| `AGENTS.md` | 仓库约束 | 开始修改前完整读取 |
| `docs/FSA硬件与时序约束及参考代码分析.md` | 架构与逐周期硬约束 | 所有优化的首要判据 |
| `include/fsa/stream/common.hpp` | actor 协议与 SA 周期常量 | 当前声明latency=5、guard=3、hop=8；最新Vitis实测SA II=1 |
| `src/stream/controller.cpp` | tile 控制和 SA 微程序 | causal、phase 顺序来源 |
| `src/stream/scratchpad.cpp` | 64-bit banked SRAM 与Q/K/V重放 | Q现在每个KV tile重放 |
| `src/stream/input_delayer.cpp` | Q/K/V阶梯延迟 | 当前每tile固定3 phase |
| `src/stream/systolic_array.cpp` | 单SA、PE/CMP与环形token流水 | cycle%8 slot；内联坐标PE边界已把结果写回由ST11提前到ST9并达到II1 |
| `src/stream/accumulator_process.cpp` | 单actor微指令 Accumulator | 避免反馈环死锁 |
| `src/stream/arithmetic.cpp` | PE/Acc FMA、PWL、reciprocal | PE latency优化入口 |
| `include/fsa/stream/pe_raw_fma.hpp`、`src/stream/pe_raw_fma*.cpp` | PE Raw FMA与独立HLS验证顶层 | 已接入正式PE；当前源码直接传递exp2精确小数域以缩短关键路径，待Vitis复核 |
| `hls/pe_raw_fma/run_hls.tcl` | 独立Raw FMA综合入口 | `./run_hls.sh pe_raw_fma`，默认CSim+CSynth，不跑CoSim/IP导出 |
| `src/stream/dataflow.cpp` | 顶层actor连接 | 检查生产/消费与死锁 |
| `hls/fsa_stream/run_hls.tcl` | HLS入口 | `set_top fsa_stream` |
| `build/fsa_stream_build/solution1/` | 最新服务器构建 | 23:21 PE边界内联RTL检查点，CSim/综合/CoSim通过、SA II=1、9.608 ns；早于当前时序候选源码 |
| `docs/fsa_stream综合报告.md` | 当前综合报告 | 已更新为15:44 build，并加入fsa_dma与FSA-main两组对比 |

## 7. Decisions

### D001 — 保持单套 FSA 计算结构

**Decision:** 只优化调度、流水重定时和数据搬运重叠，不复制 SA/PE/Accumulator。

**Reason:** 用户要求以 Chisel FSA 为优化目标，不能偏离原实现。

**Evidence/Result:** 约束文档规定 `R x C` PE、`C` CMP、`C` Accumulator lane，QK/PV顺序复用。

**Implication:** 多query硬件上下文、归约树替代SA、第二套FMA均不采用。

**Status:** Active

### D002 — Accumulator保持单actor

**Decision:** alpha、exp2、L/O更新、reciprocal和归一化在一个微指令循环中复用唯一 `accumulatorArithmeticVector`。

**Reason:** 请求/响应stream反馈环曾在RTL CoSim形成死锁。

**Evidence/Result:** 删除反馈环后旧基线 CSim/CoSim 通过，源码只有一个向量算术调用点。

**Implication:** 不重新拆成互相反馈的 DATAFLOW actor。

**Status:** Active

### D003 — 当前尝试将PE hop降至10

**Decision:** 保留9拍PE流水，仅将调度guard从7降至1，并用显式环形计数器避免 `%10` 余数网络。

**Reason:** guard不是Chisel结构要求；理论 tile 微程序可由203降至137拍。

**Evidence/Result:** 2026-09-14 build确认trip count=137，但 `pe_pipeline.partial` 循环携带依赖令最终II=12、tile=1651、时钟13.883 ns，优化失败。

**Implication:** 下一步必须先验证主循环II、时序和CoSim；失败时不得继续叠加其他优化。

**Status:** Rejected in current implementation

### D004 — 严格恢复每KV tile的Q装载语义

**Decision:** 每个KV tile都从Scratchpad重放Q并经过InputDelayer，执行Q/K/V三阶段。

**Reason:** Chisel Python kernel在每个KV block前调用 `LOAD_STATIONARY`；Score会用S/P覆盖 `PE.reg`。

**Evidence/Result:** 当前源码生产/消费数量已静态对齐，2026-09-14 CSim及RTL CoSim通过。

**Implication:** 旧基线中的“每query tile只送一次Q”优化不再是当前状态。

**Status:** Active, confirmed functional

### D005 — 恢复Chisel CMP到PE寄存器并显式解除错误slot依赖

**Decision:** 用4槽 `CmpToPeStage` 环形通道表达3拍HLS CMP流水加一级Chisel CMP->PE寄存器；保留 `pe_register`、`pe_pipeline`、`cmp_pipeline` 的真实HLS跨迭代依赖，不再使用全局 `inter false`。

**Reason:** Chisel `SystolicArray.scala` 明确使用 `pipe_no_reset(cmp.io.d_output)`；旧HLS build遗漏该边界并错误推断相邻迭代slot依赖，分别造成13.883 ns组合路径和II=12。

**Evidence/Result:** CMP/PE代码已完成；D006并入输入消费后4x4总微程序为155拍。撤销全部依赖覆盖后的12:10 build通过RTL CoSim，48个数值错误消失；CMP/PE保持独立II=1、latency=3/9，但外层SA循环因保守slot依赖最终II=9。

**Implication:** 该结构已确认功能正确；顶层估算周期恢复到7.964 ns但仍比7.300 ns有效预算差0.664 ns。下一步同时观察外层动态slot的II=9和该关键路径，不改变单SA结构或恢复全局false。

**Status:** Active, RTL-correct; performance incomplete

### D006 — 优先重构输入/输出/存储/DMA流

**Decision:** 保留单SA和现有多周期PE token调度，依次实现：Delayer数据在SA周期循环内消费、OutputDelayer每拍推进、唯一Scratchpad owner的读写请求调度、DMA内部统一请求/完成语义；顶层四个AXI bundle暂不改变。

**Reason:** 用户明确指定优先处理架构差异2、4、5、7；专用Q/K/V/O物理端口仍有利于当前HBM带宽，先统一内部协议可避免无收益的顶层接口破坏。

**Evidence/Result:** 四项代码均已实现。首次DMA请求化把Q/K/V descriptor放在一个多输出actor中，11:29 RTL CoSim形成跨通道反压死锁；拆成三个单输出请求actor后，12:10 build的CSim和RTL CoSim均通过。OutputDelayer和Scratchpad内部循环II=1，DMA request循环II=1，但Q/K/V DMA read循环为II=4。

**Implication:** 不复制Q/K/V SRAM、不复制SA/FMA；允许保留多周期PE所需的小型operand/token流水状态，但删除“所有Q/K/V完整收集后才启动SA”和“完整结果重新串行化”的事务屏障。

**Status:** Active, RTL verified; DMA read II remains to optimize

### D007 — 单变量回滚PE guard至7拍

**Decision:** 只把 `PE_SCHEDULER_GUARD_CYCLES` 从1恢复为7，使 `PE_HOP_CYCLES=16`；保留当前DMA、Delayer、Scratchpad、CMP通道以及无全局依赖覆盖的正确RTL结构。

**Reason:** 历史hop=16构建的SA主循环达到II=1、顶层估算周期7.300 ns。16拍slot复用距离覆盖9拍PE和外层提交级，并且16为2的幂，可能比hop=10更容易调度和选择；需要用单变量实验区分hop间距与其他重构的影响。

**Evidence/Result:** 13:15 build的CSim与RTL CoSim通过，但SA仍为II=9；tile latency/interval由1401/1400增至1995/1994，9x4由13006/8772增至18354/12335。时序仅由7.964改善到7.840 ns，仍超过7.300 ns有效预算；BRAM/DSP不变，FF/LUT略降。

**Implication:** guard本身不是当前II问题的解法；HLS仍把动态slot复用推断为distance=1。建议恢复guard=1，再转显式前递/静态stage方案。

**Status:** Rejected for performance; functionally valid

### D008 — 移植8b7aab7的PE/SA调度表达

**Decision:** 保留当前全部外围和流式数据通路，只在SA中恢复commit `8b7aab7bb669a1b781bffb9b67a30897c45b20a7`的两项关键表达：PE slot使用 `cycle%16`，并恢复仅针对 `pe_register` 的 `inter false`调度提示；不恢复 `pe_pipeline/cmp_pipeline` 覆盖。

**Reason:** 当前PE算术与该commit完全一致；commit相对其父版本取得II=1时，关键变化是固定hop16后用 `%16`代替显式回绕变量。当前13:15 build的II=9正由显式回绕后的动态slot被误判distance=1造成。PE.reg提示在该commit中存在，且11:49实验已证明它不是48错的充分根因。

**Evidence/Result:** 仅修改 `src/stream/systolic_array.cpp`；`common.hpp`已处于guard=7/hop=16。Scratchpad/SRAM、DMA、Input/Output Delayer、Accumulator、控制器和顶层均未修改。15:44 build确认主循环II=1、tile latency/interval=237/222，9x4 CSim与RTL CoSim通过且无死锁、无数值错误。

**Implication:** 该调度表达可保留；禁止恢复已造成48错的 `pe_pipeline/cmp_pipeline inter false`。下一性能瓶颈转为DMA read II=4，时序还需实现后验证。

**Status:** Accepted by CSim, C synthesis and RTL CoSim; implementation timing unverified

### D009 — 性能优化顺序：先降低周期数，再逐级提高频率（待用户确认）

**Decision:** 建议先把DMA循环由实际II=4优化到II=1，并在单SA、单PE状态约束下减少跨tile启动间隔；不把字面上的tile II=1作为目标。架构稳定且100 MHz获得正时序余量后，再按125 MHz到150 MHz逐级验证，200 MHz作为高风险延伸目标。

**Reason:** 当前顶层估算周期7.300 ns已用尽100 MHz目标下的有效HLS时序预算。直接修改时钟约束不能自动获得1.5倍或2倍性能，且150/200 MHz会要求多个模块重新流水化。降低当前tile interval=222带来的潜在收益高于单纯升频。

**Evidence/Result:** FSA-main源码没有固定FPGA时钟；公开论文的1.5 GHz是16 nm ASIC综合条件，仓库U55C示例只公布周期数而没有板上频率，因此不能直接将其周期数换算成时间或与当前VU37P HLS频率等同比较。

**Implication:** 后续若选择该路线，应分别记录DMA II、tile interval、CoSim总周期和实现后WNS，避免仅以HLS时钟约束判断性能。

**Status:** Accepted as three gated stages: DMA, tile pipeline, then timing/frequency

### D010 — 第一阶段用单beat扁平循环消除DMA II=4

**Decision:** 只修改 `src/stream/dma_process.cpp` 的Q/K/V读循环，把 `lane x sub-bank word` 两层循环改为一个 `transfer_word` 循环，每次迭代恰好发出一个AXI读并写一个Scratchpad packet；保持请求actor、四个AXI bundle、Scratchpad协议、SA和时钟不变。

**Reason:** 15:44综合把外层query/key tile循环流水化，并将4x4配置的lane循环完全展开，导致同一AXI口每次流水迭代出现4次读请求，目标II=1实际只能达到II=4。扁平化后流水体只有一次端口访问，符合AXI适配器II=1能力。

**Evidence/Result:** 2026-09-17 14:09 build确认Q/K/V DMA读循环均达到II=1；CSim/RTL CoSim通过，9x4端到端周期与SA/PE/CMP/Accumulator均未退化。

**Implication:** 第一阶段已验收并固定；第二阶段修改不得退化DMA II或其RTL正确性。

**Status:** Accepted by CSim, C synthesis and RTL CoSim

### D011 — 第二阶段在单SA约束下流水化tile调用

**Decision:** 撤销完整tile循环的全部 `PIPELINE/II` 约束，并恢复无PIPELINE的query/key嵌套循环顺序调用唯一TileTick；内部仍以TileTick逐拍II=1、PE II=1及hop=8为目标。08:30 build证明`ALLOCATION` pragma被忽略，最新源码已删除该pragma。

**Reason:** 外层II=1与单实例TileTick真实interval矛盾，已触发约100倍IR膨胀；将目标改为 `SA_TILE_CYCLES+1` 后，集成Raw FMA/hop=8的构建仍出现Compile/Link 366333、Unroll/Inline 4283956/3210132条指令。为避免HLS跨函数展开完整固定周期微程序，先回到已知保守的顺序事务语义，再单独验证Raw FMA集成。

**Evidence/Result:** 压平代码以及当前Raw FMA/hop=8代码的4x2、4x4、8x4本地回归全部通过。23:21 build确认内联PE边界后TileTick主循环达到II1、iteration latency9、TileTick 142/134；仍只有16条PE乘法通路和4个CMP，顶层DSP44。Verilog CoSim通过，无死锁或数值错误；代价仍是估算周期9.608 ns并触发时序警告。

**Implication:** 外层编译规模、PE wrapper控制开销和压平控制6 DSP均已关闭；内联把真实结果写回提前至ST9并消除hop8调度冲突。第二阶段的II、周期和RTL正确性目标已达到，当前只修复100 MHz时序；此前不进入第三阶段。

**Status:** II performance and RTL verified; 100 MHz timing pending

### D012 — 算术内部latency按三阶段门控优化

**Decision:** 新优化分为三个必须由用户Vitis验收后才推进的阶段：第一阶段为独立PE手写Raw FMA及其单实例资源/latency/II验证；第二阶段把候选接入SA并使用hop=8完成全链路验证；第三阶段扩展为四个内部串行检查点：3A在hop=8下独立实现并验证同类位域FP32 Raw FMA，3B替换`fsa_stream`中剩余的有效FP32 FMA通路并完成全链路验证，3C把CMP显式简化为单FP32减法流水加现有组合位序max选择并再次完成全链路验证，3D才单变量尝试hop=4。

**Reason:** 把算术单元本身的QoR与SA token调度分开，避免再次把FMA、slot、hop和依赖提示同时修改而无法归因。当前PE候选是FP16×FP16+FP32，不能直接替换Accumulator的FP32×FP32+FP32。CMP不需要FMA：当前有效数据通路只需要`lhs-new_max`的一个FP32减法结果；逐score的`newMax`由`finiteAccMax`直接比较IEEE位序后选择，`accCmp.out_max`在当前`fsa_stream`中没有消费者。每个阶段及第三阶段内部检查点失败时都停下修复，不用后续改动掩盖问题。

**Evidence/Result:** 第一阶段独立 `pe_raw_fma_top` 为latency=5、II=1、DSP1。00:21完整构建确认内联后仍有16条11x11 PE乘法通路，SA主循环II1、TileTick 142/134；结果写回位于ST9。顶层DSP44不变，但估算周期9.608 ns不满足7.300 ns有效预算。

**Implication:** 第一阶段独立算术单元门槛已满足，第二阶段已经把候选接入正式PE并进入hop=8全链路调度修复；必须先完成SA II=1及RTL CoSim验收。第三阶段所谓“全部FMA替换”只覆盖当前`fsa_stream`实际需要乘加的通路：PE沿用混合精度Raw FMA，四个Accumulator lane使用新FP32 Raw FMA。CMP不属于FMA范围，但其源码清理也归入第三阶段独立验收：把`hls::fma(a,1,-b)`改为专用FP32减法接口、移除当前顶层未消费的`out_max`，保留`finiteAccMax`组合位序选择；softmax重标定必须保持`old/local max - new max`的负向差值，不能反转为`new max - old/local max`。未被当前顶层调用的兼容函数不作为硬件实例计数目标。

**Status:** Stage 2 synthesis throughput and RTL achieved; timing acceptance pending

### D013 — 第二阶段先建立RTL检查点，再修复100 MHz时序

**Decision:** 不进入第三阶段。先对当前II1、TileTick 142/134候选运行一次RTL CoSim作为功能检查点；若通过，继续留在第二阶段修复9.608 ns关键路径，直到不超过7.300 ns有效预算，同时保持SA II1、单16 PE/4 CMP和DMA II1。

**Reason:** 当前吞吐改善依赖PE边界内联，而该变化同时把Raw FMA组合链暴露为9.608 ns关键路径。直接叠加下一阶段算术/CMP/hop修改会失去故障归因；但跳过当前CoSim也会让后续无法区分RTL数值错误来自现有内联还是时序重定时。

**Evidence/Result:** 23:21 build已确认II1、硬件数量和RTL正确性；9x4 non-causal/causal为1778/1310 cycles，无死锁或数值错误。时序仍超过预算2.308 ns。历史上综合期调度提示曾出现C++通过而RTL错误48项，因此该检查点作为后续时序修改的比较基线保留。

**Implication:** 当前版本RTL CoSim检查点已完成；下一顺序固定为单变量时序重定时 -> CSim/CSynth检查II、资源和时序 -> RTL CoSim。100 MHz通过前不评估150/200 MHz，也不开始第三阶段FP32 FMA、CMP或hop4修改。

**Status:** Active

## 8. Experiments / Results

### E001 — 历史本地功能基线

**Setup:** 4x2处理5x4、4x4处理9x4、8x4处理9x8；causal/non-causal；Acc PWL整数、中点、边界和混合输入。

**Result:** 用户提供的历史状态为全部通过，`git diff --check`通过。

**Conclusion:** 证明当时版本的C++功能；不覆盖2026-09-09 13:07后的hop=10/Q重放修改。

### E002 — 2026-09-09稳定旧build

**Setup:** 4x4，9x4 causal/non-causal，Vitis HLS 2024.2，100 MHz。

**Result:** CoSim 2737/1954 cycles；tile 222；DSP/BRAM 114/20；无死锁。

**Conclusion:** 当前可用的性能与结构基线，但早于最新源码。

### E003 — 2026-09-14当前源码build

**Setup:** 4x4，hop=10，每KV重放Q，9x4 causal/non-causal，Vitis HLS 2024.2，100 MHz。

**Result:** CSim/CoSim通过且无死锁；CoSim 15540/10466 cycles；tile主循环137次但II=12，tile=1651；估算周期13.883 ns；资源BRAM20/DSP114/FF80808/LUT115319。

**Conclusion:** 功能、单SA结构、KV II=1和64-bit BRAM均成立，但调度优化失败。与旧基线相比，non-causal慢5.68倍、causal慢5.36倍；下一步只处理SA主循环依赖和CMP->PE关键路径。

### E004 — 2026-09-14架构差异2/4/5/7本地回归

**Setup:** 直接消费Delayer、token级OutputDelayer、Scratchpad K/V公平仲裁和DMA descriptor/last协议；运行 `run_stream_test.ps1` 的4x2、4x4、8x4配置。

**Result:** 三组 streaming v2 causal/non-causal attention与Accumulator PWL测试全部通过；分别覆盖5x4、9x4、9x8矩阵。

**Conclusion:** C++功能、参数化和stream生产/消费计数闭合；不能据此声称HLS主循环II=1、OutputDelayer综合II=1、时序达标或资源未增加。

### E005 — 2026-09-14 11:02快速RTL候选

**Setup:** hop=10、4拍CMP通道、2/4/5/7重构以及SA循环依赖覆盖；用户提供CoSim末尾日志，本地没有对应server build报告。

**Result:** xsim正常结束于38995 ns，无死锁；C post-check报告non-causal/causal共48个O元素不匹配，CoSim FAIL。

**Conclusion:** 吞吐很可能已经改善，但RTL功能错误。C++回归通过而RTL失败，与只在综合阶段生效的错误 `pe_register inter false` 高度一致；当前已撤销该pragma。

### E006 — 2026-09-14 11:29 DMA请求死锁及本地修复

**Setup:** 撤销 `pe_register inter false` 后的候选，DMA使用一个 `dmaRequestProcess` 顺序写深度2的Q/K/V request FIFO；用户提供CoSim deadlock报告。

**Result:** 第一个事务0%处形成唯一依赖环：Scratchpad等Q数据，而Q descriptor无法越过已满的V descriptor FIFO。请求生成拆为Q/K/V三个独立单输出actor后，4x2、4x4、8x4本地顶层与PWL回归全部通过。

**Conclusion:** 原死锁是多输出producer造成的结构性head-of-line阻塞，不是Scratchpad公平仲裁或FIFO容量不足。actor拆分从DATAFLOW图中删除了该环；RTL修复仍须服务器CoSim确认。

### E007 — 2026-09-14 11:49无死锁但48个RTL输出错误

**Setup:** Q/K/V请求actor已拆分，`pe_register inter false`已撤销，但 `pe_pipeline`/`cmp_pipeline inter false`仍存在。

**Result:** RTL正常完成、DMA死锁未复现；post-check与11:02完全相同地失败48项，说明撤销 `pe_register` override未修复数值问题。

**Conclusion:** DMA拓扑修复成立；数值问题继续指向剩余的综合期依赖覆盖。当前源码已撤销SA中全部 `DEPENDENCE inter false`，并改进testbench使后续失败打印actual/expected值。

### E008 — 2026-09-14 12:10当前build全部功能通过

**Setup:** Q/K/V请求actor已拆分，SA中 `pe_register`/`pe_pipeline`/`cmp_pipeline` 的全局依赖覆盖全部撤销；读取完整本地同步build、CSim、综合和RTL CoSim报告。

**Result:** CSim与Verilog CoSim均通过，3/3事务正常结束，无死锁、无数值错误。9x4 non-causal/causal为13006/8772 cycles，总执行21813 cycles；SA tile latency/interval为1401/1400，155次主循环最终II=9。顶层估算周期7.964 ns、Fmax 125.56 MHz，但超过7.300 ns有效预算并触发 `HLS 200-871`；资源BRAM20/DSP108/FF68283/LUT120828。

**Conclusion:** 2/4/5/7重构的功能正确性与DMA死锁修复已被RTL确认；相对15540/10466失败性能基线快约16%，但相对2737/1954稳定旧基线仍慢4.75/4.49倍。首要瓶颈是SA动态token slot的保守循环依赖（II=9），次要瓶颈是Q/K/V DMA read循环II=4。

### E009 — 2026-09-14 13:15 guard=7/hop=16单变量实验

**Setup:** 在E008正确源码上只把PE guard从1改为7，使hop 10->16；其他数据流、CMP通道和依赖约束不变。

**Result:** CSim与RTL CoSim继续通过，无死锁或数值错误。SA主循环trip count 155->221，但II保持9，tile latency/interval 1401/1400->1995/1994。9x4 non-causal 13006->18354（+41.1%），causal 8772->12335（+40.6%）。估算周期7.964->7.840 ns，仍超7.300 ns预算；BRAM20、DSP108不变，FF 68283->65286，LUT 120828->114601。

**Conclusion:** 历史hop=16的II=1不是由7拍guard单独带来的；当前显式计数器/动态slot数据通路仍被HLS视为distance=1。回滚只增加微程序长度，端到端性能明显变差，不应作为最终方案。

### E010 — 2026-09-18独立Raw FMA第一阶段验收

**Setup:** Vitis HLS 2024.2，`pe_raw_fma_top`，VU37P目标器件，10.0 ns时钟、2.7 ns uncertainty；运行CSim和CSynth，不运行CoSim/IP导出。

**Result:** CSim 0错误；综合固定latency=5、interval/II=1，估算周期7.133 ns。资源为DSP1、FF1628、LUT5638、BRAM/URAM 0；Bind Op只有一个 `mul_11ns_11ns_22_2_1` DSP乘法实例。唯一警告是ap_none输出缺少valid可能影响自动CoSim，不影响本次CSim/CSynth指标。

**Conclusion:** 第一阶段全部验收门槛满足：latency<=5、II=1、DSP<=5、单乘法路径及100 MHz HLS时序均成立。该结果只覆盖独立候选，正式SA尚未替换，完整RTL正确性和端到端资源/时序必须在第二阶段验证。

### E011 — 2026-09-18逐字段ring提交综合

**Setup:** 保持hop=8、16 PE/4 CMP及全部外围不变；删除`row_result`整结构写回，控制字段提前提交，各PE算术字段直接写当前ring槽。Vitis HLS 2024.2运行CSim和CSynth，不运行CoSim/IP导出。

**Result:** CSim通过；PE仍为5拍/II1/DSP1，SA循环trip count=133、iteration latency=12、achieved II=2，TileTick仍为277/268，`HLS 200-880`仍定位distance=8的`pe_pipeline.partial` store/load。顶层资源20 BRAM/44 DSP/88977 FF/147222 LUT，估算周期7.300 ns；Q/K/V DMA及Delayer/Acc II均未退化。

**Conclusion:** 逐字段提交只减少20 FF和796 LUT，没有改变递归调度；整结构聚合不是II=2根因。下一候选必须结构性分离PE launch/completion与hop延迟，不能继续调整表面赋值形式或恢复全局依赖覆盖。

### E012 — 2026-09-18 23:25控制/原始结果分离综合

**Setup:** 保持hop=8、16 PE/4 CMP和外围不变，把控制/非归约旁路与原始FMA结果分环，归约MAC直接读取相邻行结果。Vitis HLS 2024.2运行CSim和CSynth，未运行CoSim/IP导出。

**Result:** CSim通过；SA循环iteration latency 12->11，但achieved II仍为2，TileTick 277/268->276/268。依赖警告转到`pe_results.out_accType`：ST11写、distance8后的ST1顶行输出读及ST2归约读。顶层20 BRAM/44 DSP/87577 FF/144041 LUT、7.300 ns；16 PE/4 CMP及DMA/SRAM/Delayer/Acc关键II均保持。

**Conclusion:** 控制选择不是剩余II瓶颈；候选节省1400 FF/3181 LUT并缩短1拍尾延迟，但不改善interval。后续必须重定时真实结果生产/消费，不能用虚假依赖覆盖。

### E013 — 2026-09-19 23:21 PE边界内联RTL检查点

**Setup:** hop=8、坐标PE边界内联、16条PE乘法通路、4 CMP及第一阶段DMA II1保持；Vitis HLS 2024.2运行CSim、CSynth和Verilog CoSim。

**Result:** CSim/C综合/RTL CoSim全部通过，C post-check无错误且无死锁。SA主循环II1、iteration latency9、TileTick 142/134；9x4 non-causal/causal为1778/1310 cycles，总执行3123 cycles。顶层20 BRAM/44 DSP/90352 FF/142213 LUT；估算周期9.608 ns，超过7.300 ns有效预算。

**Conclusion:** 内联方案的RTL功能、吞吐与硬件数量成立，第二阶段只剩时序阻塞。相对2026-09-17 RTL基线，non-causal/causal分别加速30.79%/28.34%。

### E014 — exp2直接小数域时序候选

**Setup:** 不改变流水拍、hop、PE调用点或乘法器，只删除`prepareExp2`中“小数规格化为FP16并在Raw FMA重新解包”的冗余往返；以未规格化但数值完全等价的尾数和最低位指数直接进入`addFiniteProduct`。

**Result:** 独立Raw FMA的30000随机MAC、定向IEEE和exp2测试通过；4x2、4x4、8x4 causal/non-causal完整attention及Accumulator PWL本地回归全部通过。当前只有本地C++证据，尚未运行新Vitis。

**Conclusion:** 该候选保持数值语义并移除了当前关键路径中的优先编码、FP16打包及二次解包；是否达到<=7.300 ns、是否继续保持SA II1和44 DSP必须由下一次Vitis确认。

## 9. Failed Attempts / Things Not To Repeat

### F001 — Accumulator反馈DATAFLOW环

**Tried:** 把Accumulator拆成请求/响应两个actor并形成stream反馈。

**Result:** `Bad TV file`、post-check失败和RTL CoSim deadlock。

**Why it failed:** DATAFLOW请求/响应反馈环无法完成流量闭合。

**Retry only if:** 有形式化token计数、无环数据流或完全不同的无反馈架构；当前不重试。

### F002 — guard=1与多项索引改写同时进行

**Tried:** 缩小guard，同时引入显式pipeline counter和静态展开选择器。

**Result:** SA主循环 II=12、tile 1651 cycles、估算时钟13.85 ns；CoSim non-causal/causal 15505/10452 cycles；LUT约116711。

**Why it failed:** 多项改动相互混杂；静态选择器/计数器导致大MUX或调度依赖的可能性高，无法把失败单独归因于guard。

**Retry only if:** 一次只改变一个变量并读取完整综合报告。2026-09-14重试仍得到II=12；在明确消除 `pe_pipeline.partial` 读写依赖前不再重复该结构。

### F003 — 脱离已知微程序时序屏蔽PE状态依赖

**Tried:** 在SA主循环使用 `#pragma HLS DEPENDENCE variable=pe_register inter false`，让HLS忽略全部跨迭代状态相关性。

**Result:** 11:02候选同时覆盖 `pe_register/pe_pipeline/cmp_pipeline` 时RTL错误；11:49只撤销 `pe_register` 后错误集合不变，证明48错来自其余多拍slot覆盖。用户指定的8b7aab7代码含单独 `pe_register inter false` 且历史CoSim通过，当前按相同hop16微程序条件重试。

**Why it remains risky:** `pe_register`就是Chisel `PE.reg`，逻辑上存在真实RAW。只有当静态微程序保证所有消费者晚于写回时，该pragma才是对互斥控制条件的人工证明；任何周期表变化后都必须重新做RTL CoSim。

**Retry condition:** 当前hop16周期表与历史8b7aab7证据满足一次受控重试；不得把覆盖扩展到 `pe_pipeline/cmp_pipeline`，也不得在未过CoSim时宣称正确。

### F004 — 单个DMA descriptor actor写多个有限FIFO

**Tried:** `dmaRequestProcess`按query顺序同时产生Q/K/V descriptor，并顺序写三个深度2的request FIFO。

**Result:** 11:29 RTL CoSim在第一个事务0%处死锁；V request FIFO满使producer无法继续产生Scratchpad正在等待的下一Q request。

**Why it failed:** HLS DATAFLOW中的阻塞写把三个本应独立的DMA通道耦合成一个head-of-line链，最终和Scratchpad的消费顺序闭环。

**Retry only if:** 使用非阻塞pending状态机并形式化证明公平性；当前固定采用三个独立单输出请求actor，不再使用多输出descriptor producer。

### F005 — 全局屏蔽PE/CMP token环形槽依赖

**Tried:** 对动态slot数组 `pe_pipeline` 和 `cmp_pipeline` 使用 `#pragma HLS DEPENDENCE ... inter false`，依靠理论10拍/4拍复用距离追求II=1。

**Result:** C++测试通过，但11:02及11:49 RTL均稳定产生相同48个输出错误；只撤销 `pe_register` override没有改变结果。

**Why it is unsafe:** slot写入由9拍PE或3拍CMP子函数返回，读写发生在外层流水的不同stage。全局false同时删除所有RAW/WAW，不能仅凭C循环索引距离证明RTL提交顺序。

**Retry only if:** 将返回值显式前递到下一消费者，或改成HLS可证明的静态stage并读取schedule；不得直接恢复全局false。

## 10. Bugs / Open Problems

### P001 — SA动态slot主循环II=9（已解决）

**Symptoms:** 13:15 guard=7构建中，221次tile循环最终II=9，tile函数latency/interval为1995/1994；9x4 non-causal/causal为18354/12335拍。此前guard=1同样II=9，但因只有155次循环而较快，为13006/8772拍。

**Known facts:** 13:15报告把II违例定位到 `pe_pipeline.partial/element` 的循环携带load/store依赖，仍按distance=1处理；关键路径从动态slot sparsemux/选择进入PE调用。撤销全局pe/cmp pipeline false后RTL数值恢复正确，说明不能重新屏蔽这些真实依赖。

**Result:** 恢复固定hop16下的 `cycle%16`静态slot，并只对微程序已保证安全的 `pe_register`声明inter false。15:44 build达到主循环II=1、tile 237/222，CSim与RTL CoSim通过；未复制PE算术单元。

### P002 — hop=8 SA真实跨hop递归II=2（已解决）

**Known facts:** 23:25 build的冲突为ST11写回与8次迭代后ST1/ST2读取。23:21 build内联坐标PE边界后写回提前至ST9，主循环达到II1、iteration latency9、TileTick 142/134；16条PE乘法通路、4 CMP及DSP44总量保持，RTL CoSim通过。

**Result:** 9x4 non-causal/causal RTL分别为1778/1310 cycles，无死锁、无数值错误。II问题关闭；后续只处理独立时序问题，不通过虚假`inter false`修复。

### P006 — PE内联Raw FMA时序9.608 ns

**Known facts:** 23:21已验证build的关键路径从SA operand选择进入`prepareExp2`，经过小数规格化、FP16打包/解包后到11x11 DSP乘法；估算9.608 ns，超过7.300 ns预算2.308 ns。独立Raw FMA曾达到7.133 ns，说明乘法器本身不是唯一问题。

**Implementation/Plan:** 当前候选直接把exp2小数的尾数/指数送入`addFiniteProduct`，删除规格化打包与二次解包，不增加流水级或硬件乘法。下一次Vitis必须同时核对估算周期、关键路径、SA II、iteration latency、TileTick、16条乘法通路、DSP44和CoSim。

### P003 — Tagged wave仍需K/V operand cache

**Known facts:** SA已逐拍直接消费InputDelayer，Q直接进入PE.reg，入口完整tile屏障已删除；当前8拍PE hop下QK/PV操作数仍不能像Chisel一拍跨行，K/V需要在tile tick内跨hop保存。

**Plan:** 先综合验证当前第3项。若要完全消除K/V cache，必须把tagged wave替换成真正逐PE mesh或让operand随wave携带；这属于第3项的后续架构改动，不能仅靠Delayer接口完成。

### P004 — 快速候选发生C/RTL数值分歧（已解决）

**Symptoms:** RTL正常完成且无死锁，但non-causal/causal共48个O元素不匹配。

**Known facts:** C++功能回归通过；11:49错误集合与11:02完全相同，排除DMA死锁修复和单独撤销 `pe_register` override能解决问题。剩余 `pe_pipeline`/`cmp_pipeline` override同样只影响综合调度。

**Result:** 12:10 build撤销全部SA dependence override后，C post-check通过，48个数值错误消失。代价是SA外层循环只能达到II=9。

### P005 — DMA请求actor拆分后的RTL验证

**Known facts:** 用户日志中的依赖环精确经过多输出 `dmaRequestProcess`；当前源码已无该actor，Q/K/V request各自只有一个独立生产者和消费者。其余多输出actor的输出都由同一后继按相同顺序消费，不构成该跨分支环。

**Result:** 11:49 RTL正常完成，原依赖环未复现；该问题已解决。

## 11. Current Working Set

- 第一阶段DMA扁平循环已由2026-09-17 14:09 build验收；保留 `src/stream/dma_process.cpp` 修改。
- 第二阶段撤销完整tile调用循环的PIPELINE/II约束，并恢复query/key顺序嵌套循环；最新build确认IR规模恢复、无效ALLOCATION警告消失且额外6个控制DSP已删除。
- PE内部latency优化第二阶段已接入独立验收的 `pe_raw_fma`；最新build确认16个坐标特化`spatialPeCell`均为5拍、II=1、DSP1。
- 当前焦点：II1内联候选的RTL CoSim检查点已完成；当前源码正在第二阶段用exp2直接小数域缩短9.608 ns关键路径，不开始第三阶段。
- 当前设计边界：顶层 `fsa_stream` 形参、四个AXI bundle、器件和时钟约束保持不变；内部协议可调整。
- 下一步由用户综合当前时序候选；目标<=7.300 ns且保持II1、142/134量级、16条PE乘法通路、4 CMP及DMA II1。综合门槛通过后再跑RTL CoSim。

## 12. Next Actions

- [x] 当前源码server build、CSim和9x4 causal/non-causal RTL CoSim完成且无死锁。
- [x] 顶层/RTL名为 `fsa_stream`；DSP/BRAM未增加；仍为单SA/单Accumulator向量。
- [x] 用 `%16`静态PE slot和仅限 `pe_register` 的提示解决动态slot调度，SA主循环恢复II=1且RTL正确；全局 `pe_pipeline/cmp_pipeline inter false` 方案继续拒绝。
- [x] 源码已用3拍CMP流水加一级寄存器恢复Chisel CMP->PE边界，并重排微程序周期。
- [x] 去除SA调用前的Q/K/V完整tile收集屏障，在SA周期循环内消费带phase/tag的Delayer beat；Q直接进入PE.reg，K/V cache为当前第3项所需。
- [x] 将OutputDelayer改为持续token II=1的协议流水，不再对每个完整token重置并串行化。
- [x] 将Scratchpad改为唯一owner下的显式完成/公平仲裁调度，允许下一buffer写入与当前tile计算重叠。
- [x] 统一DMA内部请求和完成语义，保留四个专用物理AXI bundle并把store outstanding恢复为8。
- [x] 运行4x2、4x4、8x4本地C++测试并检查token计数、输出和参数化。
- [x] 11:02服务器候选完成RTL运行且无死锁，但post-check有48个输出不匹配，候选判定失败。
- [x] 撤销无条件的 `pe_register inter false`，恢复Scala `PE.reg`的真实跨迭代RAW。
- [x] 定位11:29死锁为单个request actor写Q/K/V三个有限FIFO造成的跨通道head-of-line阻塞。
- [x] 将DMA请求生成拆为Q/K/V三个独立单输出actor，并通过4x2、4x4、8x4本地回归。
- [x] 11:49服务器run确认DMA死锁消失，但仍有与11:02相同的48个数值错误。
- [x] 撤销 `pe_pipeline`/`cmp_pipeline inter false`，当前SA已无全局dependence override；4x4本地回归通过。
- [x] 在服务器运行 `./run_hls.sh fsa_stream`，确认保守依赖调度下CSim、综合与CoSim全部通过。
- [x] 确认CMP/PE独立II=1、OutputDelayer token II=1，DSP/BRAM未因重构复制；SA外层主循环仍为II=9，未达到II=1目标。
- [x] 单变量把PE guard从1回滚为7，使hop从10恢复为16；保留全部已验证功能修复。
- [x] 服务器构建hop=16源码并读取CSim、SA II/tile latency、顶层时序/资源和RTL CoSim；功能通过但II仍为9、端到端慢约41%，实验失败。
- [x] 读取commit 8b7aab7完整PE/SA代码；确认PE算术与当前一致，差异集中在 `%16` slot与PE.reg依赖提示。
- [x] 仅在当前SA移植上述两点，保留SRAM/DMA/Delayer/Accumulator和所有已验证死锁修复。
- [x] 服务器构建8b7aab7调度移植候选：SA II=1、tile 237/222、顶层7.300 ns、资源20 BRAM/108 DSP/74357 FF/109091 LUT，RTL CoSim通过。
- [x] 将Q/K/V DMA读改为每拍单AXI访问的扁平beat循环，并通过4x2、4x4、8x4本地功能回归。
- [x] 新Vitis build确认dmaReadQ/K/V achieved II=1，并通过CSim与RTL CoSim；第一阶段验收通过。
- [x] 第二阶段将有效tile调用压平成单循环并限制完整SA实例数为1；II=1及参数化II均引发百万级IR，现已撤销外层全部II约束并恢复顺序调用。
- [x] PE latency新阶段一：实现独立Raw FMA、独立顶层/testbench/Tcl，并通过30000组随机MAC、定向IEEE和exp2本地位精确回归；正式SA未改。
- [x] 用户重新运行 `./run_hls.sh pe_raw_fma`；CSim通过，综合latency=5、II=1、DSP=1、单11x11乘法实例、估算周期7.133 ns，第一阶段验收成功。
- [x] 第二阶段把Raw FMA接入正式PE，设置latency=5、guard=3、hop=8；外层tile调用II约束已独立回退，先保留内部II=1和单SA结构。
- [x] 读取外层II回退后的08:30 build：CSim/C综合通过，IR规模恢复；16 PE/4 CMP和单TileTick保持，但PE wrapper=6拍、SA II=2、TileTick=278/268，阶段二性能验收失败；CoSim未运行。
- [x] 将`peMacUnit`内联到不内联的坐标特化`spatialPeCell`；删除无效ALLOCATION pragma，并恢复query/key嵌套循环以避免总tile数新增6 DSP。
- [x] 上述第二阶段修复通过4x2、4x4、8x4 causal/non-causal完整attention及Accumulator PWL本地回归。
- [x] PE边界内联和嵌套遍历修复后的新build：PE=5拍/II1/DSP1、顶层DSP44、IR规模正常，但SA仍II=2、TileTick 277/268；CSim通过、CoSim未运行。
- [x] 重构`pe_pipeline.partial`提交/读取回路：删除`row_result`整结构写回，改为控制字段提前提交、PE结果逐字段直写ring槽；保持16 PE/4 CMP、hop=8和安全依赖语义，本地三配置回归通过。
- [x] 读取逐字段提交build：CSim/C综合通过、资源小降，但`HLS 200-880`、iteration latency12、SA II2和TileTick 277/268完全不变；该方案性能失败，未运行CoSim。
- [x] 根据详细schedule分离控制/旁路与原始FMA结果，移除归约反馈上的多层控制mux与输出旁路；不增加FMA调用点、不改变hop、不覆盖真实依赖。
- [x] 读取23:25控制/结果分离build：CSim/C综合通过、资源下降、外围II保持，但真实结果环ST11写/ST1-ST2读仍使SA II2；TileTick 276/268，性能实验失败，未运行CoSim。
- [x] 内联坐标特化PE边界，保留16个展开`peMacUnit`调用点、Raw FMA和hop8，目标删除父循环两拍控制开销；4x2、4x4、8x4 causal/non-causal attention及Acc PWL本地回归通过。
- [x] 读取00:21内联候选CSim/C综合：SA主循环II1、iteration latency9、TileTick 142/134；16条PE乘法通路、4 CMP、DSP44和外围II保持，但估算周期9.608 ns失败。
- [x] 在II1内联候选上完成RTL CoSim功能检查点：9x4 non-causal/causal 1778/1310 cycles，无死锁、无数值错误。
- [x] 第一项时序候选删除exp2小数规格化打包/二次解包，直接传递精确尾数和指数；独立Raw FMA及三种阵列规模本地回归通过。
- [ ] 用户综合当前时序候选；100 MHz验收要求估算周期<=7.300 ns，同时保持SA II1、硬件数量和外围II不退化。
- [ ] 时序综合门槛通过后重新运行RTL CoSim，确认端到端周期及无死锁/数值错误。
- [ ] 第二阶段验收后进入第三阶段3A：保持hop=8，独立实现并验证FP32×FP32+FP32 Raw FMA的位精确性、II、latency、DSP和时序。
- [ ] 3A验收后进入3B：替换当前顶层实际综合出的剩余FP32 FMA通路，确认四个Accumulator lane不增殖，并完成完整CSim/综合/RTL CoSim。
- [ ] 3B验收后进入3C：把CMP的`hls::fma(a,1,-b)`改为单FP32减法通路，移除未使用的`out_max`，保留`finiteAccMax`组合位序选择和`old/local max-new max`方向；重新完成完整验证。
- [ ] 3C验收后进入3D：只把PE hop从8尝试降到4，重新完成完整验证；失败则回退到已验证hop=8版本。
- [ ] 新Vitis build已确认SA II=1且TileTick 142/134优于237/222基线；仍需RTL CoSim和用户完成第二阶段验收。
- [ ] 100 MHz时序与RTL CoSim共同验收第二阶段后，才开始第三阶段算术/CMP/hop优化；150/200 MHz评估更后置。

## 13. Validation Status

- [x] 旧基线4x4 CSim通过（旧build）。
- [x] 旧基线4x4 RTL CoSim通过且无死锁（旧build）。
- [x] 旧基线结构为单SA/单Accumulator向量（旧build）。
- [x] 历史hop=10/Q每KV重放及2/4/5/7重构源码的4x2、4x4、8x4本地功能测试。
- [x] 早期hop=10源码C综合、II、资源和时序已读取：II=12、13.883 ns，性能判定失败。
- [x] 早期hop=10源码RTL CoSim通过：9x4为15540/10466 cycles，无死锁。
- [x] 2026-09-14四拍CMP通道、slot依赖修复与2/4/5/7重构后的本地C++ CSim等价测试。
- [x] 2026-09-14 11:02候选RTL CoSim执行完成但FAIL：48个输出不匹配，无死锁（用户日志）。
- [x] 2026-09-14 11:29候选RTL CoSim FAIL：第一个事务0%处发生DMA request DATAFLOW死锁（用户日志）。
- [x] 三请求actor拆分且撤销 `pe_register inter false` 后的4x2、4x4、8x4本地C++测试。
- [x] 2026-09-14 11:49候选RTL无死锁但CoSim FAIL：与11:02相同的48个输出不匹配。
- [x] 撤销全部SA dependence override后的4x4本地C++测试。
- [x] 撤销全部SA dependence override后的Vitis CSim、综合和RTL CoSim：全部通过，无死锁、无数值错误。
- [x] guard=7/hop=16当前源码的Vitis CSim、综合和RTL CoSim：全部通过，无死锁、无数值错误；性能回退。
- [x] 静态检查commit 8b7aab7与当前PE算术：`src/stream/arithmetic.cpp`及接口无差异；无需修改PE FMA。
- [x] `%16` PE slot和PE.reg提示移植后的Vitis CSim、综合及RTL CoSim；9x4 causal/non-causal正确，无死锁。
- [x] 第一阶段DMA扁平化后的4x2、4x4、8x4本地C++功能回归。
- [x] 第一阶段DMA扁平化后的Vitis CSim、综合与RTL CoSim：Q/K/V II=1，无死锁或数值错误。
- [x] 第二阶段tile调用流水候选的4x2、4x4、8x4本地C++功能回归。
- [x] 独立 `pe_raw_fma_top` 本地C++位精确回归：30000随机MAC、定向IEEE特殊值和exp2模式通过。
- [x] 独立 `pe_raw_fma_top` 首次Vitis CSim定位：7094项均为FP32到FP16输出不一致，所示FP32累加位全部匹配；CSim失败后未执行CSynth。
- [x] 将输出half转换修为正常数RNE、非规格化数带符号FTZ，并用独立golden转换器重新通过30000随机MAC、定向IEEE和exp2本地回归。
- [x] 独立 `pe_raw_fma_top` Vitis CSim/C综合及latency/II/DSP/时序验收：0错误、5拍、II=1、DSP1、7.133 ns。
- [x] 正式PE接入Raw FMA、hop=8后的4x2、4x4、8x4本地完整attention与Accumulator PWL回归。
- [x] 撤销完整tile外层II约束后的4x4 causal/non-causal完整attention与Accumulator PWL本地回归。
- [x] 正式PE接入Raw FMA、hop=8且外层顺序调用后的Vitis CSim与C综合；CSim通过，综合暴露PE wrapper 6拍、SA II=2与TileTick 278/268。
- [x] PE边界内联和嵌套tile遍历修复后的4x2、4x4、8x4本地完整回归。
- [x] PE边界内联和嵌套tile遍历修复后的Vitis CSim/C综合：PE=5拍/II1、顶层DSP44；SA仍II=2、TileTick 277/268，第二阶段性能验收失败。
- [x] PE结果逐字段直写ring槽后的4x2、4x4、8x4本地完整attention与Accumulator PWL回归。
- [x] PE结果逐字段直写ring槽后的Vitis CSim/C综合：功能通过、资源略降，但SA仍II=2、TileTick 277/268，性能实验失败。
- [x] 控制/原始结果分離后的4x2、4x4、8x4本地回归；每配置8组输入×causal/non-causal，共48次完整顶层attention，相对旧SA输出逐位一致，独立golden和输出越界哨兵通过。扩展测试由`FSA_SA_TRANSPORT_REGRESSION`启用，不改变默认HLS事务数。
- [x] 控制/原始结果分离候选的Vitis CSim、调度、层次、资源及时序检查；未运行RTL CoSim。
- [x] 坐标PE边界内联候选的4x2、4x4、8x4本地完整attention与Accumulator PWL回归。
- [x] 坐标PE边界内联候选的Vitis CSim、综合调度、16条PE乘法通路、资源和时序检查；II目标通过，时序失败。
- [x] 正式PE接入Raw FMA、hop=8后的RTL CoSim：1778/1310 cycles，无死锁、无数值错误。
- [x] 修复SA II后第二阶段内联候选的Vitis CSim、综合与RTL CoSim；仅时序未通过。
- [x] exp2直接小数域时序候选的独立Raw FMA及4x2、4x4、8x4本地C++回归。
- [ ] exp2直接小数域时序候选的Vitis CSim、综合调度、资源及时序检查。
- [ ] IP导出。
- [ ] Vivado实现时序。
- [ ] FPGA板级验证。

## 14. Environment

- 本地：Windows/PowerShell；无Vitis，只适合代码检查和C++测试。
- 服务器HLS入口：`./run_hls.sh fsa_stream`。
- Vitis版本（当前build）：2024.2 build 5238294。
- FPGA：`xcvu37p_CIV-fsvh2892-2-e`。
- 时钟：10.0 ns，uncertainty 2.7 ns；不得放宽。
- 常用历史本地命令：
  - `.\run_stream_test.ps1 -Rows 4 -Cols 2`
  - `.\run_stream_test.ps1 -Rows 4 -Cols 4`
  - `.\run_stream_test.ps1 -Rows 8 -Cols 4`

## 15. Context Handoff Summary

1. 正在把完整FSA attention核迁移并优化到HLS，结构目标是Chisel FSA。
2. 当前坚持单SA、单PE MacUnit、单列Accumulator，通过等价流水重定时降低延迟。
3. 最新build为2026-09-19 00:21，对应PE边界内联源码：CSim和C综合通过，但CoSim未运行；最近完整RTL验证仍是2026-09-17 14:09基线，9x4为2569/1828 cycles。
4. `%16` PE slot加仅限PE.reg的调度提示已把SA主循环从II=9恢复为II=1；tile latency/interval为237/222。
5. 相对13:15基线端到端加速6.75--7.14x；相对9月9日稳定版本也快约6.1%--6.5%，说明2/4/5/7外围改造得到保留并产生净收益。
6. 最新顶层HLS估算周期9.608 ns，超过7.300 ns有效预算2.308 ns；资源BRAM20/DSP44/FF90352/LUT142213，尚无Vivado实现证明。
7. 第一阶段DMA已验收：Q/K/V read均II=1；计算、存储、端到端周期均未退化，代价为DMA流水FF增加。
8. 外层tile PIPELINE/II回退已把Unroll/Inline从百万级降至最新30851条；当前层次只有一个TileTick，顺序循环保证单实例。
9. 00:21 build确认内联坐标PE边界把结果写回由ST11提前至ST9，SA由II2降为II1、TileTick由276/268降为142/134，且硬件数量不增加；代价是时序退化至9.608 ns。
10. 当前固定顺序为先跑现有II1版本RTL CoSim建立功能检查点，再留在第二阶段修到<=7.300 ns并重跑CoSim；此前不得开始第三阶段或150/200 MHz评估。
11. SRAM/Scratchpad、DMA三请求actor、Delayer、Accumulator和当前CMP寄存器通路全部保留；禁止恢复 `pe_pipeline/cmp_pipeline inter false`。

## 16. Decision / Progress Log

- 2026-09-09 — 稳定旧build通过CSim与RTL CoSim；9x4为2737/1954 cycles，tile 222。
- 2026-09-09 — 按严格Scala语义恢复每KV Q/K/V三phase，并将guard降为1、hop降为10；尚未验证。
- 2026-09-14 — 启用持久项目上下文；确认build早于当前源码，下一步必须先重新构建。
- 2026-09-14 — 读取当前源码新build：CSim/CoSim通过且无死锁，但SA主循环II=12、tile=1651、时钟13.883 ns，9x4为15540/10466 cycles；当前hop=10实现判定为性能失败。
- 2026-09-14 — 直接修复SA调度：用4槽环形通道表达3拍HLS CMP流水和一级Chisel CMP->PE Pipe，对10拍PE环形slot解除错误distance=1依赖，并将4x4微程序对齐到145拍；按用户要求未运行任何测试或综合。
- 2026-09-14 — 按用户优先级完成2/4/5/7：SA周期内直接消费Delayer（4x4总微程序155拍）、OutputDelayer去除逐token重串行化、Scratchpad K/V公平仲裁、DMA descriptor/last协议及O outstanding=8；4x2、4x4、8x4本地回归全部通过，Vitis待验证。
- 2026-09-14 — 用户提供11:02服务器CoSim：RTL于38995 ns正常结束、无死锁，但post-check有48个O不匹配。重新读取本地修改后，撤销 `pe_register inter false`；该pragma错误隐藏SCALE/PWL/ROW_SUM/PV之间的真实状态RAW，修复尚待服务器验证。
- 2026-09-14 — 用户提供11:29服务器CoSim：第一个事务0%处DMA DATAFLOW死锁。完整审计确认单个request actor被满V FIFO阻塞，无法产生Scratchpad等待的Q descriptor；改为Q/K/V三个独立单输出请求actor，4x2、4x4、8x4本地回归通过，RTL待验证。
- 2026-09-14 — 用户提供11:49服务器CoSim：DMA死锁消失，但与11:02相同的48个输出仍不匹配，否定“只由pe_register override造成”的过强判断；撤销pe/cmp token环形槽的全部全局dependence override，4x4本地回归通过，RTL待验证。
- 2026-09-14 — 读取12:10新build：CSim/RTL CoSim全部通过，DMA死锁和48个数值错误均解决；9x4为13006/8772 cycles，SA主循环155次且II=9，tile=1401/1400；顶层7.964 ns超过7.300 ns有效预算并触发 `HLS 200-871`，资源BRAM20/DSP108/FF68283/LUT120828。当前结论为功能修复成功、性能与HLS时序仍受SA动态slot路径限制。
- 2026-09-14 — 按用户要求只把PE guard从1回滚为7（hop 10->16），保留2/4/5/7重构、DMA三请求actor和安全依赖约束；当前静态SA微程序155->221拍，尚未测试或构建。目的为验证历史hop=16的II=1/7.300 ns优势能否在当前正确结构中复现。
- 2026-09-14 — 读取13:15 hop=16 build：CSim/RTL CoSim通过，无死锁和数值错误；SA仍II=9，tile 1995/1994，9x4为18354/12335。时序仅改善到7.840 ns且仍超7.300 ns预算；BRAM20/DSP108、FF65286/LUT114601。单变量guard回滚判定性能失败，历史II=1来自旧slot表示/数据通路而非7拍guard本身。
- 2026-09-14 — 用户指定commit 8b7aab7作为PE/SA参考。读取确认该commit只改common/SA，当前PE算术已完全一致；在不触碰SRAM/DMA/Delayer/Accumulator的前提下，SA恢复 `cycle%16` PE slot及仅限 `pe_register` 的inter false提示，不恢复曾造成48错的 `pe_pipeline/cmp_pipeline`覆盖。
- 2026-09-14 — 读取15:44 `%16` slot与PE.reg提示新build：CSim/RTL CoSim通过且无死锁、无数值错误；SA主循环II=1，tile 237/222，9x4为2569/1828，总执行4432 cycles。顶层估算7.300 ns等于有效预算，资源BRAM20/DSP108/FF74357/LUT109091。相对13:15加速6.75--7.14x，并略快于9月9日稳定版本；PE/SA修复判定成功，后续转向DMA read II=4与实现时序。
- 2026-09-17 — 更新 `docs/fsa_stream综合报告.md` 为15:44当前build；新增与 `fsa_dma_top` 的同口径9x4综合/CoSim对比，以及与 `FSA-main` 的结构、静态微程序周期和README板级示例对比。FSA-main缺少同器件综合产物，报告明确不计算资源/Fmax/端到端加速比。
- 2026-09-17 — 用户确定三阶段门控计划：先DMA，再tile流水，最后时序/频率；每阶段必须由用户验收后才能进入下一阶段。第一阶段将Q/K/V DMA读的lane/word嵌套循环扁平化，使流水体每拍仅一次AXI读和一次FIFO写；4x2、4x4、8x4本地回归通过，等待新Vitis综合与CoSim验收。
- 2026-09-17 — 读取14:09第一阶段新build：Q/K/V DMA读循环均由II=4降至II=1；CSim/RTL CoSim通过，9x4仍为2569/1828 cycles，SA tile 237/222、PE/CMP/Acc、BRAM20/DSP108和7.300 ns均未退化。FF因三个DMA流水从74357增至88661，LUT从109091降至108077。第一阶段通过，进入第二阶段。
- 2026-09-17 — 第二阶段第一版：把SA有效tile调用压平成单循环，设置PIPELINE II=1优化目标，并以ALLOCATION limit=1禁止复制TileTick；4x2、4x4、8x4本地回归通过，等待Vitis确认单实例条件下的最低achieved II和RTL正确性。
- 2026-09-17 — 第二阶段服务器综合在外层tile `PIPELINE II=1` 后出现中间表示爆炸：Compile/Link仍为292765条，与14:09基线292771几乎一致；但Unroll/Inline升至4135964/2865164条，分别约为基线37172/27858的111/103倍。该告警统计的是编译IR而非最终RTL实例数；新csynth报告尚未同步，不能据此断言PE/SA已复制。最可能原因是单TileTick真实interval=222却要求调用循环II=1，促使HLS展开/内联221拍tile循环。若构建不能合理完成，下一候选应把外层目标改为单实例可实现的II=222并复核16 PE/4 CMP/108 DSP。
- 2026-09-17 — 按用户要求将外层tile流水目标从II=1改为II=222，保留压平循环和TileTick `ALLOCATION limit=1`。后续内部latency主方案为手写raw FP16乘FP16/FP32累加FMA，先以latency=4、II=1、hop=8作为安全目标，再在完整综合/CoSim通过后尝试hop=4；不得通过复制FMA或屏蔽pe/cmp token真实依赖换性能。
- 2026-09-17 — 用户将PE内部latency优化重分为三阶段门控：步骤1/2（独立FMA与资源确认）为第一阶段，步骤3/4（hop=8集成与全链路验证）为第二阶段，步骤5（hop=4）为第三阶段，每阶段由用户手动Vitis验收。第一阶段已新增 `pe_raw_fma_top`、整数位域Raw FMA、30000随机+定向+exp2 testbench和 `./run_hls.sh pe_raw_fma`入口；本地位精确回归通过，正式SA/hop未改，等待Vitis报告。
- 2026-09-18 — 读取首次 `pe_raw_fma` 服务器日志：Vitis CSim报7094项错误，日志所示FP32累加输出全部逐位匹配，差异只在half输出（正常数少1 LSB，负下溢丢失符号）；CSim失败后未进入综合，故尚无latency/II/DSP结论。仅修改独立Raw FMA的FP32->FP16转换为RNE及带符号FTZ，并把testbench golden从平台half cast改为显式规范转换；本地30000随机MAC、定向IEEE与exp2重新通过，等待用户重跑，第二阶段未开始。
- 2026-09-18 — 读取01:24 `pe_raw_fma` 重跑build：CSim 0错误；综合固定latency=5、II=1，估算周期7.133 ns；资源DSP1、FF1628、LUT5638、BRAM/URAM 0，且Bind Op仅一个11x11 DSP乘法实例。第一阶段全部门槛满足并验收，正式SA仍未替换，等待用户确认后进入第二阶段hop=8集成。
- 2026-09-18 — 用户要求第三阶段增加“全部FMA手写替换”。代码审计确认当前PE候选只支持FP16×FP16+FP32，不能直接覆盖Accumulator的FP32×FP32+FP32；第三阶段因此细分为3A独立FP32 Raw FMA、3B在hop=8下替换剩余有效硬件FMA并全链路验收、3C再单变量尝试hop=4，避免多项变化混在一个build中。随后确认CMP不属于FMA目标：有效输出只需`old/local max-new max`减法，逐score最大值由`finiteAccMax`位序比较完成，现有`accCmp.out_max`在`fsa_stream`中未被使用。
- 2026-09-18 — 用户进一步要求CMP修改归入第三阶段。第三阶段更新为3A独立FP32 Raw FMA、3B替换Accumulator有效FMA、3C单独将CMP改为专用FP32减法并移除未使用`out_max`、3D再尝试hop=4；每个检查点都保留手动Vitis验收，CMP修改不与hop变化混合。
- 2026-09-18 — 用户启动算术latency第二阶段：正式`peMacUnit`改为复用已验收Raw FMA，PE latency 9->5、guard 7->3、hop 16->8；4x4静态`SA_TILE_CYCLES`由221降为133，外层tile调用II由固定222改为参数化`SA_TILE_CYCLES+1`（默认134），HLS Tcl加入`pe_raw_fma.cpp`。删除被Raw FMA取代的旧PE PWL预处理死代码；4x2、4x4、8x4本地完整attention和Acc PWL回归全部通过，等待完整Vitis CSim/综合/RTL CoSim，第三阶段未开始。
- 2026-09-18 — 参数化外层II与Raw FMA集成后的服务器构建仍出现Compile/Link 366333、Unroll/Inline 4283956/3210132条指令，规模与此前外层II=1失败实验相近。按用户要求撤销完整tile调用循环的全部PIPELINE/II约束及`SA_TILE_CALL_II`常量，恢复顺序调用唯一TileTick；保留TileTick内部逐拍II=1、PE II=1、单SA限制和Raw FMA/hop=8，等待重新构建隔离验证。
- 2026-09-18 — 外层II回退后的4x4本地回归通过：9x4 causal/non-causal输出完整，Accumulator PWL位处理通过；功能未退化。该结果不代表Vitis编译规模、综合调度、资源或RTL CoSim已经验收。
- 2026-09-18 — 读取08:30外层II回退build：CSim和C综合通过、0 error，Unroll/Inline由4283956/3210132量级降至50605/38962（最终31510），编译爆炸解决；未运行CoSim。正式peMacUnit为5拍/II1/DSP1，但16个spatialPeCell均实际6拍并拒绝max=5约束，distance=8的pe_pipeline依赖使133次SA循环achieved II=2，TileTick 278/268，慢于已验收237/222。顶层资源20 BRAM/50 DSP/89953 FF/148087 LUT，7.300 ns零裕量；DMA II1、CMP 3/1、Acc 10/1未退化。ALLOCATION pragma被忽略但顺序外层仍只综合一个TileTick；阶段二判定未通过。
- 2026-09-18 — 按用户要求继续修复第二阶段：把`peMacUnit`内联进保持`INLINE off`的坐标特化`spatialPeCell`，目标消除ap_ctrl_hs边界第6拍且保留16个独立PE；删除被Vitis忽略的ALLOCATION pragma；把总tile数压平循环恢复为无PIPELINE的query/key嵌套循环，避免`tiles*tiles`/三角数控制生成6 DSP。4x2、4x4、8x4 causal/non-causal完整attention及Acc PWL本地回归通过；新Vitis II、latency、资源和CoSim待验收。
- 2026-09-18 — 读取08:58第二阶段修复build：CSim/C综合通过，IR最终31479条；16个`spatialPeCell`均恢复5拍/II1/DSP1，`HLS 200-892`和无效ALLOCATION警告消失，额外6个控制DSP删除，顶层资源20 BRAM/44 DSP/88997 FF/148018 LUT。SA的133次循环iteration latency由13降到12但仍因distance=8的`pe_pipeline.partial`依赖只能II=2，TileTick 277/268；CoSim未运行，第二阶段仍未通过，下一步继续重构结果槽回路。
- 2026-09-18 — 按用户要求继续修改阶段二：删除`row_result`完整PeWave聚合/写回，让控制字段提前写入当前`cycle%8`槽，并让每个坐标PE的`partial/element/exp2_match`结果逐字段直接提交，减少load->FMA->store路径；未使用全局依赖覆盖，hop和硬件实例目标不变。4x2、4x4、8x4 causal/non-causal完整attention及Accumulator PWL本地回归全部通过，等待服务器CSim/C综合确认SA II和资源。
- 2026-09-19 — 读取00:21 PE边界内联build：CSim/C综合通过，`HLS 200-880`消失，SA循环trip133、iteration latency9、II1，TileTick 142/134；报告中仍为16条11x11 PE乘法通路和4个CMP，顶层BRAM20/DSP44/FF90352/LUT142213，DMA/SRAM/Delayer/Accumulator未退化。估算周期9.608 ns超过7.300 ns有效预算并触发`HLS 200-871`；未运行RTL CoSim，因此第二阶段尚未最终验收。
- 2026-09-19 — 用户提出应先修时序。阶段门控更新为：先对当前II1候选跑RTL CoSim建立功能检查点，再留在第二阶段单变量修复9.608 ns关键路径；100 MHz达到<=7.300 ns且复跑CoSim通过后才验收，不提前进入第三阶段或150/200 MHz评估。
- 2026-09-18 — 读取10:41逐字段提交build：CSim/C综合通过，16 PE/4 CMP、PE 5拍/II1/DSP1和外围II保持；顶层20 BRAM/44 DSP/88977 FF/147222 LUT、7.300 ns。`pe_pipeline.partial`的distance8警告仍在，SA iteration latency12、II2、TileTick 277/268完全不变；未运行CoSim。实验仅节省20 FF/796 LUT，性能失败，证明整结构写回不是根因；下一步改独立launch/completion stage。
- 2026-09-18 — 用户要求修复SA II2。详细schedule定位ST1结果读取、ST4控制选择、ST5 PE调用（父循环跨度7拍）、ST12写回。改为控制/旁路PeWave与无初始化PeResult独立环；MAC从固定相邻行结果选输入，原始结果无条件写回，先捕获所有旧值再更新。保留hop8、唯一FMA/坐标、DMA/SRAM/CMP/Acc与100MHz约束。4x2、4x4、8x4各16次顶层attention相对修改前逐位一致，独立golden与哨兵通过；等待用户Vitis确认II/资源/RTL。仅分FIFO或总iteration latency判断不足以证明II改善。
- 2026-09-18 — 读取23:25控制/结果分离build：CSim/C综合通过，16 PE保持5拍/II1/DSP1、4 CMP保持3拍/II1/DSP2，DMA/SRAM/Delayer/Acc关键II未退化；顶层20 BRAM/44 DSP/87577 FF/144041 LUT、7.300 ns。SA loop iteration latency 12->11、TileTick latency 277->276，但II2和interval268不变。`HLS 200-880`转移到原始`pe_results.out_accType`，schedule为ST11写、distance8后ST1顶行输出读/ST2归约读，证明控制mux已非根因。CoSim未运行，阶段二仍不通过。
- 2026-09-19 — 继续修复SA II2：将坐标特化`spatialPeCell`改为内联，删除其独立PIPELINE/LATENCY边界；完全展开模板仍保留16个`peMacUnit`调用点，Raw FMA/hop8及外围不变。目的为把父循环中ST5--ST11的7级调用缩回Raw FMA本身约5级，使结果写回满足distance8。4x2、4x4、8x4 causal/non-causal完整attention及Acc PWL本地回归通过；新Vitis需重点确认II、ST级、16个PE DSP和时序。
