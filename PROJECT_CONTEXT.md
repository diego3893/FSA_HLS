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
- 4x4、PWL=8 时，修复后的源码微程序 `SA_TILE_CYCLES` 为145；这是静态公式，尚无新综合结果。
- `systolic_array.cpp` 用显式回绕 `pipeline_slot` 取代 `% PE_HOP_CYCLES`，仍保留动态数组索引。
- SA主循环对 `pe_pipeline` 增加经调度证明安全的inter-dependence覆盖：同一slot每10拍才重用，而PE结果延迟9拍；同迭代先读后写依赖保持不变。
- 新增4槽 `CmpToPeStage` 环形通道：前三拍容纳当前HLS CMP输出流水，最后一拍对应Chisel `pipe_no_reset(cmp.io.d_output)`；UPDATE score回流以及PROP_MAX/PWL/ROW_SUM经过该通道，SCALE/PV仍直接注入。
- `spatialCmpOutputCell` 保持独立的 II=1、latency=3 流水函数，避免把CMP与PE组合串联；微程序已为score回流、SUB_MAX依赖、PWL和ROW_SUM/PV入口重新对齐。
- 每个 KV tile 已恢复 Q/K/V 三个 InputDelayer phase；Q 从原 Scratchpad Q buffer 重放，每个 KV tile 都重新执行逻辑 `LOAD_STATIONARY`。
- 当前仍在 SA 边界把 Delayer 输出恢复到完整 `q_tile/k_tile/v_tile` 局部数组后再调用 tile tick；尚未改成 Scala 式逐拍直连。
- 本轮按用户要求没有运行测试或综合；修复后源码的功能、II、时序、资源与CoSim均待服务器验证。

### Main issue

- 修复代码已针对旧build的两个直接根因：错误的 `pe_pipeline` distance=1依赖，以及未显式保留的Chisel CMP->PE Pipe；同时保留旧build已观测到的3拍CMP算术流水。
- 新源码尚无对应build；在确认SA主循环恢复II=1、关键路径不再串联CMP与PE之前，不继续叠加逐拍直连或PE算术重构。

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
- 4x4、PWL=8、3拍CMP流水加一级CMP->PE寄存器时，当前源码 `SA_TILE_CYCLES=145`。

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

**Evidence/Result:** 代码已完成，4x4静态微程序由137调整为145拍；尚未运行CSim/综合/CoSim。

**Implication:** 下一build必须同时确认II=1和CMP/PE关键路径分离；若仍失败，只继续处理这一调度边界，不改变单SA结构。

**Status:** Active, unverified

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

## 10. Bugs / Open Problems

### P001 — SA主循环II=12且时序失败

**Symptoms:** 137次tile循环实际耗时1645拍，tile函数1651拍；顶层估算周期13.883 ns；9x4 non-causal/causal为15540/10466拍。

**Known facts:** HLS报告把II违例定位到 `systolic_array.cpp:567` 的 `pe_pipeline.partial` 循环携带load/store依赖；关键路径把CMP输出和PE计算串在同拍。

**Next investigation:** 修复已实现；在服务器重新构建，确认 `pe_pipeline.partial` II违例消失、主循环II=1、CMP与PE不再位于同一关键路径。若依赖pragma仍不足，再把动态环形slot改成显式静态stage移位，但不能复制PE算术单元。

### P002 — PE latency仍为9拍

**Known facts:** 旧报告中每PE为5 DSP、latency=9、II=1；内部包含half到float、FP32乘加和float到half转换。

**Plan:** 在保持每PE单FMA和数值语义的前提下，研究FP32部分和与FP16写回的完成点分离，或实现等价混合精度FMA。

### P003 — SA入口仍完整收集tile

**Known facts:** InputDelayer已逐拍输出，但 `systolicArrayProcess` 先恢复完整Q/K/V数组，再调用tile tick。

