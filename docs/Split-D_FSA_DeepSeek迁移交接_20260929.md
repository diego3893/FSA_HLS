# Split-D FSA DeepSeek迁移交接

## 1 文档用途与状态优先级

本文用于把`fsa_stream_split_d`的后续工作交给DeepSeek智能体。接手后不要从历史方案重新推导，也不要重复已经排除的实验；先按本文恢复最新状态，再从第8节开始工作。

当不同文档发生冲突时，按以下优先级判断：

1. 用户在新会话中的最新明确要求。
2. 本迁移文档和仓库根目录[`PROJECT_CONTEXT.md`](../PROJECT_CONTEXT.md)。
3. 最新修改日志[`2026-09-29_1032_split_d_qk_load_修改日志.md`](修改日志/2026-09-29_1032_split_d_qk_load_修改日志.md)。
4. 历史实现规格与验收要求。
5. `docs/split_d_implementation_plan_20260923/PROJECT_CONTEXT.md`只是一份历史快照，已明确停止维护，不得把它当作当前状态来源。

建议依次阅读：

- [`00_阅读入口.md`](split_d_implementation_plan_20260923/00_阅读入口.md)：历史文档导航。
- [`02_SplitD详细架构规格.md`](split_d_implementation_plan_20260923/02_SplitD详细架构规格.md)：算法、状态、数据布局和数值合同。
- [`03_Agent实施步骤与验收.md`](split_d_implementation_plan_20260923/03_Agent实施步骤与验收.md)：历史实施步骤和验收框架。
- [`FSA硬件与时序约束及参考代码分析.md`](FSA硬件与时序约束及参考代码分析.md)：器件、时钟和参考模块约束。
- [`2026-09-29_0129_split_d_full_acceptance_修改日志.md`](修改日志/2026-09-29_0129_split_d_full_acceptance_修改日志.md)：RTL数值错误定位及Q/K/V加载修复过程。
- [`2026-09-29_1032_split_d_qk_load_修改日志.md`](修改日志/2026-09-29_1032_split_d_qk_load_修改日志.md)：4×4最终验收和16×16扩展结果。

## 2 一句话结论

- **4×4/HEAD_DIM=16版本已经完整合格。**CSim、CSynth和RTL CoSim全部通过，数据9/9通过，无deadlock，DSP40，估算周期7.300ns，QK/ROW_SUM为II5，PWL/PV为II1。
- **16×16/HEAD_DIM=128版本尚不合格。**CSim通过、CSynth完成、估算周期仍为7.300ns，生成RTL确认只有一套256PE阵列和一套16列Accumulator；但`runPeArray`因控制流过于复杂无法流水化，连带QK、ROW_SUM、PWL和PV调用循环无法达到目标II。RTL CoSim运行到4/7后按用户要求停止，未执行最终C post-check，因此不能宣称RTL数据通过。
- 当前主要问题已经从“算法或数据错位”转为“16×16大阵列控制与流水结构不可扩展”。

## 3 当前代码与入口

- 分支：`fsa_split_D`
- 当前代码基线commit：`83b9b51585cbc3ed4002f596b2c437a8afae8523`
- 当前仓库HEAD：`8216f04e69a605039e954c062d619da5272f89e2`，该commit只在上述代码基线上补充4×4验收文档。
- 当前工作树含尚未提交、尚未经过Vitis回归的文件拆分和本迁移文档；接手时不得把HEAD等同于当前工作树内容。
- Split-D顶层：[`src/stream/split_d/fsa_stream_split_d.cpp`](../src/stream/split_d/fsa_stream_split_d.cpp)
- Split-D主控制器：[`src/stream/split_d/split_d_controller.cpp`](../src/stream/split_d/split_d_controller.cpp)
- PE/Accumulator执行模块：[`src/stream/split_d/split_d_compute.cpp`](../src/stream/split_d/split_d_compute.cpp)
- DMA tile搬运：[`src/stream/split_d/split_d_dma.cpp`](../src/stream/split_d/split_d_dma.cpp)
- 内部模块接口：[`include/fsa/stream/split_d/split_d_internal.hpp`](../include/fsa/stream/split_d/split_d_internal.hpp)
- 参数配置：[`include/fsa/stream/split_d/split_d_config.hpp`](../include/fsa/stream/split_d/split_d_config.hpp)
- 测试：[`tests/stream/test_fsa_stream_split_d.cpp`](../tests/stream/test_fsa_stream_split_d.cpp)
- HLS脚本：[`hls/fsa_stream_split_d/run_hls.tcl`](../hls/fsa_stream_split_d/run_hls.tcl)
- 统一运行入口：[`run_hls.sh`](../run_hls.sh)

