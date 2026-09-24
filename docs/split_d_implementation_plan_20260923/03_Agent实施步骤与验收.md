# Agent 实施步骤与验收

以 02 的协议为准。每阶段完成其直接验证后再进入下一阶段；不得把“文件已生成”当作对应硬件已通过。下列目标文件为未来实施建议，本次没有在仓库创建它们。

## P0：固定基线与构建输入

1. 阅读仓库 AGENTS、PROJECT_CONTEXT、01、02。核对本目录 source_manifest；若主路径变化，先更新审计再实施。不要进行 Git 操作。
2. 在允许的独立工作副本/构建目录实施新 top，保留 `fsa_stream` 为对照。不要改 WEEK 5 副本。
3. 准备仓库所需 Vitis headers；当前副本缺 ap_int.h。选择工具已安装的 include 路径或项目约定 third_party 路径，记录来源/版本，禁止用临时假 ap_int 模拟“C++通过”。
4. 运行现有 stream 的默认小配置 C++ 回归、transport regression（启用其宏）和直接 Raw FMA 测试；从测试文件确认真正执行的用例数，保存编译命令和输出。没有环境则明确 blocked 项，继续可做的代码/模型工作。
5. 需要 HLS 时重建当前 top，10ns、uncertainty2.7ns、当前 VU37P；构建产物保存源码 hash。报告中的7.300ns只作历史参照。旧 `run_hls.tcl` 的 RUN_COSIM=1 不自动授权执行它；新建流程默认关闭 cosim/export。

完成证据：基线源码清单、编译日志、测试数量、可用工具/缺失项清单。不得拿旧 fsa_dma 综合报告代替。

## P1：参数/类型/布局，先不接入 SA

建议新增：

```text
include/fsa/stream/split_d/split_d_config.hpp
include/fsa/stream/split_d/split_d_types.hpp
include/fsa/stream/split_d/split_d_common.hpp
include/fsa/stream/split_d/split_d_pe.hpp
include/fsa/stream/split_d/split_d_processes.hpp
src/stream/split_d/*.cpp
tests/stream/test_fsa_stream_split_d.cpp
hls/fsa_stream_split_d/run_hls.tcl
```

头文件 guard 遵循项目命名规则，用上述唯一文件名避免与旧guard冲突。公共符号采用 `fsa::split_d` 隔离，不同时包含 legacy core config/types。不得用全局搜索替换 SA_ROWS→HEAD_DIM。

首版推荐保留旧scalar算术文件，沿用 `fsa::elem_t/acc_t` 和RawFMA输入输出类型；它们关联的旧config仍以物理R/BQ编译，新namespace单独定义逻辑D、地址和tile参数。新actor不得借用旧 `sram_address_t`、`TileMeta` 中不够宽/不够表达的字段或旧维度化向量。复制/重构的应是参数化actor，非另写一套浮点算术。若之后抽取独立共享数值头，作为单独重构并跑旧top回归。

新增 `splitDAttentionScaleAcc/Elem`，SA SCALE与Acc alpha都调用它；用源码搜索确认新路径没有调用旧 `attentionScale()/elemAttentionScale()`。原helper仍依赖SA_ROWS，即使新config定义了HEAD_DIM也不会自动正确。

逐项列出每个旧常量的新来源：

| 用途 | 使用参数 |
|---|---|
| 物理 PE 行、局部向量宽、subbank | R |
| PE列、query步长、Acc行宽 | BQ |
| key步长、active_keys、P有效行 | BK |
| QKV外存stride、输出stride、scale、Acc容量 | D |
| d/V循环 | ceil(D/R) |
| Delayer长度 | 各phase的BQ或BK加R−1 |
| 每KV结果token | D+2 |
| query的KV数量 | 02第9节公式 |

实现 address、word/lane packing、mask、tile count 的纯函数。选取 `(D,R,BQ,BK)=(4,4,4,4),(5,4,2,3),(17,8,4,8),(128,16,16,16)`，对所有有效/填充元素枚举验证唯一地址、无越界、反变换一致；这些是新增协议的必要检查。

验收：外存tail与片上tail不混淆；768行示例容量正确；D1/D5的每token补齐正确；静态非法R/BQ/BK编译失败；请求最大值/地址位宽覆盖上界。

