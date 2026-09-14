# FSA HLS Project Context

> 本文件是 `FSA_HLS` 的持久任务状态。后续工作开始时先完整读取，并在关键决策、代码阶段或新验证结果后原地更新。

## 1. Mission

- 最终目标：在 Vitis HLS 中实现与 Chisel FSA 架构和时序语义一致的完整 attention 核，并在不复制计算阵列的前提下降低延迟、提高吞吐。
- 顶层一次调用完成一次完整 attention；例如 `L=9, D=4` 的 Q/K/V 只能调用一次 `fsa_stream`。
- 顶层接收 Q/K/V/O 地址、序列长度和 causal 模式，核内 DMA 自动搬运。
- 当前最高优先级：在保持单套 SA 和原 FSA 数据流的条件下降低 KV tile 间隔与 PE 多周期流水带来的放大延迟。

## 2. Hard Constraints

- Chisel `FSA-main/src/main/scala/fsa/` 是功能、结构和时序语义的主要参考，不修改该目录。
- 只维护新路径：`src/stream/`、`include/fsa/stream/`、`tests/stream/`、`hls/fsa_stream/`。
- 顶层固定为 `fsa_stream`；不改器件、100 MHz 时钟约束、2.7 ns uncertainty、AXI 位宽或顶层协议。
- `SA_ROWS`、`SA_COLS` 必须参数化，不能写死 4x4。
- 只有一套 `R x C` SA；QK 与 PV 顺序复用；每个 PE 只有一个 MacUnit 和一个元素精度 `PE.reg`。
- 每列一个 CMP；每列一个 Accumulator MAC/exp2/reciprocal 通道。不得用复制 SA、PE 算术阵列或 Accumulator 向量换吞吐。
- S/P 继续驻留在 `PE.reg`；不得新增完整 score/probability 数组。
- 保持 Scratchpad/AccRAM 的 bank/sub-bank、64-bit 物理字、full/narrow 访问与单写端口语义。
- 保持 InputDelayer/OutputDelayer、CMP causal counter、在线 softmax、PWL 段顺序和 `PROP_MAX_DIFF -> EXP_S1 -> EXP_S2 -> L/O` 顺序。
- 不使用 `accumulator_pipeline` 架构。
- 不进行 Git 操作。除非用户改变要求，否则本地只读代码/文档，不运行测试；Vitis 由服务器执行。

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

### Latest server run（当前候选，用户提供末尾日志）

- 服务器路径：`/home/zhangchenxuan/FSA_HLS/hls/fsa_stream/build/solution1`；xsim 于 2026-09-14 11:02:13 结束。
- RTL正常执行到 `$finish`（38995 ns），没有死锁；失败发生在随后的C post-check。
- 9x4 non-causal/causal共报告48个O元素不匹配，最终为 `COSIM 212-361`、`C/RTL co-simulation finished: FAIL`。
- **Current assessment:** 仿真结束时间较旧run明显缩短，主循环很可能已接近II=1，但尚未取得该次build的综合报告，不能确认精确II、时序、资源或分事务latency。
- **Root-cause assessment:** `pe_register inter false` 隐藏了Scala `PE.reg`的真实跨迭代RAW。SCALE写回到首个PWL读取、PWL写回到ROW_SUM/PV读取都要求严格有序；pragma不影响C仿真，却可能让II=1 RTL读取旧状态。
- 当前源码已撤销这一条错误依赖覆盖；保留有明确槽复用距离的 `pe_pipeline`/`cmp_pipeline inter false`，等待下一次服务器验证。

### Latest build（修复前性能失败基线，已旧于当前源码）

