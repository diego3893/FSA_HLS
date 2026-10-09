# Split-D修正与TAPA启发的分阶段修改验收方案

> 日期：2026-10-09。状态：修改与验收计划，尚未实施代码改动或新构建。本文以本次源码和原始日志核查为依据，修正旧交接中的推论，并作为后续工作的执行顺序。近期范围为4×4/head16；16×16/head128仍暂缓验证。

> 同日第二次核查：已按官方Tcl的全部9个编译源文件及关联头文件，对照当前控制器、数值测试和相关Chisel实现复查。第13节记录新增修正；本轮为静态代码核查，未运行编译、HLS、Vivado或板测。

## 1 目标、范围与不变约束

目标分两层：先使4×4成为功能、物理实例和性能证据完整的基线，再验证“局部计算块＋距离感知通信流水”是否改善实际布局布线。最终判断依据是正确结果、固定计算资源下的端到端性能和实现后时序。

- 保持独立顶层`fsa_stream_split_d`；不修改`FSA-main`和生产顶层`fsa_stream`。
- 保持VU37P器件、Q/K/V/O四个独立AXI master bundle、64-bit内存打包格式、AXI-Lite控制、10ns时钟和2.7ns uncertainty。源码设有`max_widen_bitwidth=512`，这是允许拓宽的上限，不代表实际端口就是512-bit；重构前后以接口报告/RTL核对实际数据宽度及burst参数，不能只凭C类型推断外部接口不变。
- 保持总共一套D×D物理PE、一套D列Accumulator；各阶段顺序复用。空间分块是把同一套阵列分成互不重叠的子块，不能为QK/PWL/PV复制整阵列。
- 每个PE的算法状态仍为一个FP16 `reg`和一个FP32 `score_acc`；完整S/P只驻留这套状态，禁止在块外保存完整副本。流水寄存器、有限通信缓冲可以增加，但必须说明容量、生命周期和用途，不能暗中成为完整S/P备份。
- 完整head维QK累加结束后才进入softmax；保留各query的online最大值、指数和、输出更新顺序，以及RawFMA/PWL/转换的位宽、舍入和特殊值合同。
- 保留每个点积按feature递增、PV按key递增的浮点累加顺序。head维并行归约、split-K或矩形重映射另属架构变更，不混入本计划主线。
- 不恢复虚假的PE反馈`DEPENDENCE false`、已经失败的请求/响应task控制闭环或单DMA actor顺序写多个有限请求FIFO。

本次授权是编写方案。此前SSH授权仅覆盖只读日志核对，不覆盖远端构建、IP导出、Vivado实现和板测；Git操作仍由用户执行。后续各阶段进入远端执行前按实际范围和轮数取得授权，已有授权范围内连续完成该轮工作，不重复询问。

## 2 已核实的起点

### 2.1 性能与版本

| 项目 | 当前4×4/head16 | 历史16×16/head128 |
|---|---|---|
| 版本 | P3a，`426ff6c` | P1，`f9f0638`，不是当前P3a形状 |
| CSim/CSynth/RTL CoSim | 均通过 | 均通过当时测试 |
| 顶层HLS估算周期 | 7.300ns | 7.934ns，超出7.300ns有效预算 |
| QK/ROW_SUM/PWL/PV循环II | 5/5/1/1 | 8/8/5/5 |
| BRAM/DSP/FF/LUT | 8/40/31923/125123 | 8/160/177842/550164 |
| `runPeArray` | 报告latency3、II1；日志Depth4 | 日志Depth8、Final II5 |
| CoSim覆盖 | 8个有效事务＋1个非法长度事务 | 6个有效事务＋1个非法长度事务 |
| IP导出/实现后时序/板测 | 未验证 | 未验证 |

证据：服务器`/tmp/p3a_4x4.log`、`/tmp/p3a_4x4_summary.log`、`/tmp/p1_16x16.log`、`/tmp/p1_summary.log`。报告latency和调度日志Depth分别记录，不混成同一指标。

本地分支引用为`591c6fc`，远端为`426ff6c`；本次对比的4个Split-D实现文件、4个头文件、Tcl和testbench共10个文件SHA256逐个一致。这证明这些文件当前一致，不代替未来构建的完整源码清单。

来源不明的2026-10-01远端构建已覆盖`syn/report`，按既有要求不追查。该目录以及本地拆分前生成物均不能证明当前版本的物理实例数。当前4×4已有功能与性能通过记录，但“唯一物理阵列”仍需对应新构建补齐实例证据。

### 2.2 原交接中需要纠正的说法

