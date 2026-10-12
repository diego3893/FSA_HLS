# Split-D关键路径修复迭代

## 1 本次调用信息

- 开始：2026-10-12 08:50+08；进行中，最大迭代次数未设置；09:04用户明确恢复发布及CSim/CSynth。
- 用户要求：“继续修复关键路径。在时序通过之前不进行cosim”。本次恢复CSim/CSynth迭代；CoSim、IP导出、Vivado及板测均不执行。时序通过后先报告结果，不自动扩大范围。
- 本地：`C:/Users/30130/Desktop/workstation/FlashAttention/FSA_HLS`；远端：`FSA-FPGA-NM37-tailBox:/home/zhangchenxuan/FSA_HLS`；分支`fsa_split_D`。
- 起始提交：`931853de7f8a35748f227a3c5eed32f6993d3689`，已有ROW_SUM广播候选仅完成语法检查。本次先测此候选，不凭旧报告叠加新修改。
- 使用`fsa-hls-remote-iteration`及`maintaining-project-context`；Git/SSH沿用本对话授权，测试以本次最新要求为准。
- Vitis HLS2024.2、VU37P、10ns时钟、2.7ns uncertainty；固定两个query块和原精度/接口/算术顺序。

## 2 可验证标准

- [ ] 4×4/head16官方CSim数学/严格24/24及非法长度保护通过；既有覆盖不收窄。
- [ ] 16×16/head128官方CSim数学22/22及非法长度保护通过；1024长测仍留后续。
- [ ] 两种配置完成CSynth，顶层估算周期≤7.300ns；读取全部关键路径和warning，不只检查命令退出码。
- [ ] RTL实例核对总D×D PE、D条Accumulator，记录资源、循环/函数实际II，排除复制；吞吐II目标的未达项单独记录，不伪称已修复。
- [ ] `RUN_COSIM=0 EXPORT_IP=0`，无CoSim/IP/实现/板测；待时序通过后由用户决定后续范围。

## 3 初始核查

- 本地已有`AGENTS.md`修改、`.tmprun/`及旧stub日志未跟踪，暂存为空；保留这些用户/无关文件。
- 远端HEAD5fda5a4、分支/origin正确，tracked干净；既有未跟踪工具日志/JSON不动。无Vitis/XSIM进程，689GB可用；4×4 IP快照及71025ff测量归档存在。
- 非交互`.bashrc`未使工具链进入PATH；显式source `.bashrc`后，再source已核实`/opt/Xilinx_2024.2/Vitis/2024.2/settings64.sh`，`vitis-run`可用。交互stdin核查输出不完整，未启动测试；改为非交互Bash加明确环境初始化，exit0。
- 旧71025ff关键路径为ROW_SUM load→operand_c select0.227ns→PE call7.163ns，总7.390ns。931853d已删除源级选择器，广播扇出/重新调度待本轮实测。

## 4 迭代总览

| 轮次 | 被测commit | 修改 | 本地检查 | 远端结果 | 状态 |
|---:|---|---|---|---|---|
| 1 | 1721ff4（已提交，push被拒） | 继承931853d的ROW_SUM广播，暂不叠加新硬件修改 | 两种配置语法检查已完成 | 未启动；等待用户确认push | 未完成（阻塞） |

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
