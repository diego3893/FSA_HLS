# Split-D关键路径修复迭代

## 1 本次调用信息

- 开始：2026-10-12 08:50+08；两配置测试于10:22:16全部结束，HLS时序工作包通过，闭环1轮；最大迭代次数未设置。09:04用户明确恢复发布及CSim/CSynth。
- 用户要求：“继续修复关键路径。在时序通过之前不进行cosim”。本次恢复CSim/CSynth迭代；CoSim、IP导出、Vivado及板测均不执行。时序通过后先报告结果，不自动扩大范围。
- 本地：`C:/Users/30130/Desktop/workstation/FlashAttention/FSA_HLS`；远端：`FSA-FPGA-NM37-tailBox:/home/zhangchenxuan/FSA_HLS`；分支`fsa_split_D`。
- 起始提交：`931853de7f8a35748f227a3c5eed32f6993d3689`，已有ROW_SUM广播候选仅完成语法检查。本次先测此候选，不凭旧报告叠加新修改。
- 使用`fsa-hls-remote-iteration`及`maintaining-project-context`；Git/SSH沿用本对话授权，测试以本次最新要求为准。
- Vitis HLS2024.2、VU37P、10ns时钟、2.7ns uncertainty；固定两个query块和原精度/接口/算术顺序。

## 2 可验证标准

- [x] 4×4/head16官方CSim数学/严格24/24及非法长度保护通过；既有覆盖不收窄。
- [x] 16×16/head128官方CSim数学22/22及非法长度保护通过；1024长测仍留后续。
- [x] 两种配置完成CSynth，顶层估算周期≤7.300ns；读取全部关键路径和warning，不只检查命令退出码。
- [x] RTL实例核对总D×D PE、D条Accumulator，记录资源、循环/函数实际II，排除复制；吞吐II目标的未达项单独记录，不伪称已修复。
- [x] `RUN_COSIM=0 EXPORT_IP=0`，无CoSim/IP/实现/板测；待时序通过后由用户决定后续范围。

## 3 初始核查

- 本地已有`AGENTS.md`修改、`.tmprun/`及旧stub日志未跟踪，暂存为空；保留这些用户/无关文件。
- 远端HEAD5fda5a4、分支/origin正确，tracked干净；既有未跟踪工具日志/JSON不动。无Vitis/XSIM进程，689GB可用；4×4 IP快照及71025ff测量归档存在。
- 非交互`.bashrc`未使工具链进入PATH；显式source `.bashrc`后，再source已核实`/opt/Xilinx_2024.2/Vitis/2024.2/settings64.sh`，`vitis-run`可用。交互stdin核查输出不完整，未启动测试；改为非交互Bash加明确环境初始化，exit0。
- 旧71025ff关键路径为ROW_SUM load→operand_c select0.227ns→PE call7.163ns，总7.390ns。931853d已删除源级选择器，广播扇出/重新调度待本轮实测。

## 4 迭代总览

| 轮次 | 被测commit | 修改 | 本地检查 | 远端结果 | 状态 |
|---:|---|---|---|---|---|
| 1 | ebe529be6f4d03f855add5f3fe745af9874fa9d4 | 验证931853d的ROW_SUM广播，未叠加硬件修改 | 两种配置语法检查通过 | 4数学/严格24/24；16数学22/22；两配置CSynth7.300ns，实例数量保持 | 通过，闭环1轮 |

## 5 第1轮计划

先推送本日志和更新后的合同，精确HEAD同步后，以标准入口分别运行4×4/head16及16×16/head128；每个配置生成独立日志，先归档4×4结果再启动16×16。环境显式`RUN_CSIM=1 RUN_COSIM=0 EXPORT_IP=0 FSA_SPLIT_D_LONG_CSIM_TEST=0`。只用既有测试台和Tcl，不修改脚本或测试范围。

读取结果、分析已解决/剩余问题后才结束本轮。若时序未达标，先依据实际关键路径及官方资料选择下一处局部修正；保持真实依赖，禁止降低uncertainty、放宽误差或复制算术资源。

## 6 第1轮发布状态