| 原说法或不足 | 核查结论 | 后续处理 |
|---|---|---|
| 一个函数定义＋控制器`ALLOCATION limit=1`保证单实例 | 有6处调用，跨控制器、未内联的QK与ROW_SUM层级；不能据此证明全局共享 | 先查实例路径，必要时改层级或收敛执行点 |
| 调用latency8意味着II5在物理上不可能 | latency与II不同；反馈路径及依赖距离才限制连续相关操作，单元自身II也限制请求率 | 分开记录单元II、完整反馈延迟、依赖距离和阶段周期 |
| 4×4延迟降到3拍，16×16也会如此 | 没有当前形状的16×16证据 | 保留为待验证假设 |
| 16×16阵列II5同时支持PWL/PV II1 | PWL每次迭代调用阵列；若单共享实例Final II5，无法持续每拍调用 | 在恢复16×16前先统一吞吐目标和单元服务能力 |
| 第9.2节QK/ROW_SUM的II1表示每次累加II1 | 混淆函数和循环指标；已确认循环II均为5 | 报告逐项注明函数/循环、Target/Final、latency/Depth |
| `finiteAccMax`忽略NaN | 位序比较没有NaN检测，正NaN可能被选为最大值 | 核实attention层特殊值策略，不直接改变算法 |
| 数值testbench能完整判错 | NaN差值的比较可能全部为假，误差检查漏报 | 在既有testbench中显式检查非有限输出 |
| 16×16的7/7覆盖满tile和多tile | 当前最大有效长度5，未覆盖16或17等边界 | 恢复16×16后使用参数化边界用例 |
| V按转置顺序装载 | Q/K/V均为`tile[token][feature]`；区别在消费方式 | 修正文档，不额外增加转置缓存 |
| 改`PE_BANK_ROWS`即可拆当前阵列 | 当前`runPeArray`不使用遗留bank函数 | 不把无效宏调整作为实验 |
| 共享半阵列调用两次仍是256个物理PE | 可能只剩128个MAC配256份状态，不满足物理PE合同 | 主线使用空间互斥分块，不使用时间复用冒充空间并行 |
| 方形改D×P即可统一key/head归约 | head点积与key softmax归约是不同维度；还涉及舍入和状态存储 | 暂不采用该变更 |
| UG1399的arrays句子解释204-65 | 原文解释RAM端口争用的204-69，不是控制流复杂度 | 204-65根因标为待验证，按实际调度证据定位 |
| 本地缺`ap_int.h` | 仓库已有该头文件；完整可用工具链仍未验证 | 区分头文件存在、C++可编译、Vitis可综合 |

写回索引常量化与4×4周期改善存在对照证据，但未取得对应完整关键路径对比；不把它推广成16×16或所有版本的唯一时序根因。

## 3 TAPA启发如何落到本项目

本地读到的是TAPA-CS论文；导师描述的FPGA内部粗粒度规划和长连接流水，与TAPA/AutoBridge的工作更直接相关。需要区分论文给出的工具流程与我们准备采用的工程方法。

TAPA的相关流程先综合计算task取得RTL和资源信息，再进行粗粒度floorplanning，随后生成/调整通信流水和物理约束。它不等于给现有C++全展开函数加几个`PIPELINE`就自动获得物理感知。我们的第一版采用手工反馈闭环：

```text
源码中的块边界与状态归属
    → HLS得到块RTL、面积、单元II与反馈延迟
    → Vivado粗粒度布局与关键路径
    → 估计跨区连接需要的流水级、缓冲和到达对齐
    → 回到代码/通信RTL修改
    → 重新HLS、CoSim与实现验证
```

采用三条原则：

1. **状态靠近反复使用它的计算单元。**不让`score_acc`每个feature都在远端控制器与MAC之间往返。C++中声明在同一函数不足以证明物理就近，最终检查寄存器、运算单元的位置和路径。
2. **优先流水化前向通信。**K/V广播、任务头和输出汇聚允许增加传输延迟；数据、mask、索引和控制标记必须一起对齐。循环累加路径则先算延迟与依赖距离，不能盲目插级。
3. **先按逻辑局部性分块，再按资源规划位置。**粗粒度区域提供边界，寄存器级数必须由路径和吞吐需求校正；不预设固定跨SLR拍数，也不强行让4×4跨SLR。

TAPA的almost-full/容量管理提示：存在在途数据时，满信号必须留出吸收在途数据的空间。实现优先使用具有完整背压的标准弹性通道；自定义固定延迟链必须计入反压传播时间和在途token，不能只增大FIFO后宣称安全。

反馈环不能任意增加延迟；原TAPA也讨论了不能满足延迟平衡约束时限制反馈环的区域分配。因此“物理流水”同时是通信设计和调度设计。

## 4 统一的调度和性能记账方法

令`H=HEAD_DIM`、`D=PE_DIM`，逐feature执行的QK需要H次阵列求值，不是仅`H/D`次。D=16、H=128时仍是128次调用；旧文档的“8×8=64拍”漏算每个feature的执行，不能作为tile性能基线。

对每个阶段记录：调用数、有效PE操作数、单元Final II、循环Final II、流水填充/排空、DMA等待、反馈等待及完整阶段cycles。简单无停顿流水近似为：

`阶段cycles ≈ 首次完成延迟＋(调用数−1)×实际发射间隔＋阶段收尾`

存在条件分支、背压、多个单元或阶段屏障时，公式仅作核对，以实际调度和RTL周期为准。QK为H次；PWL扫描8段；ROW_SUM为D次。PV按当前8上下文调度，PE调用数为D×H，另有每feature的Accumulator更新；不要把PE调用数和扁平循环迭代数混为一谈。