### 3.1 交接前源码拆分

原`fsa_stream_split_d.cpp`约800行，交接前已按现有`stream`目录的职责拆分方式重构：

- `fsa_stream_split_d.cpp`只保留HLS顶层接口和参数检查。
- `split_d_controller.cpp`保存attention主控制流程、QK、softmax和PV调度。
- `split_d_compute.cpp`保存唯一PE阵列、唯一Accumulator列组、结果寄存级和倒数列模块。
- `split_d_dma.cpp`保存Q/K/V tile加载和O tile写回。
- `split_d_internal.hpp`只声明实现内部跨文件接口。
- `run_hls.tcl`已经加入全部新实现文件。

本次拆分没有主动改变算法、pragma、循环顺序或函数签名，`git diff --check`和符号唯一性检查通过；但本地没有Vitis头文件，因此尚未执行拆分后的CSim/CSynth。尤其Q/K/V加载函数虽然仍保留`#pragma HLS INLINE`，其定义现在位于独立翻译单元，接手后必须先通过4×4CSynth确认它们仍被正确内联，不能把这次源码级拆分视为已经完成HLS回归。

默认命令：

```bash
./run_hls.sh fsa_stream_split_d
```

16×16/128命令：

```bash
FSA_SPLIT_D_PE_DIM=16 FSA_SPLIT_D_HEAD_DIM=128 \
./run_hls.sh fsa_stream_split_d
```

服务器环境：

- SSH别名：`FSA-FPGA-NM37-tailBox`
- 仓库：`/home/zhangchenxuan/FSA_HLS`
- 工具：Vitis HLS 2024.2
- 每次SSH必须使用Bash并显式加载`~/.bashrc`，否则工具环境和网络代理可能不可用。

## 4 不得改变的任务边界

### 4.1 算法合同

- 物理结构必须是参数化`D×D`阵列，而不是原来的`R×C`阵列。
- 默认`D=4`、`dim=16`；目标配置只改参数得到`D=16`、`dim=128`。
- 每个PE保留一个FP16工作寄存器`reg`和一个FP32累加寄存器`score_acc`。
- QK必须执行`dim/D`个block；每个block消费D个特征，最终形成完整S。
- softmax只能在完整S形成后执行。
- PV必须再次执行`dim/D`个block，并复用同一套PE阵列。
- 不得在阵列外增加完整S或P副本，不得放宽RawFMA、PWL、舍入、特殊值或0.03数据容差。

### 4.2 硬件合同

- QK、SUB_MAX、SCALE、PWL、ROW_SUM和PV必须顺序复用同一套物理PE阵列。
- 每列只允许一条Accumulator RawFMA通路；不得针对不同阶段复制Accumulator。
- 默认4×4资源目标已经验收为PE16DSP、Accumulator8DSP、减法器16DSP，总计DSP40。
- 16×16允许资源按真实256PE结构增长，但必须证明只有一套256PE阵列和一套16列Accumulator，不能通过阶段复制换取吞吐。
- 不修改生产顶层`fsa_stream`，不改变Q/K/V/O四个64-bit AXI master bundle和AXI-Lite控制接口。

### 4.3 时序与吞吐合同

- 器件固定为`xcvu37p_CIV-fsvh2892-2-e`。
- 时钟固定10ns，uncertainty固定2.7ns，因此HLS估算周期必须不超过7.300ns。
- 目标II：QK和ROW_SUM接受真实反馈II5；PWL和PV必须达到II1。
- 禁止用`PIPELINE off`、虚假`DEPENDENCE false`、复制阵列、改变器件或放宽时钟掩盖问题。
- “估算周期7.300ns”只说明关键路径估计合格，不等于循环II和实际吞吐合格。

### 4.4 16×16 CoSim墙钟超时合同