## P2：只实现 PE 的 split-QK，不做 softmax

1. 从当前 `spatialPeCell`、peMacUnit、pe_raw_fma 的真实调用链复用 MAC，不调用旧未使用的浮点 helper。
2. 将 reg、score_acc、邻接 link、result槽、tag/busy放入每PE的状态。先R4/BQ2/BK3/D9。
3. 按02第3节实现 seed向下、QK向上、结果返回owner；一key wave完成再发下一key。规定 partial初值是oldscore，链末赋值。
4. 同时明确 QK feature有效和score key有效两种mask；BK<R时高行仍参与feature乘加。
5. t递减、r递减，g≥D旁路；收到当前d所有回写才重装Q。每KV仅清score一次。
6. 删除/不移植 `pe_register inter false` 等旧微程序限定pragma。保留真正的RAW/WAR，若工具II增大先接受。

验证：逐 `(key,query)` 比较完整D点积；使用feature编码pattern查反转/错行、随机正负、跨块抵消、Dtail；另存每d score trace。注入“每d清零”“把种子结果再加old”“提前转half”“最后d判错”为负例，测试应失败。

综合核对：PE Raw FMA 实例数R×BQ，新增score为FF，不增R×BQ个float加法器。若score为RAM或调用多次同函数被复制多个MAC，先修正再集成。与当前结构估算不同的DSP必须追到实例，不凭合成总数猜原因。

## P3：CMP/PWL/P与PV

1. 在同一次KV处理内部接上全D结束屏障。保持CMP按列，scoreFP32参与max，之后才castS16。
2. MAX_DIFF新增alpha_mode/有效信息，无历史=0、有历史但空tile=1，普通情况compute；避免inf差值。
3. 原reg按S→N→X→P复用。PWL命中处理保持X直至匹配结果返回，拒绝迟到重复写。
4. ROW_SUM只运行一次，数据取已落在reg的P16；不从未量化exp临时值另算L。
5. 按VT遍历所有D feature，使用当前R宽V slice；P冻结，PV结果带全局h。
6. 输出串行MAX、ROWSUM、PV[0..D−1]。第一次采用保守无冲突发射，后续增加并行需结果队列与仲裁。

验证：单key的O应等于该V（在当前算术容差内）；P快照跨VT不变；D>R时后半段输出非零；max在后续KV上升时alpha确实作用；BQ≠BK的causal mask；全mask内部测试产生0而不是NaN。分别检查近似softmax oracle和FP64误差，不能仅看最终宽松误差。

## P4：Scratchpad/DMA/Delayer与完整数据流

1. 以当前 dmaQ/K/VRequestProcess 为模板，新请求函数不互相等待；Q每query一次，KV每tile一次，完整D分写片上多行。
2. 保留单 scratchpad owner，按02定义地址、双buffer状态与request_id。检查transfer_last只出现一次且在完整tile末尾。
3. 修改Q跨KV重放为ND次Q slice重放。K/V分别在ND/NV所有slice发出后释放，不能提前用ping-pong覆盖。
4. 输入协议改为tagged slice；input delayer按phase实际行数工作，bubble也带标签。输出delayer改为D+2，作为合法token转发器。
5. saExecutionPlanProcess以描述符表达阶段；旧周期脚本不与新逻辑混跑。不要复用旧cycle计数上限导致截断。
6. AccumulatorProcess扩成D+1行、D+1更新事件、D维归一化，校验kind/index。FP32算术和reciprocal继续在同一actor内。
7. O writer更新ceil(D/2) stride、tail lane与depth；invalidL时输出不变。
8. 仅在独立新top绑定四AXI bundle；先保持64位，max_widen上限不改为实验变量。生成工具报告核实实际宽度。

用带累计计数的testbench记录每actorsend/recv、每tile tag、每种token、外存read/write地址。功能Csim的hls::stream常为无限队列，不能证明有限FIFO无死锁；用软件有限队列调度模型可提前检查结构，最终RTL cosim需获当前任务对应授权后运行。深度选小值测试2/4/8与目标值；禁止靠无限增深FIFO掩盖循环依赖。