若一条真实反馈路径延迟为`L`拍、相同状态的循环依赖距离为`d`，调度需满足`d×II≥L`；还要满足资源服务间隔和调用协议等约束。PV的distance8只有在新增通信和写回延迟仍可被其覆盖时才足够。

空间分成N块，每块D×B个PE，`N×B=D`时，每次全局阵列求值仍对应D²个PE，不代表N套完整阵列。总PE数为D²、Accumulator数为D。吞吐比较必须包含所有块完成同一阶段的时间。

当前PV扁平循环总迭代数为`ceil(H/8)×8×(D+1)`：其中D×H次有效feature的阵列调用、H次Accumulator更新，其余为最后不满8上下文组的空迭代。默认16/128两档H均整除8；4×4/head16为80次循环迭代，16×16/head128为2176次。这不是整tile的总cycles，还包含之前的其他阶段、填充和DMA。

阵列调用不等于全部PE都做有效工作：ROW_SUM每次只采用选定key行的D个结果；PV每次也只采用选定行的D个query结果。QK还计算了随后被causal mask舍弃的元素。报告分别给出物理算术操作、被采用的操作与最终有效MAC，不把D²乘以所有调用数当作有效吞吐。

报告同时给出HLS估算、实际运行时钟和实现后WNS/TNS。`1/7.300ns`只是HLS组合延迟的参考倒数，不是已实现的运行频率。

## 5 阶段0：固定证据与修正文档

**输入：**本次核实的P3a日志和当前源码。**不启动新构建。**

- 以本文第2节替代旧交接中的当前状态推论，保留旧实验作为历史，不反复重试没有新增假设的失败路线。
- 下一轮构建记录精确版本、所有实际编译源文件/头文件指纹、参数、工具版本、官方命令和开始结束时间。用户同步源码后先核对再运行。
- 每轮结束，在下一轮重置build之前保存该轮原始日志、报告、RTL层次和关键实例/路径证据；官方zip只有流程成功后才生成，失败轮也要保存已有证据。归档不另建测试wrapper。
- 每个结论标注“已证实”“当前判断”“待验证”或“计划”，不靠历史HEAD或被覆盖的报告代替本轮证据。

**验收：**别人能从记录定位该次源码与各指标出处；缺失的物理实例和实现时序明确标为未验证。本文和根`PROJECT_CONTEXT.md`同步维护，其余历史快照不作为执行入口。

## 6 阶段1：修复测试判错能力与数值合同

**近期执行对象：4×4/head16。**修改既有`tests/stream/test_fsa_stream_split_d.cpp`，不新增testbench或测试脚本。

1. 对当前有界、期望有限输出的用例，先检查actual和reference是否有限，再比较误差；保留0.03误差门槛，并输出第一个异常坐标、数值及位模式。扩展输入前明确数值范围，不能把有限输入等同于中间永不溢出。
2. 保留已有单key、ones-V、feature0/末feature basis、causal/noncausal用例；增加L=D−1、D、D+1、2D、2D+1的边界覆盖，去重但不删除原诊断用例。4×4对应3、4、5、8、9；原L=7和16仍保留。
3. 在一次main中交替执行不同输入/长度/causal，验证顶层事务之间没有遗留状态；非法长度0和大于MAX的调用检查全部O内存canary，不只检查O[0]。无效长度不得触发数据访问。
4. 检查输出有效区之外的canary；对新增多tile用例比较每个query、每个输出feature，覆盖online重标定、尾tile及全局causal索引。
5. 将`finiteAccMax`现状与Chisel/CMP和算术合同对照。当前不能宣称忽略NaN；NaN/Inf传播、带符号零及相等值的选择策略确认后，才决定是否改硬件比较器。不能为了让测试通过擅自排除既有特殊值合同。
6. 增加能辨别online重标定的定向多tile输入：让后一key tile产生显著更大的max，V取可区分的值；另用零Q/K、ones-V和跨块位置的basis-V检查L/O更新及query路由。随机边界长度不代替这些状态路径覆盖。补PWL分段边界附近和有界负指数输入，保留既有0.03门槛。

**验收：**官方完整流程CSim与CoSim均通过；新增边界事务完成C post-check；非有限输出不能再被误差比较漏报；有效输入状态为0，无效长度状态为1且O保持canary。测试台变化不能改变综合硬件；其顶层资源、II和估算周期应与P3a一致。

扩展测试后事务数和CoSim平均值会变，不能继续使用“9/9”“7/7”或混合平均latency作跨版本性能指标；保留同名、同输入有效事务的逐项周期。

**两种正确性证据分开：**既有reference是double点积＋精确exp的数学attention参考，用于0.03算法误差验收；它不是当前混合精度/PWL的位级金标准。结构重构前通过同一官方入口取得各固定输入的原始O位模式，在既有testbench中保留可核对的基线期望，后续重构逐word比较，并继续检查独立数学参考。基线快照只证明行为未变，不单独证明基线算法正确。不能把两个均在0.03以内的输出写成位级一致，也不新增第二个被测顶层或临时测试台。

