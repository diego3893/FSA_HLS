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

### Confirmed stable baseline build（旧于当前源码）

- 构建目录：`build/fsa_stream_build/solution1`。
- 构建时间：2026-09-09 09:22--09:29；当前相关源码修改时间为 2026-09-09 13:07，因此该 build **不对应当前源码**。
- Vitis HLS 2024.2；器件 `xcvu37p_CIV-fsvh2892-2-e`；目标 10.0 ns，uncertainty 2.7 ns。
- CSim 通过：一次顶层调用完成 9x4 causal 与 non-causal attention。
- Verilog/xsim CoSim 通过，无死锁：
  - 9x4 non-causal：2737 cycles；
  - 9x4 causal：1954 cycles；
  - 非法长度：55 cycles；
  - 三事务总执行：4726 cycles。
- 顶层估算周期 7.300 ns，等于有效预算，HLS 估算裕量约 0；无实现后时序证明。
- 顶层资源：BRAM18K 20、DSP 114、FF 86978、LUT 109469、URAM 0。
- `spatialSystolicArrayTileTick`：latency/interval 222/222，估算周期 7.265 ns，DSP 88。
- `systolicArrayProcess` 最小 latency/interval 269；DATAFLOW 最小 latency 353、最小 interval 270。
- 层次保持单 4x4 SA：16 PE + 4 CMP；Accumulator 为 4 列通道。
- 没有 IP export、Vivado implementation 或板级验证结果。

### Current source（尚未验证）

- `PE_TOKEN_LATENCY=9`。
- `PE_SCHEDULER_GUARD_CYCLES` 已从 7 改为 1，因此 `PE_HOP_CYCLES=10`。
- 4x4、PWL=8 时，源码微程序 `SA_TILE_CYCLES` 从 203 降为 137；这是公式结果，不是综合结果。
- `systolic_array.cpp` 用显式回绕 `pipeline_slot` 取代 `% PE_HOP_CYCLES`，仍保留动态数组索引。
- 每个 KV tile 已恢复 Q/K/V 三个 InputDelayer phase；Q 从原 Scratchpad Q buffer 重放，每个 KV tile 都重新执行逻辑 `LOAD_STATIONARY`。
- 当前仍在 SA 边界把 Delayer 输出恢复到完整 `q_tile/k_tile/v_tile` 局部数组后再调用 tile tick；尚未改成 Scala 式逐拍直连。
- 本轮最新代码没有执行本地测试、C 综合或 CoSim；正确性、II、资源及时序均待验证。

### Main issue

- 需要首先确认 hop=10 + 显式计数器能否保持 SA 主循环 II=1。若不能，当前优化不可接受。
- 即使 hop=10 成功，PE 本身仍是 9-cycle latency；与 Chisel 单拍组合 MacUnit 的调度目标仍有明显差距。

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
- 4x4、PWL=8 的当前统一 hop 公式：`SA_TILE_CYCLES = 27 + 11 * PE_HOP_CYCLES`。

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
| `build/fsa_stream_build/solution1/` | 最新现有构建 | 当前为旧基线，不可代表最新源码 |

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

**Evidence/Result:** 仅有源码公式，尚无新 build。

**Implication:** 下一步必须先验证主循环II、时序和CoSim；失败时不得继续叠加其他优化。

**Status:** Active, unverified

### D004 — 严格恢复每KV tile的Q装载语义

**Decision:** 每个KV tile都从Scratchpad重放Q并经过InputDelayer，执行Q/K/V三阶段。

**Reason:** Chisel Python kernel在每个KV block前调用 `LOAD_STATIONARY`；Score会用S/P覆盖 `PE.reg`。

**Evidence/Result:** 当前源码生产/消费数量已静态对齐，尚无运行验证。

**Implication:** 旧基线中的“每query tile只送一次Q”优化不再是当前状态。

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