- 本轮合同已提交1721ff4；计算源码仍与931853d一致。普通push被自动审批拒绝，理由为未确认GitHub目的地归属及本次具体内容授权；已向用户请求确认，确认前不重试推送、不拉取/启动构建。
- 当前未完成远端数据分析，不计已闭环轮次；此为发布授权阻塞，不是Vitis或DUT失败。CoSim/IP/实现/板测未执行。

## 7 本次停点与恢复动作

- 再次语法检查：4×4/16与16×16/128计算翻译单元均exit0，未执行testbench。
- 静态复核旧全部XML：超过7.300ns的模块均包含ROW_SUM路径；其他阶段QK/PWL7.272ns、PV7.299ns。旧数据仅定位，不证明新候选已达标。
- 已解决：合同及精确候选确定、远端环境/工作树/归档核对、本地语法检查。仍存在：1721ff4发布被自动审批拒绝，新CSim/CSynth未启动，时序未验收。
- 闭环轮次0，第1轮未完成（发布授权阻塞）；已向用户请求对现有GitHub目的地及本轮内容的明确推送授权，未绕过拒绝。
- 首个恢复动作：用户确认后正常push本轮提交，远端ff-only拉取、核对精确HEAD，再执行原定两配置CSim/CSynth；读完数据后才结束第1轮。CoSim/IP/实现/板测保持关闭。
- 当前日志末尾及PROJECT_CONTEXT阻塞状态留本地未提交，等待确认后同本轮发布记录一起整理；1721ff4为合同提交，未作HLS验收。

## 8 发布授权恢复（2026-10-12 09:04+08）

用户明确回复“允许推送并继续CSim/CSynth”，授权本轮内容发布到既有GitHub仓库fsa_split_D，再由NM37拉取运行两配置CSim/CSynth。此前审批阻塞解除；同一第1轮继续，非新增迭代。补记授权及当前日志后提交，精确被测HEAD以实际发布提交为准。CoSim/IP/实现/板测仍关闭。

## 9 第1轮实际启动

- 被测HEAD`ebe529be6f4d03f855add5f3fe745af9874fa9d4`，远端ff-only同步成功、tracked干净；其计算源码与931853d一致。拉取预检末行受PowerShell here-string末尾CR影响使test退出1；重新ls/HEAD核对两个归档确实存在且预检exit0。启动脚本显式去CR并补LF，不改变测试流程。
- 4×4/head16于2026-10-12 09:07:10+08启动，日志`/tmp/codex_split_d_ebe529b_4x4_timing_20261012.log`；本地SSH等待session60654。标准命令`RUN_CSIM=1 RUN_COSIM=0 EXPORT_IP=0 FSA_SPLIT_D_PE_DIM=4 FSA_SPLIT_D_HEAD_DIM=16 FSA_SPLIT_D_LONG_CSIM_TEST=0 ./run_hls.sh fsa_stream_split_d`。
- 每次显式加载.bashrc及Vitis2024.2 settings64.sh；原4×4IP与71025ff的16测量归档已保留。当前仅启动4配置，16待4报告保存后进行。

## 10 第1轮4×4结果（16配置尚待完成）

- 标准流程09:07:10—09:10:49退出0；官方CSim数学24/24、严格24/24、0 errors。CSynth顶层7.300ns、DSP40/FF45881/LUT128820/BRAM18K8，保留2个PE块、总16条PE乘法/4条Acc乘法/8个fsub；无200-871/1016/880/875。
- 相对bf41d16：LUT129412→128820（−592），FF45889→45881（−8），DSP/BRAM不变。仅HLS估算，不是新IP或板测验收。
- 247文件报告/RTL及源SHA归档到`build/split_d_ebe529b_4x4_timing_evidence_20261012`。收集脚本中循环变量name覆盖归档名，首份tar生成`/tmp/codex_fsa_stream_split_d.tar.gz`；内容正确、SHA fe9b575ad774099e7934ae2bbad88670d8c64842c69481c5c347c5cdd9c403cf、1207740bytes。仅修证据工具归档名变量，16使用独立名称，不覆盖这份4证据。
- 第1轮尚未闭环：4×4功能/时序/实例已通过，16×16待测试；CoSim/IP/实现/板测仍关闭。

## 11 第1轮16×16启动