新检查在CSim和CoSim两种执行中都启用，不使用综合宏隐藏失败；原始输入/输出快照、版本、参数与用例ID一并保存。非法长度的“大于MAX”采用MAX+1，不为该非法长度分配或打包超界输入；先复用合法大小的Q/K/V/O缓冲。

## 7 阶段2：补齐单阵列证据，必要时修正共享层级

**输入：**阶段1通过的4×4构建。先检查，再决定是否改代码。

- 展开顶层RTL实例树，核对全部`runPeArray`实例路径、每实例中的RawFMA数、Accumulator数，以及各阶段对哪个实例发请求。模块类型数与实例数分别统计。
- 交叉核对HLS运算符报告、RTL算术单元、DSP/LUT/FF资源和调度。当前PE尾数乘法显式绑定DSP/latency1，FP32尾数乘法绑定DSP/latency2；外围对齐、舍入和选择逻辑仍使用LUT/FF，FP32乘法可能使用多个DSP，且还有FP32减法通路。DSP40不能单独倒推16个PE，独立RawFMA顶层的latency也不能代替其内联进阵列后的调度结果。
- 同时检查`reciprocalColumns`展开后的每列倒数通路、`cvtAtoE`转换单元、减法器及PWL前后处理；不能只统计两种FMA而漏掉共享或复制的辅助资源。
- 若物理上确为16PE＋4列Accumulator且各阶段共享，保留现有层级；仅更正误导性注释，清理无调用的`peBankMacUnit`、`stagePeArrayResult`和无效bank配置。删除前核对`stageAccumulatorResult`仍在使用，不能一并删除。
- 若发现复制，先做最小层级修正：让PE调用落在同一综合共享域，例如内联QK/ROW_SUM包装并核对自动outline；`ALLOCATION`只作为约束，实例树作为结论。一次候选只改变这一个原因。
- 若最小修正不稳定，再改为有限状态执行器：各阶段准备操作数，在同一个静态求值位置使用唯一阵列，结果就地更新状态。阶段调度保留QK/ROW_SUM反馈等待和PV交错；不重新接入历史同步请求/响应task闭环。

**验收：**总共16个PE RawFMA和4条Accumulator RawFMA，无阶段专属副本；完整S/P只有一份；阶段1全部用例通过；QK/ROW_SUM有效发射间隔不差于5、PWL/PV不差于1，估算周期≤7.300ns。若改为单一执行循环，报告其循环II和各阶段有效发射间隔，不能以插空的II1掩盖阶段吞吐退化。

4×4的DSP40作为资源回归目标，但仍逐项解释资源映射；任何增加先查复制及绑定变化。LUT/FF/BRAM变化记录来源，不把增加寄存器误称零成本优化。

## 8 阶段3：建立空间计算块，保持总资源和数值顺序

**首选分法：按query列切分。**当前`row=key`、`col=query`，并不是按当前物理行切分query。4×4先试B=2，即两个4×2块；单块拥有其2列的完整key方向。N=1作为对照，N=2为第一候选。

| 内容 | 所有者/连接 |
|---|---|
| `reg`、`score_acc`、PWL命中标记、score mask | 对应PE所在块 |
| 每query的running_max/sum、history_valid、output_acc、PV上下文 | 对应query列所在块 |
| 每列Accumulator与列内key归约 | 同一块；保持原顺序 |
| 每列倒数计算与最终归一化 | 同一query块；全阵列共D条倒数通路，FP32归一化仍复用该列Accumulator |
| Q tile | 路由到所属query块，不向所有块复制完整Q |
| K/V tile或其流 | 各块需要相同key数据，显式广播；缓存复制与端口开销计入资源 |
| 阶段号、tile索引、active数、causal信息 | 与数据对齐的控制token |
| O数据 | 各块负责不重叠query输出，汇聚后由既有O AXI写回 |

```text
既有DMA/分发
    ├── Q列0..1、K/V、控制 → 4×2块0：PE状态＋本地max/sum＋2列Acc ──┐
    └── Q列2..3、K/V、控制 → 4×2块1：PE状态＋本地max/sum＋2列Acc ──┤→ O汇聚
```

这只把16PE划成8＋8、4列Acc划成2＋2，不引入两套4×4阵列。不同query不互相做softmax归约，因此分块后无需跨块合并max/sum，也无需改变FMA顺序。

### 8.1 参数、索引和状态生命周期

新增块宽B和块数N；D始终表示原全局token tile边长，H表示head维。第一版要求`0<B≤D`且`D%B==0`，块ID为静态常量。不能把`PE_DIM`或生产全局`SA_COLS`改成B，不能用B重算attention scale、DIM_BLOCKS、query/key tile数量、AXI depth或PV key行数。

块b的列偏移为`g=b×B`，局部query c映射为`query_base+g+c`。有效query数为`max(0,min(B,active_queries−g))`，实现时先判`active_queries>g`再做无符号减法，防止下溢。全局`query_base=query_tile×D`、`key_base=key_tile×D`；score有效性仍为key/query均有效且全局key≤全局query。所有query子块共用原`key_tiles`范围，mask后无贡献也不能漏消费广播token。