**Retry only if:** 一次只改变一个变量并读取完整综合报告。当前源码再次使用显式计数器但保留动态索引，因此仍需重点检查II。

## 10. Bugs / Open Problems

### P001 — 当前源码没有对应构建

**Symptoms:** build报告时间早于源码约3.5小时。

**Known facts:** 旧build为guard=7/Q单次发送基线；当前源码为guard=1/Q每KV重放。

**Next investigation:** 在服务器运行 `./run_hls.sh fsa_stream`，随后检查新产物时间与源码、CSim、SA II、tile latency、时钟、资源和CoSim。

### P002 — PE latency仍为9拍

**Known facts:** 旧报告中每PE为5 DSP、latency=9、II=1；内部包含half到float、FP32乘加和float到half转换。

**Plan:** 在保持每PE单FMA和数值语义的前提下，研究FP32部分和与FP16写回的完成点分离，或实现等价混合精度FMA。

### P003 — SA入口仍完整收集tile

**Known facts:** InputDelayer已逐拍输出，但 `systolicArrayProcess` 先恢复完整Q/K/V数组，再调用tile tick。

**Plan:** 仅在hop=10验证稳定后，评估按Scala ExecutionPlan逐拍消费Scratchpad/Delayer数据；不得增加额外完整tile缓存。

## 11. Current Working Set

- 当前修改文件：`include/fsa/stream/common.hpp`、`src/stream/scratchpad.cpp`、`src/stream/input_delayer.cpp`、`src/stream/systolic_array.cpp`。
- 当前焦点：hop=10环形token调度与每KV Q/K/V三阶段流量。
- 当前假设：一拍guard足以覆盖9拍PE流水提交；显式计数器在不引入静态选择器时可能保持II=1。
- 下一检查：读取新server build；当前没有验证结果。

## 12. Next Actions

- [ ] 在服务器对当前源码运行 `./run_hls.sh fsa_stream`。
- [ ] 确认构建读取新 `src/stream/fsa_stream.cpp`，顶层/RTL名为 `fsa_stream`。
- [ ] 确认 CSim 和 9x4 causal/non-causal RTL CoSim通过且无死锁。
- [ ] 检查 SA主循环是否仍为II=1，tile latency/interval是否明显低于222。
- [ ] 检查顶层DSP/BRAM不增加、仍为单SA/单Accumulator向量，并检查时钟是否在7.3 ns有效预算内。
- [ ] 若当前阶段通过，再开始Scala式逐拍Scratchpad/InputDelayer/SA重构。
- [ ] 最后研究PE FP32结果早出和等价混合精度FMA；每次只改变一个性能变量。

## 13. Validation Status

- [x] 旧基线4x4 CSim通过（旧build）。
- [x] 旧基线4x4 RTL CoSim通过且无死锁（旧build）。
- [x] 旧基线结构为单SA/单Accumulator向量（旧build）。
- [ ] 当前hop=10/Q每KV重放源码的本地功能测试。
- [ ] 当前源码C综合、II、资源和时序。
- [ ] 当前源码RTL CoSim及实际9x4 latency。
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
3. 旧稳定build已通过9x4 CSim/CoSim，性能为2737/1954 cycles；Accumulator死锁已修复。
4. 最新源码把hop从16降到10并恢复每KV Q装载，但没有任何对应验证。
5. 已排除复制阵列、多query状态复制、Accumulator反馈环和一次混合多项激进改写。
6. 第一下一步是运行并读取当前源码的新 `fsa_stream` build；在结果出来前不要继续叠加PE或数据流重构。

## 16. Decision / Progress Log

- 2026-09-09 — 稳定旧build通过CSim与RTL CoSim；9x4为2737/1954 cycles，tile 222。
- 2026-09-09 — 按严格Scala语义恢复每KV Q/K/V三phase，并将guard降为1、hop降为10；尚未验证。
- 2026-09-14 — 启用持久项目上下文；确认build早于当前源码，下一步必须先重新构建。