完成证据：top端到端数值/地址canary/计数通过；每query Q读次数1；causal读取prefix正确；全D输出全部写回；无外部score/P RAM；不存在Acc反馈环。

## P5：测试矩阵及准入门槛

| 类型 | 最少配置/场景 | 判据 |
|---|---|---|
| 原路径回归 | 原默认4×4及transport配置 | 新增代码不改变旧top结果/构建 |
| D=R退化 | R4 D4，R8 D8 | 与固定算术/顺序的新oracle一致；比较旧top差异需定位转换/时序 |
| D分块 | R4 D8/12/17，R16 D128 | score与全D点积匹配；token数不随ND重复MAX/L |
| 矩形 | R8 BQ4 BK8；R4 BQ2 BK3 | Q/K stride、causal正确，BK<R不丢feature |
| L边界 | 0,1,BQ−1,BQ,BQ+1,2BQ+1,MAX_L,MAX_L+1 | 合法全覆盖；非法不访问内存 |
| D尾部 | D1,5,17,127 | padding隔离，每token stride正确，O尾lane0 |
| 数值 | max变化、抵消、PWL边界、FTZ、极小P | 位级stage检查；FP64误差分布，无未解释NaN |
| 生命周期 | 不同L/causal连续top调用 | 无旧score/P/history残留 |
| 回压 | 输入间歇、输出长暂停、不同FIFO深度 | 无丢重、无提前释放；统计等待 |

零输入允许精确检查均匀attention；一般最终结果使用与实际算术模型匹配的容差，不在未采样前规定“0.18就算通过”。FP64 Python标准样例误差仅应在数值roundoff级，不代表PWL硬件精度。

## P6：综合和局部物理结构

新增Tcl显式列出源文件，避免run_stream_test.ps1原有通配/包含top测试模式把两个top重复编译。复用数值底层 `.cpp` 可行，不能把全部legacy core都加进来。为不同参数建立不同build目录，构建目录在本次允许的输出根下，不用默认repo/build。

新Tcl配置：top=fsa_stream_split_d，part沿用当前，create_clock 10，显式记录uncertainty2.7，Csim/CSynth开启，RUN_COSIM/EXPORT_IP默认0。不要把time target或part变更混入split-d实验。

从小配置至16×16检查：

- DSP实例归属：每PE单Raw FMA、每列CMP/Acc，不额外PWL/score add；FP32 carry接入没有强制新的浮点算术核。
- FF/BRAM：score32位×BK×BQ（未用行优化可接受），reg16位×R×BQ；SPAD与Acc capacity正确、银行并行度正确。
- 层级/布局：score和link状态可追踪到PE；长seed/return链没有被综合合成全局组合总线；局部寄存器不是源代码命名假象。
- 时序：记录critical path、II、latency、stall，接受因正确依赖导致II>1，不人为写false dependence换II。
- 不对外层tile/d循环机械PIPELINE，使工具把多个stage/算术副本展开；空间R/BQ展开，时间ND/NV循环保留。

资源/时序不满足时依次处理局部mux、过宽tag、未用方向link、pipeline槽、共享算术绑定、RAM端口，保留数值和协议合同。精度变化、取消寄存器locality、削减D不是默认修复手段。

## P7：并行engine与实验（可选阶段）

E1稳定后依次E2、E4、E8；先独立KV，再共享KV，再区域共享。每次记录真实实例/资源和功能。E增加但吞吐不增时检查AXI有效带宽、广播最慢消费者、O仲裁、Acc瓶颈、布局拥塞，不能继续盲目扩展。

按04做floorplan和link pipeline的正交对照；物理层需可导出的RTL/IP，执行导出、cosim和Vivado流程前遵循仓库相应授权规则。此次用户请求是写方案，所以本次不运行这些流程。

## 完成交接

功能完成条件：全D正确、PE内score/P生命周期成立、FIFO/地址计数闭合、旧top回归保持。硬件完成条件：对应hash的综合、必要RTL验证和实现报告齐全。论文效果完成条件：至少一个04实验有公平对照的真实数据。三种“完成”分别记录。

最终交接应包含配置、hash、工具命令、实际运行范围、测试数量、失败配置、关键路径/资源变化解释、原始报告路径。更新项目上下文时修正过时状态，不追加互相矛盾的记录。