以下规则只适用于当前NM37服务器、Vitis HLS 2024.2、当前7事务testbench和参数`PE_DIM=16`、`HEAD_DIM=128`：

- 从`xsim`输出`## run all`开始计时，到7/7事务、RTL仿真退出和C post-check结束为止；不把CSim、CSynth、Verilog编译和`xelab`时间计入。
- 正常墙钟区间定义为15至45分钟。考虑服务器负载和事件仿真的波动，45至60分钟记为警告区间，但仍可等待最终结果。
- 超过60分钟仍未完成7/7和C post-check，立即停止本次CoSim，并判定该候选“CoSim超时、性能/可验证性不合格”。不得因为已经完成部分事务而继续无限等待。
- 连续20分钟没有任何事务计数或intra-transaction进度变化，可以提前按超时处理；先保存最后进度和日志，再停止仿真。
- 超时不等价于数值失败或deadlock。报告必须分别写明已完成事务数、最后进度、是否有deadlock报告、C post-check是否执行，以及超时导致的数据状态“未验收”。
- 不允许通过缩减测试事务、关闭检查或只跑CSim来规避该墙钟门槛。若以后更换服务器、工具版本或testbench，必须重新标定时间区间，并得到用户确认后才能修改60分钟门槛。

该区间是针对当前环境的工程验收值，不是由RTL仿真时间戳直接换算出的数学上限。依据是4×4完整HLS流程约6分钟、16×16算术实例增加16倍但测试序列更短，以及正确流水结构不应产生当前每个小事务约17分钟的低吞吐。正常候选应在几十分钟内完成；60分钟已经包含较宽的事件仿真和服务器负载余量。

### 4.5 数据合同

- CSim和RTL CoSim都必须通过。
- 现有testbench覆盖单key、双key全1-V、首特征basis-V、末特征basis-V、因果和非因果等用例。
- 所有有效用例最大误差必须不超过0.03。
- 必须无deadlock、无`Bad TV file`，且C post-check通过。

## 5 已经验证的4×4结果

被测commit为`83b9b51585cbc3ed4002f596b2c437a8afae8523`，参数为`PE_DIM=4`、`HEAD_DIM=16`。

| 项目 | 结果 |
|---|---|
| CSim | PASS |
| CSynth | PASS |
| RTL CoSim | 9/9，C post-check PASS |
| Deadlock/Bad TV | 无 |
| 估算周期 | 7.300ns |
| 资源 | BRAM8、DSP40、FF29732、LUT124651 |
| `runPeArray` | DSP16、latency4、II1 |
| `runAccumulatorColumns` | DSP8、latency7、II1 |
| 关键循环 | QK/ROW_SUM II5，PWL/PV II1 |

这轮最终修复是把Q/K共用的`loadElemTile`强制内联。此前V加载已经内联；Q/K内联后，feature0和last-feature basis诊断同时通过，证明历史RTL数据错位已经修复。不要撤销Q/K/V加载内联，也不要重新尝试非内联共享加载器。

## 6 16×16最新结果

被测代码与4×4完全相同，只设置`FSA_SPLIT_D_PE_DIM=16`和`FSA_SPLIT_D_HEAD_DIM=128`。远端运行在RTL CoSim达到4/7后由用户要求停止，不需要重复这次长时间测试。

### 6.1 已证实

- CSim通过：`[PASS] fsa_stream_split_d: PE=16x16 HEAD_DIM=128 DIM_BLOCKS=8`。
- CSynth成功完成，耗时约2小时41分；作为对比，4×4的完整综合约2分31秒。
- HLS估算周期仍为7.300ns。
- `runAccumulatorColumns`仍可达到II1。
- 生成RTL中关键PE算术核为256个，Accumulator算术核为16个，说明参数确实生成一套16×16阵列和一套16列Accumulator，没有生成多套阶段专用阵列。
- HLS明确报告`runPeArray`的控制流过于复杂，无法流水化。
- QK循环以及调用`runPeArray`的ROW_SUM、PWL、PV相关循环随之无法达到请求的pipeline II。
- 综合过程中出现大量高扇出控制网络；已观察到多个控制信号扇出约9000至25000。
- RTL CoSim能够推进，不是停在0/7：停止前完成4/7，前三个事务完成点约为17.07ms、34.13ms、51.22ms，第4个约68.31ms；每个小事务约需17分钟墙钟时间。
- 停止前没有deadlock报告。
- 按第4.4节新增门槛估算，当前结构完成7事务需要约2小时，显著超过60分钟硬超时；即使数值最终可能正确，也必须判为CoSim超时和整体不合格。