- 构建目录：`build/fsa_stream_build/solution1`；生成时间 2026-09-14 09:42--09:50。该build对应修复前hop=10源码，但当前源码已再次修改，因此只作为失败基线。
- Vitis HLS 2024.2；器件 `xcvu37p_CIV-fsvh2892-2-e`；目标 10.0 ns，uncertainty 2.7 ns。
- CSim 通过：一次顶层调用完成 9x4 causal 与 non-causal attention。
- Verilog/xsim CoSim 通过，无死锁：
  - 9x4 non-causal：15540 cycles；
  - 9x4 causal：10466 cycles；
  - 非法长度：55 cycles；
  - 三事务总执行：26041 cycles。
- 顶层估算周期 13.883 ns，Estimated Fmax 72.03 MHz，未满足100 MHz约束；无Vivado实现后时序证明。
- 顶层资源：BRAM18K 20、DSP 114、FF 80808、LUT 115319、URAM 0。
- `spatialSystolicArrayTileTick`：latency/interval 1651/1651；主循环137次，iteration latency=14，目标II=1但最终II=12。
- II=12的直接原因是 `pe_pipeline.partial` 在 `systolic_array.cpp:567` 上的循环携带读写依赖；不是KV DMA/写SRAM限制。
- 关键路径串联 `spatialCmpOutputCell<0>`（6.602 ns）和 `spatialPeCell<0,0>`（6.375 ns），位于同一SA主循环调度路径。
- K/V DMA循环和Scratchpad K/V窄写循环均综合到II=1；64-bit物理存储保持成立：Scratchpad 2个12x64存储共4 BRAM，AccRAM 4个3x64存储共8 BRAM。
- 层次仍是单4x4 SA：16 PE + 4 CMP；单 `systolicArrayProcess`、单 `spatialSystolicArrayTileTick`、单 `accumulatorProcess`、单 `accumulatorArithmeticVector`，Accumulator为4个算术lane。
- Accumulator中没有FP32 `fsub`实例；每lane仅见一组共享算术路径的FP32 `fadd`/`fmul`。`accumulatorArithmeticVector` latency=10、II=1。
- 没有 IP export、Vivado implementation 或板级验证结果。

### Current source

- `PE_TOKEN_LATENCY=9`。
- `PE_SCHEDULER_GUARD_CYCLES` 已从 7 改为 1，因此 `PE_HOP_CYCLES=10`。
- 当前显式建模 `CMP_TOKEN_LATENCY=3`，并保留 Chisel CMP->PE 的一级 Pipe，因此 `CMP_HOP_CYCLES=4`。
- 4x4、PWL=8 时，当前源码微程序 `SA_TILE_CYCLES` 为155；其中前21拍直接消费Q/K/V Delayer beat，QK在K phase结束后的第14拍启动，V装入与QK发射重叠。这是静态公式，尚无新综合结果。
- `systolic_array.cpp` 用显式回绕 `pipeline_slot` 取代 `% PE_HOP_CYCLES`，仍保留动态数组索引。
- SA主循环对 `pe_pipeline` 增加经调度证明安全的inter-dependence覆盖：同一slot每10拍才重用，而PE结果延迟9拍；同迭代先读后写依赖保持不变。
- SA主循环不再对 `pe_register` 使用 `inter false`；该数组是Scala `PE.reg`的真实状态，必须保留SCALE/PWL/ROW_SUM/PV之间的RAW顺序。
- 新增4槽 `CmpToPeStage` 环形通道：前三拍容纳当前HLS CMP输出流水，最后一拍对应Chisel `pipe_no_reset(cmp.io.d_output)`；UPDATE score回流以及PROP_MAX/PWL/ROW_SUM经过该通道，SCALE/PV仍直接注入。
- `spatialCmpOutputCell` 保持独立的 II=1、latency=3 流水函数，避免把CMP与PE组合串联；微程序已为score回流、SUB_MAX依赖、PWL和ROW_SUM/PV入口重新对齐。
- 每个 KV tile 已恢复 Q/K/V 三个 InputDelayer phase；Q 从原 Scratchpad Q buffer 重放，每个 KV tile 都重新执行逻辑 `LOAD_STATIONARY`。
- SA已从周期循环第0拍直接消费带phase/tag的Delayer beat：Q直接写唯一 `PE.reg`，不再形成 `q_tile`；K phase结束即启动QK，V phase与QK发射重叠。由于第3项仍采用10拍hop的tagged wave，K/V仍保留跨hop operand cache，尚不是Scala逐PE mesh的逐线直连。
- OutputDelayer保留为独立actor，但当前tagged SA输出本已按列对齐，因此不再将每个完整token重新串行化成 `SA_COLS` 拍；循环目标为token II=1。
- Scratchpad仍是唯一banked SRAM owner；Q以显式last完成，K/V由同一个II=1循环公平仲裁并以last分别完成。当前tile的行被推入FIFO后，owner即可在SA计算期间装入下一双缓冲区。
- DMA新增统一内部 `DmaReadRequest` descriptor以及每个写流的 `request_id/kind/transfer_last` 完成语义；顶层仍保留Q/K/V/O四个专用AXI bundle，O写outstanding已从16恢复为8。
- 撤销 `pe_register inter false` 之前的4x2、4x4、8x4本地C++回归均通过；该pragma不影响C语义。撤销后的源码未运行本地测试；HLS II、时序、资源与CoSim均待服务器验证。