Q缓存为B×H，K/V仍各为D×H。PE与mask为D×B，online状态为B，输出缓存为B×H，PV partial为8×B。查询tile开始时清online状态；每key tile开始清PE/命中标记/相关临时和；同一query的L/O必须贯穿所有key tile。最后先求倒数，再按原feature顺序归一化和写回。

当前K/V在每个query/key tile内各读取一次；第一版保持这些外存读取次数。各块自行重读同一K/V会放大N倍流量，不属于原条件下的布局优化。使用共享缓存还是复制本地K/V缓存需单独记账；保留有限tile缓存作为第一版，不能把当前多阶段、重排访问的数组直接替换成一次性FIFO而不说明保存和重放方式。

### 8.2 数值操作顺序

按代码保留以下链条：QK按H个feature顺序累加FP32 S→在未下转的S上求FP32 max→S经`cvtAtoE`到FP16→PE SUB_MAX取FP16输出→PE SCALE再取FP16输出→8段PWL命中后保存同一个FP16 P→ROW_SUM和PV消费这一份P。PWL扫描期间`reg`保持X，只有S已不再需要时才把命中P暂存在`score_acc`，不能提前覆盖它。

alpha沿现有FP32列Accumulator的缩放/PWL链生成，不换成PE的FP16 PWL路径；保留无历史alpha=0、有历史但本tile无有效key时alpha=1。L更新为融合的alpha×旧L＋row_sum，O为融合的alpha×旧O＋PV，最终归一化为倒数×O＋0。不能合并量化步骤、交换max与下转、换成普通乘法加法或逐次重算P。

实现优先让每块具有可辨认的RTL边界和唯一状态更新者。可用静态块ID/模板区别有意存在的空间块；每块内部各阶段继续共享同一套PE。不能通过全阵列参数和mask让多个块各综合一套完整阵列。

第一版先采用有限、确定的分块调用验证功能与实例数；是否实现块间并发由实际调度决定，不能把顺序调用写成空间并行已成立。若增加并发通信，采用“输入分发→块内完整执行→输出汇聚”的前向图，避免外部控制器发请求后等待结果再继续给同一块供数的控制环。

块接口以query/tile头和有限数据序列为单位：头中给出query/key基址、有效数、causal及结束标记，各块据此计算相同广播序列的消费次数。被mask屏蔽的数据仍按协议接收后丢弃，不能各自跳过读取而让后续token错位。输入分发可以反压等待各块，但继续供数不能依赖同一tile的计算结果回传；块内负责阶段推进和历史状态，输出标识query归属。块函数只接收本块宽度的Q/状态接口，不把D×D全阵列接口复制到每个块。

### 8.3 并发通信的等待关系

前向进程图本身不足以证明无死锁。广播器有多个输出，汇聚器有多个输入；若广播在块1的下一tile输入上阻塞，而块1的输出又因汇聚器等块0被堵住，块0还在等广播提供剩余数据，仍会形成循环等待。并发版本必须列出生产/消费顺序、有限缓冲和等待关系，不能靠统一加大FIFO作为证明。

第一版保持同步的有限tile数组调用，确认数值与资源后才增加并发。并发时输入按一致的tile序列供数，输出汇聚独立推进，能够接收已就绪块的完整query输出并按其全局query地址写回，避免固定等待未就绪块饿死其他块；若坚持固定输出顺序，明确证明所需缓冲足以吸收该顺序下的在途输出。只有一个O AXI写入者，并等待全部有效query写回和必要写响应完成后顶层置status=0。空query块仍遵循协议消费输入，但不写无效O。

**验收：**阶段1用例与未分块版本数值一致；总PE16/Acc4；块内状态没有从远端逐feature读写；无完整S/P复制；证明两块同时有效时的实际调度。阶段周期、FIFO/缓存容量和总资源全部可解释。此步允许记录分块引入的开销，但没有通过原性能门槛的候选不能替代已验收基线。

## 9 阶段4：通信流水与粗粒度floorplanning闭环

**输入：**阶段3的空间块边界已验证。此阶段包括新RTL/约束和Vivado执行，进入时单独明确执行范围。

### 9.0 实现载体与证据等级

当前Tcl的`EXPORT_IP=0`，尚无Split-D IP或集成工程。物理实验先在获授权后为正式Tcl启用IP导出，仍执行完整CSim/CSynth/CoSim，不用其他Tcl跳过步骤；Vivado工程单独版本化记录IP指纹、约束、时钟、复位和接口环境。

若先做OOC实现，明确采用的边界时钟、输入输出预算和被分析的路径，它只证明这些边界条件下的局部实现。随后完整集成须保留真实AXI访问/背压和控制协议，未连接端口或缺失I/O时序约束的结果不能写成板级100MHz已通过。对照之间固定同一集成载体；不把四路AXI直接当作开发板外部引脚，也不以小容量AXI RAM测试包代替完整序列/带宽验收。