### 6.2 尚未证实

- RTL CoSim没有完成7/7，也没有执行最终C post-check，不能宣称16×16 RTL数据正确。
- 本次停止前没有把16×16顶层资源表完整抄回仓库；后续如需资源数字，应读取现存远端build报告，不能猜测。
- 7.300ns只是HLS估计，没有Vivado实现后时序证明。
- 未做IP导出、Vivado实现和板级测试。

## 7 16×16耗时异常的分析

### 7.1 高置信度原因

1. PE数由16增加到256，是16倍增长；`runPeArray`的完整数组端口、完全分区、双重完全展开和结果级也同步扩展。
2. 当前`runPeArray`是单个巨大函数：一次接收完整`PeState[16][16]`、`operand_b[16][16]`、`operand_c[16][16]`，同时展开256个`peMacUnit`，并通过`exp2_mode`支持多种阶段。
3. Vitis已经直接报告该函数控制流过于复杂，pipeline失败。这是性能不合格的直接证据，不只是编译慢的猜想。
4. 调用者中的QK、PWL、ROW_SUM和PV都依赖这个非流水函数，因此各自pipeline也被连带破坏。
5. 巨型完全分区数组会被标量化为大量端口、选择器、使能和返回信号；高扇出控制网络使HLS调度、绑定、RTL生成和事件驱动RTL仿真都显著变慢。

### 7.2 当前推测

- 算术规模增加本身不足以解释“2分31秒到2小时41分”的差距；主要放大器应是巨型函数边界、模式控制和完整数组握手造成的调度搜索与控制网络爆炸。
- CoSim每个短事务约17分钟而且进度稳定，表现更像大量RTL事件和低吞吐状态机，而不是死锁。
- 4×4时`runPeArray`能够pipeline，说明算法数据依赖不是根本障碍；问题在于同一种平铺控制结构不能直接扩到256PE。

## 8 推荐的下一步修改方向

目标不是改变算法，而是把“大而平”的PE执行入口改成“层次化、可流水”的同一套物理阵列。

### 8.1 第一优先级：重构单PE执行入口

- 将256PE拆成固定层次的row bank或小tile bank，例如16个“每行16PE”的bank，或16个4×4子tile。
- 每个bank内部完全展开并独立保持简单II1流水；上层只负责并行连接这些bank，不在一个函数中携带整个256PE复杂控制流。
- bank划分只是同一套256PE阵列的层次化RTL组织，不能为QK、PWL、ROW_SUM、PV分别生成bank副本。
- 模式选择尽量移出最内层大规模流水控制，或在进入bank前形成简单、统一的PE操作数据；不得通过建立多个阶段专用算术函数而复制PE资源。
- 重新评估`stagePeArrayResult`是否需要携带完整256元素函数握手；可以按bank分级寄存，但总物理PE数量必须保持256。

### 8.2 第二优先级：保持控制循环可综合

- QK仍然是8个block、每个block16个feature，保持原FMA顺序和FP32反馈。
- ROW_SUM保持真实反馈II5，不允许用错误的依赖pragma强迫II1。
- PV继续使用独立feature上下文交错隐藏反馈延迟；当前8路交错在4×4已验证，不应无证据地改成16路。
- 修改后先单独综合PE bank和顶层调用循环，确认pipeline成功，再做长时间CoSim。

### 8.3 不建议继续尝试

- 继续给当前巨大`runPeArray`叠加`PIPELINE`、`LATENCY`或`ALLOCATION`pragma。
- 使用`PIPELINE off`回避综合错误。
- 对真实PE反馈增加`DEPENDENCE false`。
- 为不同阶段建立多套PE或Accumulator。
- 增大FIFO、切换FRP或重新引入`hls::task`反馈环；历史上这些方案没有解决结构问题，部分方案造成RTL deadlock。
- 在当前16×16结构不变时再次完整跑7事务CoSim；它只会重复约两小时级的仿真，不能修复pipeline失败。