### Main issue

- 最新服务器run已经从“II=12且功能正确”转为“运行明显加快但RTL结果错误”。`$finish`是正常结束，不是死锁；真正失败是48个输出的C/RTL post-check不一致。
- 首要根因是无条件忽略 `pe_register` 的真实RAW，当前已撤销。下一次build必须先确认CoSim恢复正确，再判断移除pragma后II是否仍为1；在此之前暂停继续叠加2/4/5/7或PE算术改动。

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
- HLS PE cell 已是 II=1，但 latency=9。KV 瓶颈来自跨行依赖和保守 hop，不是单个 PE 的启动间隔。
- 4x4、PWL=8、3拍CMP流水加一级CMP->PE寄存器时，当前源码 `SA_TILE_CYCLES=155`；旧结构还需在调用SA前另等21拍收集输入，而当前155拍已经包含输入消费。

## 6. Important Files

| File | Purpose | Important notes |
|---|---|---|
| `AGENTS.md` | 仓库约束 | 开始修改前完整读取 |
| `docs/FSA硬件与时序约束及参考代码分析.md` | 架构与逐周期硬约束 | 所有优化的首要判据 |
| `include/fsa/stream/common.hpp` | actor 协议与 SA 周期常量 | 当前 guard=1、hop=10 |
| `src/stream/controller.cpp` | tile 控制和 SA 微程序 | causal、phase 顺序来源 |
| `src/stream/scratchpad.cpp` | 64-bit banked SRAM 与Q/K/V重放 | Q现在每个KV tile重放 |
| `src/stream/input_delayer.cpp` | Q/K/V阶梯延迟 | 当前每tile固定3 phase |
| `src/stream/systolic_array.cpp` | 单SA、PE/CMP与环形token流水 | 当前优化和主要风险所在 |
| `src/stream/accumulator_process.cpp` | 单actor微指令 Accumulator | 避免反馈环死锁 |
| `src/stream/arithmetic.cpp` | PE/Acc FMA、PWL、reciprocal | PE latency优化入口 |
| `src/stream/dataflow.cpp` | 顶层actor连接 | 检查生产/消费与死锁 |
| `hls/fsa_stream/run_hls.tcl` | HLS入口 | `set_top fsa_stream` |
| `build/fsa_stream_build/solution1/` | 最新现有构建 | 修复前失败基线；已旧于当前源码 |

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

**Decision:** 用4槽 `CmpToPeStage` 环形通道表达3拍HLS CMP流水加一级Chisel CMP->PE寄存器，并对每10拍才重用的 `pe_pipeline` 动态slot声明无distance=1 inter dependence；CMP槽每4拍重用，覆盖其3拍流水延迟。