2026-10-12 09:13:01+08启动，被测仍ebe529b，4证据已归档；标准命令`RUN_CSIM=1 RUN_COSIM=0 EXPORT_IP=0 FSA_SPLIT_D_PE_DIM=16 FSA_SPLIT_D_HEAD_DIM=128 FSA_SPLIT_D_LONG_CSIM_TEST=0 ./run_hls.sh fsa_stream_split_d`，日志`/tmp/codex_split_d_ebe529b_16x16_timing_20261012.log`，本地等待session61253。未启动其他测试。

- 4完整247文件归档已下载并逐SHA核对，92份综合输入与被测Git内容一致；Git精简证据23文件存`docs/evidence/split_d_ebe529b_4x4_timing/`。两个块阶段II均5/5/1/1；ROW_SUM估算7.048ns。
- 官方资料复核：AMD UG1448 2024.2 Timing-Critical-Path说明按超出时钟边界的操作链定位；UG1387 2024.2 Avoid-Unnecessary-Pipelining说明额外寄存器可能增加资源/布线压力。因此本轮先验证消除无效选择器，未盲目插级。来源：https://docs.amd.com/r/2024.2-English/ug1448-hls-guidance/Timing-Critical-Path 和 https://docs.amd.com/r/2024.2-English/ug1387-acap-hardware-ip-platform-dev-methodology/Avoid-Unnecessary-Pipelining 。

- 资料核查更正：AMD上述UG1448链接返回的正文是flow-target默认配置表，与页面Timing-Critical-Path标题不符；UG1387全文抓取失败，当前仅取得搜索摘要。它们不作为具体pipeline修复效果或200-871归因的依据，本轮归因仍仅由实际构建路径与单因素修改支撑。

- 4×4 runPeArray仍latency3/II1，估算7.048ns；Accumulator仍latency7/II1、7.299ns。广播未新增流水级，阶段II5/5/1/1保持；完整归档与原始rpt支撑，未用源码pragma代替结果。

## 12 第1轮16×16 CSim完成

官方CSim耗时7分49秒，已进入CSynth；读取摘要确认数学22/22、PE16×16/head128/DIM_BLOCKS8、0 errors。既有22有效＋2非法覆盖保留；16配置无独立严格位模式基线，不能外推4的24/24严格结果。尚未得到本轮综合时钟/资源/II。

- 综合监控补充：09:25只读进程树确认编译优化子进程clang累计CPU3分31秒/墙钟212秒，仍持续计算；日志暂未新增时序结果不代表停滞。旧60分钟超时合同仅针对CoSim，本次CSynth不套用该门槛。

## 13 第1轮共享PE调度完成（综合尚未结束）

runPeArray调度完成，Final II5、Depth10，调度墙钟1340.66秒/CPU1336.72秒；没有200-871/1016/204-65。与旧1345.96秒调度耗时接近，不能把本次约22分钟调度等待视为停滞。随后进入资源绑定；函数latency/时钟须读取新XML，ROW_SUM和顶层仍未验收。日志Target II1与源码实际ii5口径差异仍待解释，本轮不修改PE_ARRAY_II。

- 4证据计数口径澄清：已逐SHA核对247个manifest载荷文件，tar实际含248个文件（另含manifest.json）；Git精简载荷23个、另含local_evidence_manifest.json。原“247文件归档”指载荷，不含清单本身。

## 14 第1轮调用端调度完成（RTL生成中）

两个query worker已完成阶段调度，实际II仍QK10/ROW_SUM10/PWL5/PV5；截至RTL生成阶段200-871/1016/204-65及ERROR均0。共享PE绑定墙钟569.09秒，计算源码/pragma保持931853d；目标II的未达项保留，不当作时序修复已解决。综合尚未完整结束，需读取新XML时钟/资源、完整RTL实例与最终退出标记后闭环。

## 15 第1轮最终远端结果与分析