## 9 建议的验收顺序

每个候选必须按以下顺序验收，前一项失败就先分析报告，不要直接发起长CoSim：

1. 本地`git diff --check`和两种参数的C++/CSim检查。
2. 4×4/16 CSynth回归：必须保持单阵列、DSP40、7.300ns、QK/ROW_SUM II5、PWL/PV II1。
3. 16×16/128 CSynth：先检查`runPeArray`或新bank函数是否真正pipeline、各调用循环实际II、实例数量、高扇出和顶层资源。
4. 只有16×16的结构、II和时序通过后，才运行RTL CoSim；从`## run all`开始执行第4.4节的60分钟硬超时。
5. CoSim必须在60分钟内完成全部事务和C post-check；中间完成若干事务不能算数据通过。
6. 最后再考虑IP导出、Vivado实现后时序和板测，它们不属于当前已完成范围。

每次报告至少记录：被测commit、完整命令、参数、CSim、CSynth、顶层资源、层次/实例数、关键循环II、估算周期、CoSim事务数、C post-check和退出码。

## 10 智能体行为限制

- 修改前先读根目录`PROJECT_CONTEXT.md`和最新修改日志；不得依赖聊天记忆猜测当前状态。
- 只维护根目录`PROJECT_CONTEXT.md`。不要修改`docs/split_d_implementation_plan_20260923/PROJECT_CONTEXT.md`。
- 工作树中与任务无关的改动属于用户，不得覆盖、清理或混入提交。
- 每轮只提交本轮任务文件；远端必须测试精确commit，并记录本地与远端HEAD。
- 未读取对应build报告前，不得声称DSP、实例数、II、时序或CoSim已经通过。
- CSim通过不能替代CSynth或RTL CoSim；HLS估算周期通过不能替代II通过。
- 遇到HLS错误先读取完整report、日志和生成层次，再修改源码；不要根据单条警告盲目加pragma。
- 远端已有未跟踪`evidence/`、`logs/`和Vivado日志，不在本任务范围，不得删除。
- 未经用户新授权，不要无限迭代、推送或占用服务器进行数小时重测。

### 10.1 单轮迭代闭环

每一轮必须完整执行并记录以下闭环：

1. 根据上一轮真实build证据提出一个最小修改假设。
2. 本地修改并完成静态检查或可用的本地测试。
3. 只提交本轮任务文件，记录commit并推送。
4. 远端使用Bash加载`~/.bashrc`，确认工作树后fast-forward到精确commit。
5. 按第9节顺序测试；触发第4.4节超时门槛时立即停止。
6. 读取新build的结构、资源、II、时序和数据证据，不能只看终端最后一行。
7. 更新本次调用的修改日志和根目录`PROJECT_CONTEXT.md`。
8. 每完成一轮暂停并向用户汇报；只有仍在用户授权的轮数内且用户要求继续时，才进入下一轮。

用户指定最大轮数时必须遵守；提前全部合格则提前结束。用户要求停止、保存结果或不再重测时，立即终止后续轮次，并如实记录未完成的验证，不能自行补跑。

## 11 接手后的第一个具体动作

先不要运行16×16 CoSim。第一步应当验证交接前的纯文件拆分：

1. 使用默认4×4/16运行CSim和CSynth；
2. 确认Q/K/V加载内联层次没有回退；
3. 确认仍只有一套16PE和一套4列Accumulator；
4. 确认DSP40、7.300ns、QK/ROW_SUM II5、PWL/PV II1保持；
5. 若CSynth指标完全一致，再决定是否用一次4×4RTL CoSim验证拆分没有改变RTL数据。

完成拆分回归后，再设计并实现最小的层次化PE bank候选，使：

1. 4×4仍映射为唯一16PE阵列；
2. 16×16映射为唯一256PE阵列；
3. `runPeArray`的巨型单函数边界被拆小；
4. 4×4的数值和资源不回退；
5. 16×16 CSynth不再报告“control-flow is too complicated to be pipelined”。

只有第5项成立并读到实际循环II后，才值得继续16×16 RTL数据验收。