**Reason:** Chisel `SystolicArray.scala` 明确使用 `pipe_no_reset(cmp.io.d_output)`；旧HLS build遗漏该边界并错误推断相邻迭代slot依赖，分别造成13.883 ns组合路径和II=12。

**Evidence/Result:** CMP/PE代码已完成；D006并入输入消费后4x4总微程序为155拍。11:02服务器run无死锁但C post-check出现48个输出不匹配；确认无条件屏蔽 `pe_register` RAW的实现不可接受，现已撤销该pragma。

**Implication:** 下一build必须同时确认II=1和CMP/PE关键路径分离；若仍失败，只继续处理这一调度边界，不改变单SA结构。

**Status:** Active after RAW-ordering correction; unverified

### D006 — 优先重构输入/输出/存储/DMA流

**Decision:** 保留单SA和现有多周期PE token调度，依次实现：Delayer数据在SA周期循环内消费、OutputDelayer每拍推进、唯一Scratchpad owner的读写请求调度、DMA内部统一请求/完成语义；顶层四个AXI bundle暂不改变。

**Reason:** 用户明确指定优先处理架构差异2、4、5、7；专用Q/K/V/O物理端口仍有利于当前HBM带宽，先统一内部协议可避免无收益的顶层接口破坏。

**Evidence/Result:** 四项代码均已实现：SA周期内消费Delayer；OutputDelayer token II=1；Scratchpad owner公平仲裁K/V并以last收敛；DMA descriptor/完成标记和store outstanding=8。4x2、4x4、8x4本地功能回归通过，尚无新综合结果。

**Implication:** 不复制Q/K/V SRAM、不复制SA/FMA；允许保留多周期PE所需的小型operand/token流水状态，但删除“所有Q/K/V完整收集后才启动SA”和“完整结果重新串行化”的事务屏障。

**Status:** Active, locally verified; HLS unverified

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

### F003 — 无条件屏蔽PE状态寄存器的跨迭代依赖

**Tried:** 在SA主循环使用 `#pragma HLS DEPENDENCE variable=pe_register inter false`，让HLS忽略全部跨迭代状态相关性。

**Result:** 本地C++回归通过，但快速RTL候选post-check有48个输出不匹配。

**Why it failed:** `pe_register`就是Chisel `PE.reg`。SCALE写回到下一拍PWL读取、匹配PWL写回到ROW_SUM/PV读取均为真实RAW；该pragma只改变综合调度，因此会产生C/RTL分歧。

**Retry only if:** 对具体假依赖使用窄范围证明，或以显式前递保持真实RAW；不得再次对整个 `pe_register` 声明 `inter false`。

## 10. Bugs / Open Problems

### P001 — SA主循环II=12且时序失败

**Symptoms:** 137次tile循环实际耗时1645拍，tile函数1651拍；顶层估算周期13.883 ns；9x4 non-causal/causal为15540/10466拍。

**Known facts:** HLS报告把II违例定位到 `systolic_array.cpp:567` 的 `pe_pipeline.partial` 循环携带load/store依赖；关键路径把CMP输出和PE计算串在同拍。

**Next investigation:** 修复已实现；在服务器重新构建，确认 `pe_pipeline.partial` II违例消失、主循环II=1、CMP与PE不再位于同一关键路径。若依赖pragma仍不足，再把动态环形slot改成显式静态stage移位，但不能复制PE算术单元。

### P002 — PE latency仍为9拍

**Known facts:** 旧报告中每PE为5 DSP、latency=9、II=1；内部包含half到float、FP32乘加和float到half转换。

**Plan:** 在保持每PE单FMA和数值语义的前提下，研究FP32部分和与FP16写回的完成点分离，或实现等价混合精度FMA。

### P003 — Tagged wave仍需K/V operand cache