- 16标准流程09:13:01—10:22:16退出0；官方数学22/22、0 errors、CSim7分49秒。顶层7.300ns，两个ROW_SUM7.272ns，旧7.390ns超限消除，200-871/1016/204-65/too complicated均0。固定10ns/2.7ns合同未更改。
- 新资源DSP352/FF231565/LUT1213113/BRAM18K8/URAM0；相对71025ff，DSP/BRAM不变、FF−32、LUT−8576。PE每块资源/latency9/II5与旧报告完全相同，顶层最坏latency850450435、interval-max850450436亦保持；不能当作N1024实测时间。
- RTL实例：2个runPeArray、2个runAccumulatorColumns，总256条11×11尾数乘法、16条24×24乘法、32个FP32减法；DSP256＋32＋64=352，无额外阵列。每块PE估算LUT459128仍超过单SLR容量，实际16布局未验证。
- 实际阶段II10/10/5/5保持，真实反馈200-880四项、200-875十六项仍存在。本轮解决组合路径时序，未解决吞吐/完整反馈延迟，不用DEPENDENCE false或更换参数掩盖。
- 原始338个manifest载荷文件归档`build/split_d_ebe529b_16x16_timing_evidence_20261012`；tar`/tmp/codex_split_d_ebe529b_16x16_timing_evidence_20261012.tar.gz`，SHA fbe94010a3d66624efd6d63fcb3c936f0d573b9367092be0434111aa5dec9011，9928963bytes，正在下载作本地逐SHA核对。
- 已解决：两配置官方CSim及CSynth、≤7.300ns估算时序、固定总PE/Acc与资源复制核查；尚待本地归档/源SHA及最终文档同步。仍存在：16实际II未达5/5/1/1、16独立位模式/RTL背压/CoSim/IP/物理/板测与N1024长测未验收。
- 下一轮修改：本次时序目标已达到，无需新时序轮；停止新构建，完成证据校验及文档收尾。CoSim/IP/实现/板测保持关闭，不从条件门槛解除推断新的执行授权。
- 第1轮远端数据已读取并完成分析，闭环1轮；最终完整交付在下节记录。

## 16 最终证据校验与闭环

两个归档已下载并逐文件SHA核对。4配置验证247个manifest载荷文件，归档共248文件；16配置验证338个载荷文件，归档共339文件。两配置各92份源码均与`git show ebe529b:<path>`的原始字节一致，避免Windows工作副本CRLF影响比较。Git精简证据分别为23/26个载荷加各自`local_evidence_manifest.json`，位于`docs/evidence/split_d_ebe529b_4x4_timing/`及`docs/evidence/split_d_ebe529b_16x16_timing/`。

| 配置 | 官方CSim | 顶层/ROW_SUM估算(ns) | QK/ROW_SUM/PWL/PV实际II | DSP/FF/LUT/BRAM18K |
|---|---|---|---|---|
| 4×4/head16 | 数学24/24、严格24/24，另含2笔非法长度 | 7.300/7.048 | 5/5/1/1 | 40/45881/128820/8 |
| 16×16/head128 | 数学22/22，另含2笔非法长度；无独立严格位模式基线 | 7.300/7.272 | 10/10/5/5 | 352/231565/1213113/8 |

- 16×16 CSynth实际耗时1小时00分37秒；读取全部XML，估算周期均≤7.300ns。ROW_SUM由7.390降至7.272ns，顶层由7.390降至7.300ns，保持10ns及2.7ns uncertainty。通过的是HLS估算时序，未经Vivado物理验收。
- 最终XML阶段循环周期/II与71025ff相同：QK1281/10、ROW_SUM160/10、PWL53/5、PV10886/5；最终PV pipeline depth12，中间调度控制台曾显示13，以最终XML为准。原顶层最坏周期模型保持，不是N1024硬件实测。
- 原始日志核对`CSIM=1 COSIM=0 EXPORT_IP=0`，没有执行`cosim_design`或`export_design`。16×16反馈类warning保留；吞吐目标5/5/1/1、RTL背压/数值、IP、实现后时序和板测仍未验收。本轮不新增测试。
- 本地证据整理曾因Windows默认GBK读取UTF-8文本失败，显式UTF-8后完成；收尾编辑脚本曾因多行字符串语法失败，解析前终止，未改文件。均为本地整理问题，不计DUT/HLS失败。
- 本轮计算修改仅931853d的ROW_SUM广播，精确HLS被测ebe529b。最终提交仅补充上下文、日志及原始/派生证据，不视为重新HLS验收；未暂存用户的AGENTS.md、.tmprun及旧stub日志。

本次无轮数上限，1轮完成当前时序合同，已停止新构建。后续吞吐、CoSim或物理验证按新的任务范围推进。