### 9.1 先取物理证据

同一块结构先跑自动布局，检查关键路径、net delay、扇出、拥塞和SLR跨越。然后根据实际块面积、DSP/BRAM分布和DMA位置划粗粒度区域，确保局部反馈在块内。Pblock边界与实例路径来自生成物，不在文档中臆造坐标或SLR资源容量。

4×4体量可能不足以体现跨区问题。若自动布局已经紧凑、关键路径主要在RawFMA内部，接受“该规模物理感知收益不明显”的结果；不要强制跨SLR来制造收益。此时先完成方法验证，扩大规模的收益留到阶段5。

### 9.2 在真正的远连接上插级

- 优先对象：K/V前向广播、高扇出模式/阶段控制、块到输出汇聚。广播可用局部分发树，数据与对应mask/索引/阶段/valid同步推进。
- 每条边记录位宽、每tile token数、生产/消费顺序、流水级数、有效容量、背压协议和结束条件。遇到输入停顿、输出反压时，未被接收的payload和标记必须保持一致。
- 合流处按事务与索引匹配；若使用固定周期对齐，分别核对各支路真实延迟。阶段切换前排空前一阶段在途数据，禁止结果写入下一tile的状态。
- 通信寄存器接收新token以握手为准；阶段/模式信号不能先于对应操作数变化。共享PE或Accumulator在切换用途前确认相关结果已提交，使用固定计数或有效标记说明这一屏障，不靠软件调用先后假定RTL已排空。
- 不把所有连接统一加固定级数。根据关键路径选择级数，并重新计算完整反馈延迟，尤其是QK、ROW_SUM和PV distance8；反馈不能被现有距离覆盖时调整局部调度，不声明假依赖。
- 若采用自定义非弹性流水，缓冲容量须覆盖在途token和反压传播期间继续到达的token；用时序图证明容量。优先沿用标准背压接口，避免自制不完整的almost-full协议。

在既有testbench增加长短事务交替、尾tile和阶段切换诊断；读取官方CoSim已有AXI握手/波形核对停顿与排空。若该流程没有覆盖可控反压场景，明确记录这一缺口；不要未经授权另建wrapper或故障注入testbench。

### 9.3 比较设计收益

| 对照 | 目的 |
|---|---|
| 未分块P3a | 功能、周期与资源基线 |
| 分块＋自动布局 | 隔离块边界本身的影响 |
| 同一分块＋粗粒度布局约束 | 隔离floorplanning影响 |
| 同一分块/约束＋距离感知通信流水 | 检查新增流水的时序收益与周期成本 |

各对照固定器件、接口、时钟、输入和物理PE/Acc总数；记录实现设置及seed，不以一次幸运布局概括普遍收益。

**HLS/RTL验收：**结构和全部功能用例通过；目标阶段有效发射间隔不退化；无死锁、遗漏或重复输出。**物理验收：**在固定100MHz和对应时序约束下，route完成，setup的WNS≥0/TNS=0，hold的WHS≥0/THS=0，必需路径均有约束，例外路径逐项说明，并核对DRC。确认局部反馈位置以及已规划长边的流水落点。OOC通过、完整集成通过和板测通过分别报告。

同时计算相同有效事务的`cycles/实际运行频率`。达到时序但额外等待使端到端变慢，不能报告性能优化成功；应保留为物理可实现性结果并继续查开销。若都在100MHz运行，cycles下降才直接意味着该事务加速；提高运行频率需另设实验，不能由HLS估算倒数替代。

## 10 阶段5：恢复16×16与扩展研究

**当前不执行。进入条件：用户恢复16×16方向，4×4功能/单实例证据完备，分块方法已通过验证或明确失败原因。**

1. 先用当前P3a的16×16作待测对照，不预设latency3或PWL/PV II1；复核`PE_ARRAY_II`、调用协议和实际服务能力。官方Tcl目前不接收PE_ARRAY_II环境变量，不能只设一个未传入CFLAGS的变量假装实验生效。
2. 将阶段1边界用例参数化用于D=16：15、16、17、32、33，覆盖causal/noncausal、完整tile、尾tile和连续事务。保留原1/2/5诊断；若原MAX配置允许，继续检查非法长度边界。
3. 比较单块16×16与空间query分块，例如4个16×4块；总PE256、Acc16。B值由综合面积和区域容量确定，不预设恰好一块一个SLR。
4. 若204-65再现，定位到具体函数、循环、展开规模、端口和调用层级；优先缩小单块调度范围。128PE时间复用两次不作为256物理PE方案。若分块仍失败，保留失败日志再讨论更大架构改动。
5. 同时验收QK/ROW_SUM有效II5、PWL/PV有效II1、估算周期≤7.300ns及实现后100MHz时序。若目标不可兼得，按真实结果报告未达标；改变指标属于新决策，不能私自用II5替代PWL/PV II1。

保留原16×16完整流程的60分钟CoSim硬超时与20分钟无进度提前停止规则。但原15–45分钟经验来自7事务，不适用于扩展测试后的事务数；新增用例导致超时时标为未验收，后续由用户决定是否调整时间预算，不能删用例规避超时。