**Known facts:** SA已逐拍直接消费InputDelayer，Q直接进入PE.reg，入口完整tile屏障已删除；但10拍PE hop使QK/PV操作数不能像Chisel一拍跨行，K/V需要在tile tick内跨hop保存。

**Plan:** 先综合验证当前第3项。若要完全消除K/V cache，必须把tagged wave替换成真正逐PE mesh或让operand随wave携带；这属于第3项的后续架构改动，不能仅靠Delayer接口完成。

### P004 — 快速候选发生C/RTL数值分歧

**Symptoms:** RTL正常完成且无死锁，但non-causal/causal共48个O元素不匹配。

**Known facts:** C++功能回归通过；被撤销的 `pe_register inter false` 只影响综合调度，并隐藏了真实状态RAW。错误随query/feature扩散，符合softmax/PV读取旧PE状态的表现。

**Next investigation:** 服务器重建撤销pragma后的源码。先确认CoSim恢复正确；再读取SA loop schedule，确认自然RAW是否保持II=1，并校验PE/CMP调用返回到环形槽写入的实际stage。

## 11. Current Working Set

- 当前修改范围：`include/fsa/stream/common.hpp`、`src/stream/controller.cpp`、`src/stream/dataflow.cpp`、`src/stream/dma_process.cpp`、`src/stream/scratchpad.cpp`、`src/stream/input_delayer.cpp`、`src/stream/output_delayer.cpp`、`src/stream/systolic_array.cpp`、相关stream测试。
- 当前焦点：修复11:02服务器run的C/RTL不一致；已撤销错误的 `pe_register inter false`，保留4拍 `CmpToPeStage` 与有槽距离依据的环形依赖覆盖。
- 当前设计边界：顶层 `fsa_stream` 形参、四个AXI bundle、器件和时钟约束保持不变；内部协议可调整。
- 下一检查：在服务器重新构建，先看CoSim是否恢复，再读取loop II、OutputDelayer II、时序和资源。

## 12. Next Actions

- [x] 当前源码server build、CSim和9x4 causal/non-causal RTL CoSim完成且无死锁。
- [x] 顶层/RTL名为 `fsa_stream`；DSP/BRAM未增加；仍为单SA/单Accumulator向量。
- [x] 源码已处理 `pe_pipeline.partial` 错误distance=1依赖。
- [x] 源码已用3拍CMP流水加一级寄存器恢复Chisel CMP->PE边界，并重排微程序周期。
- [x] 去除SA调用前的Q/K/V完整tile收集屏障，在SA周期循环内消费带phase/tag的Delayer beat；Q直接进入PE.reg，K/V cache为当前第3项所需。
- [x] 将OutputDelayer改为持续token II=1的协议流水，不再对每个完整token重置并串行化。
- [x] 将Scratchpad改为唯一owner下的显式完成/公平仲裁调度，允许下一buffer写入与当前tile计算重叠。
- [x] 统一DMA内部请求和完成语义，保留四个专用物理AXI bundle并把store outstanding恢复为8。
- [x] 运行4x2、4x4、8x4本地C++测试并检查token计数、输出和参数化。
- [x] 11:02服务器候选完成RTL运行且无死锁，但post-check有48个输出不匹配，候选判定失败。
- [x] 撤销无条件的 `pe_register inter false`，恢复Scala `PE.reg`的真实跨迭代RAW。
- [ ] 在服务器运行 `./run_hls.sh fsa_stream`，验证RAW修复后的CSim、综合与CoSim。
- [ ] 确认SA主循环恢复II=1、CMP/PE关键路径分离、OutputDelayer token II=1，且DSP/BRAM未因重构复制。
- [ ] 最后研究PE FP32结果早出和等价混合精度FMA；每次只改变一个性能变量。

## 13. Validation Status