**Plan:** 仅在hop=10验证稳定后，评估按Scala ExecutionPlan逐拍消费Scratchpad/Delayer数据；不得增加额外完整tile缓存。

## 11. Current Working Set

- 当前修改文件：`include/fsa/stream/common.hpp`、`src/stream/scratchpad.cpp`、`src/stream/input_delayer.cpp`、`src/stream/systolic_array.cpp`。
- 当前焦点：验证4拍 `CmpToPeStage` 环形通道与 `pe_pipeline inter false` 是否让hop=10环形token调度恢复II=1，同时切断CMP到PE的同拍关键路径。
- 当前修改：`include/fsa/stream/common.hpp`、`src/stream/systolic_array.cpp`；此前Q重放相关修改仍在 `src/stream/scratchpad.cpp`、`src/stream/input_delayer.cpp`。
- 下一检查：服务器重新构建；当前build不可代表修复后源码。

## 12. Next Actions

- [x] 当前源码server build、CSim和9x4 causal/non-causal RTL CoSim完成且无死锁。
- [x] 顶层/RTL名为 `fsa_stream`；DSP/BRAM未增加；仍为单SA/单Accumulator向量。
- [x] 源码已处理 `pe_pipeline.partial` 错误distance=1依赖。
- [x] 源码已用3拍CMP流水加一级寄存器恢复Chisel CMP->PE边界，并重排微程序周期。
- [ ] 在服务器运行 `./run_hls.sh fsa_stream`，验证修复后CSim、综合与CoSim。
- [ ] 确认SA主循环从II=12恢复II=1，且CMP/PE不再形成同拍关键路径。
- [ ] 修复后重新确认tile latency低于222、9x4延迟优于2737/1954，且DSP/BRAM不增加。
- [ ] 若当前阶段通过，再开始Scala式逐拍Scratchpad/InputDelayer/SA重构。
- [ ] 最后研究PE FP32结果早出和等价混合精度FMA；每次只改变一个性能变量。

## 13. Validation Status

- [x] 旧基线4x4 CSim通过（旧build）。
- [x] 旧基线4x4 RTL CoSim通过且无死锁（旧build）。
- [x] 旧基线结构为单SA/单Accumulator向量（旧build）。
- [ ] 当前hop=10/Q每KV重放源码的本地功能测试。
- [x] 当前源码C综合、II、资源和时序已读取：II=12、13.883 ns，性能判定失败。
- [x] 当前源码RTL CoSim通过：9x4为15540/10466 cycles，无死锁。
- [ ] 2026-09-14四拍CMP通道与slot依赖修复后的CSim、综合、RTL CoSim（本轮未运行）。
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
4. 已用“3拍HLS CMP流水 + 1拍Chisel边界”恢复CMP->PE通道、解除错误slot distance=1依赖，并把4x4微程序重排为145拍；尚未验证。
5. 已排除复制阵列、多query状态复制、Accumulator反馈环和一次混合多项激进改写。
6. 第一下一步是在服务器重建并检查II/关键路径；结果出来前不要继续叠加PE或数据流重构。

## 16. Decision / Progress Log

- 2026-09-09 — 稳定旧build通过CSim与RTL CoSim；9x4为2737/1954 cycles，tile 222。
- 2026-09-09 — 按严格Scala语义恢复每KV Q/K/V三phase，并将guard降为1、hop降为10；尚未验证。
- 2026-09-14 — 启用持久项目上下文；确认build早于当前源码，下一步必须先重新构建。
- 2026-09-14 — 读取当前源码新build：CSim/CoSim通过且无死锁，但SA主循环II=12、tile=1651、时钟13.883 ns，9x4为15540/10466 cycles；当前hop=10实现判定为性能失败。
- 2026-09-14 — 直接修复SA调度：用4槽环形通道表达3拍HLS CMP流水和一级Chisel CMP->PE Pipe，对10拍PE环形slot解除错误distance=1依赖，并将4x4微程序对齐到145拍；按用户要求未运行任何测试或综合。