多个完整小FSA并行处理不同query块、HBM供数和多SLR扩展是后续研究层，不属于当前“同一套D×D阵列内部空间分块”的资源合同。必须另设总资源与带宽预算，不把复制多个完整核混进上述公平对照。

## 11 执行入口、验收清单与失败处理

### 11.1 官方入口

在用户同步精确候选版本、授权对应远端轮次并加载Vitis环境后，4×4执行：

```bash
./run_hls.sh fsa_stream_split_d
```

16×16只在恢复该方向后执行：

```bash
FSA_SPLIT_D_PE_DIM=16 FSA_SPLIT_D_HEAD_DIM=128 ./run_hls.sh fsa_stream_split_d
```

保持官方CSim→CSynth→CoSim流程，不通过替代Tcl、wrapper或临时testbench跳过步骤。本计划未执行以上命令。IP导出、Vivado工程与实现约束属于物理验证阶段新增工作，不能把当前未导出IP写成已完成。

### 11.2 每阶段的交付物

| 验收维度 | 必须交付的证据 | 失败时的下一步 |
|---|---|---|
| 版本一致 | 编译输入指纹、参数、工具和命令 | 修正版本对应，暂停解释性能 |
| 功能 | 各有效用例误差/有限性、状态与canary、C post-check | 定位首个事务/坐标，不放宽误差 |
| 物理结构 | RTL实例路径、算术单元数量、状态存储及资源分解 | 修共享/块边界，不用总DSP猜结论 |
| 调度 | 各阶段调用数、Final II、反馈距离、有效发射间隔和cycles | 区分资源服务、反馈、端口或控制停顿 |
| 物理时序 | 实现载体、完整约束、route/DRC状态、setup与hold、关键路径/跨区/拥塞 | 按关键路径改通信或局部计算 |
| 实际性能 | 同输入有效事务cycles、实际频率、总时间、面积成本 | 拆解填充/排空/DMA/屏障开销 |
| 可验证性 | 完成事务数、耗时、死锁诊断、最后进度 | 超时或中断写未验收，不写Pass |

一次候选只验证一个主要假设，顺序为：测试补强→共享证据/必要修复→query分块→布局反馈→长边流水。前一步未达条件，不叠加下一步掩盖问题。原则上不新增削减算法的临时顶层；调度消融若确有需要，先明确范围，再通过正式源码版本和同一入口执行。

## 12 首个实际修改包与资料依据

下一次代码工作先完成阶段1：修复既有testbench的非有限输出漏报、canary覆盖和4×4边界用例；同步准确注释，不先改变RawFMA、阵列规模或引入通信task。其后用该次构建补阶段2实例证据；只有发现复制或开始空间分块时，才进入结构修改。

主要源码位置：

- [控制器与在线状态](C:/Users/30130/Desktop/workstation/FlashAttention/FSA_HLS/src/stream/split_d/split_d_controller.cpp)
- [阵列求值与QK/ROW_SUM](C:/Users/30130/Desktop/workstation/FlashAttention/FSA_HLS/src/stream/split_d/split_d_compute.cpp)
- [配置和遗留宏](C:/Users/30130/Desktop/workstation/FlashAttention/FSA_HLS/include/fsa/stream/split_d/split_d_internal.hpp)
- [既有testbench](C:/Users/30130/Desktop/workstation/FlashAttention/FSA_HLS/tests/stream/test_fsa_stream_split_d.cpp)
- [官方Tcl](C:/Users/30130/Desktop/workstation/FlashAttention/FSA_HLS/hls/fsa_stream_split_d/run_hls.tcl)

资料与适用边界：