- [x] 旧基线4x4 CSim通过（旧build）。
- [x] 旧基线4x4 RTL CoSim通过且无死锁（旧build）。
- [x] 旧基线结构为单SA/单Accumulator向量（旧build）。
- [x] 当前hop=10/Q每KV重放及2/4/5/7重构源码的4x2、4x4、8x4本地功能测试。
- [x] 当前源码C综合、II、资源和时序已读取：II=12、13.883 ns，性能判定失败。
- [x] 当前源码RTL CoSim通过：9x4为15540/10466 cycles，无死锁。
- [x] 2026-09-14四拍CMP通道、slot依赖修复与2/4/5/7重构后的本地C++ CSim等价测试。
- [x] 2026-09-14 11:02候选RTL CoSim执行完成但FAIL：48个输出不匹配，无死锁（用户日志）。
- [ ] 撤销 `pe_register inter false` 后的Vitis CSim、综合和RTL CoSim。
- [ ] IP导出。
- [ ] Vivado实现时序。
- [ ] FPGA板级验证。

## 14. Environment

- 本地：Windows/PowerShell；无Vitis，只适合代码检查和C++测试。
- 服务器HLS入口：`./run_hls.sh fsa_stream`。
- Vitis版本（旧build）：2024.2 build 5238294。
- FPGA：`xcvu37p_CIV-fsvh2892-2-e`。
- 时钟：10.0 ns，uncertainty 2.7 ns；不得放宽。
- 常用历史本地命令：
  - `.\run_stream_test.ps1 -Rows 4 -Cols 2`
  - `.\run_stream_test.ps1 -Rows 4 -Cols 4`
  - `.\run_stream_test.ps1 -Rows 8 -Cols 4`

## 15. Context Handoff Summary

1. 正在把完整FSA attention核迁移并优化到HLS，结构目标是Chisel FSA。
2. 当前坚持单SA、单PE MacUnit、单列Accumulator，通过等价流水重定时降低延迟。
3. 旧稳定build为2737/1954 cycles；最新现有build功能通过但退化为15540/10466 cycles，该build现已旧于当前源码。
4. 11:02快速RTL候选无死锁但有48个输出不匹配；根因判断为错误屏蔽Scala `PE.reg`真实RAW，源码已撤销该pragma。
5. 已排除复制阵列、多query状态复制、Accumulator反馈环和一次混合多项激进改写。
6. 下一步是在服务器重建并先确认CoSim恢复正确，再检查II/关键路径/资源；结果出来前不继续叠加PE或数据流重构。

## 16. Decision / Progress Log

- 2026-09-09 — 稳定旧build通过CSim与RTL CoSim；9x4为2737/1954 cycles，tile 222。
- 2026-09-09 — 按严格Scala语义恢复每KV Q/K/V三phase，并将guard降为1、hop降为10；尚未验证。
- 2026-09-14 — 启用持久项目上下文；确认build早于当前源码，下一步必须先重新构建。
- 2026-09-14 — 读取当前源码新build：CSim/CoSim通过且无死锁，但SA主循环II=12、tile=1651、时钟13.883 ns，9x4为15540/10466 cycles；当前hop=10实现判定为性能失败。
- 2026-09-14 — 直接修复SA调度：用4槽环形通道表达3拍HLS CMP流水和一级Chisel CMP->PE Pipe，对10拍PE环形slot解除错误distance=1依赖，并将4x4微程序对齐到145拍；按用户要求未运行任何测试或综合。
- 2026-09-14 — 按用户优先级完成2/4/5/7：SA周期内直接消费Delayer（4x4总微程序155拍）、OutputDelayer去除逐token重串行化、Scratchpad K/V公平仲裁、DMA descriptor/last协议及O outstanding=8；4x2、4x4、8x4本地回归全部通过，Vitis待验证。
- 2026-09-14 — 用户提供11:02服务器CoSim：RTL于38995 ns正常结束、无死锁，但post-check有48个O不匹配。重新读取本地修改后，撤销 `pe_register inter false`；该pragma错误隐藏SCALE/PWL/ROW_SUM/PV之间的真实状态RAW，修复尚待服务器验证。