1. [TAPA原论文](https://zhenman.github.io/files/J14-TRETS2023-TAPA.pdf)，重点为floorplanning、通信流水、延迟平衡和反馈环约束。本文采用其方法启发，未承诺直接迁移到TAPA工具链。
2. [本地TAPA-CS论文](<C:/Users/30130/Desktop/高阶实验/论文调研/TAPA-CS _ Enabling Scalable Accelerator Design on Distributed HBM-FPGAs.pdf>)，多设备/分布式HBM的扩展背景，不能直接代替本项目单FPGA物理证据。
3. [AMD UG1399：Function Inlining](https://docs.amd.com/r/2024.1-English/ug1399-vitis-hls/Function-Inlining)，同层级共享要求。
4. [AMD UG1399：Pipelining Paradigm](https://docs.amd.com/r/2024.1-English/ug1399-vitis-hls/Pipelining-Paradigm)，latency、II与总体周期的区别。
5. [AMD UG1399：Array Partitioning](https://docs.amd.com/r/2024.2-English/ug1399-vitis-hls/Array-Partitioning)，arrays引句解释RAM端口争用，不能用于断言204-65根因。
6. [SWAT原文](https://arxiv.org/html/2405.17025v1)，支持数据就近、row-major和K/V驻留方向；不证明旧交接D×P方案符合我们的物理PE与数值合同。

以上是方法依据；阶段3—5的收益和可行性仍是待验证问题，不是已有实验结果。

## 13 第二次代码对照核查记录

### 13.1 核查范围

按正式Tcl编译输入核查全部9个源文件：Split-D顶层、controller、compute、DMA四文件，以及`arithmetic.cpp`、`fp32_raw_fma.cpp`、`pe_raw_fma.cpp`、`dma.cpp`、`accumulator.cpp`。关联检查Split-D四个头文件和stream层types/config/arithmetic/dma/accumulator/state/control等定义、既有Split-D测试和两个RawFMA测试、Tcl与shell入口。相关参考检查Chisel的FPArithmeticImpl、CMP、Accumulator及ExecutionPlan，生产streaming的状态/通信实现用于对照。

未将第三方库、旧IP生成RTL、本地历史可执行文件或被覆盖报告当作当前Split-D的实现证据。`src/core`与生产streaming不是本顶层的全部编译输入；已检查相关参考位置，但本轮没有声称对整个仓库所有独立模块逐行审计，也没有运行它们的回归。

### 13.2 代码对应和新增修正

| 代码位置 | 直接事实 | 方案修正 |
|---|---|---|
| `split_d_controller.cpp`：query/key外层循环 | tile步长D，key/query有效数及causal使用全局索引 | 分块新增B，保留D/H及全局坐标，增加无符号下溢保护 |
| `split_d_controller.cpp`：QK至PWL | FP32 max在cvt前，SUB_MAX/SCALE逐次取FP16结果，PWL用score_acc暂存 | 阶段3写明量化和状态覆写顺序 |
| `split_d_controller.cpp`：alpha与L/O | alpha用FP32列路径；L/O由同一列FMA融合更新 | 不用PE PWL或普通mul＋add替代 |
| `split_d_compute.cpp`：reciprocalColumns | 按列UNROLL内联恢复除法，零分母输出0 | 每块拥有其query倒数通路，总D条，加入实例/资源统计 |
| `split_d_controller.cpp`：PV | 8个feature上下文，D次key行调用后有8次列更新 | 补精确迭代数与有效/无效操作区分 |
| `split_d_dma.cpp`与`dma.cpp` | row-major token/H布局，QKV每word4个FP16，O每word2个FP32 | 保持外存布局和K/V读取次数，O单一写入者 |
| `fsa_stream_split_d.cpp` | 正常status在run返回后置0，非法长度直接返回，允许AXI拓宽上限512 | 无效长度复用合法缓冲；实际端口宽度查报告；并发版完成所有写回才置0 |
| `arithmetic.cpp`及两个RawFMA实现 | 当前调用RawFMA而非旧peMac/peExp2PWL；尾数乘法绑定DSP，转换和PWL合同各不同 | 固定实际算术路径；实例/资源验收覆盖辅助通路 |
| `test_fsa_stream_split_d.cpp` | double/exp参考，误差门槛0.03，无非有限判错；输入有界 | 独立算法误差＋重构位模式基线双重检查，补定向重标定用例 |
| Chisel `FPCmpUnit` | 根据FMA计算a−b的结果符号选择max，没有显式忽略NaN分支 | 不把NaN策略解释成IEEE maxNum或声称已完整一致 |
| Chisel `FPMacUnit`与HLS `peRawFma` | Chisel分别从raw结果舍入至两种精度；HLS先产生FP32位模式再转换FP16，已有PE测试遵循后者，FP16次正规输出FTZ | 保持当前已验收HLS合同；与Chisel的舍入/特殊值完整一致性仍需定向证据，不能用结构重构顺带修改 |
| `local_math_stubs.cpp`与Tcl | local stubs有独立宏，正式9文件列表未包含它 | 历史本地exe或stub结果不能充当本轮HLS验证 |
| 生产`dataflow.cpp`/DMA与历史死锁记录 | 独立Q/K/V actor和有限FIFO存在消费顺序约束 | 新广播/汇聚必须查等待关系，前向图不等于无死锁证明 |
| `run_hls.sh` | 成功后才替换最终目录和zip，失败时可能留旧成功目录与新临时build | 失败轮查本次临时build和日志，不能读旧最终目录冒充新结果 |

### 13.3 核查结论

query列分块的算法独立性成立：每列只依赖自己的max/L/O，拥有完整key方向可以保持原key和feature顺序。具体HLS共享、并发调度、通信活性及物理收益仍需上述阶段验收。阶段1、2可作为近期修改包；阶段3—5须按条件逐步进入，不能把本次静态核查写成可综合、无死锁或时序已达标。

新增资料依据：[AMD AXI端口拓宽上限](https://docs.amd.com/r/2024.2-English/ug1399-vitis-hls/config_interface)、[AMD OOC边界约束](https://docs.amd.com/r/en-US/ug903-vivado-using-constraints/Out-of-Context-Constraints)、[AMD I/O时序上下文](https://docs.amd.com/r/en-US/ug903-vivado-using-constraints/About-Constraining-I/O-Delay)。后两项作为约束原理参考，实际Vivado2024.2工程命令与报告仍以所用工具核对。
