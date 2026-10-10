# Split-D阶段4恢复与OOC对照修改日志

## 1 调用与合同

- 开始：2026-10-10T10:32:37+08:00。用户确认“恢复阶段4”，撤销本阶段暂停；先完成OOC对照，完整系统集成后续。Git/SSH、正式4×4/head16 CSim→CSynth→CoSim→IP、Vivado OOC自动布局及区域对照属于范围，无迭代轮数上限。16×16、bitstream、板测不进入。
- 本地：C:/Users/30130/Desktop/workstation/FlashAttention/FSA_HLS；远端：FSA-FPGA-NM37-tailBox:/home/zhangchenxuan/FSA_HLS；分支fsa_split_D，origin为git@github.com:diego3893/FSA_HLS.git。
- 起始HEAD39ba509756eebe1c8b083593fb83d734bf2e27b9，计算基线130c778。上次暂停日志0248保留，另起本次日志。用户AGENTS.md、.tmprun/、旧stub日志保留且不提交；原暂停上下文/日志变动属于本任务。
- 标准：数学24/24、严格24/24、26事务完成；16PE/4Acc/4恢复除法循环、DSP40、II5/5/1/1、HLS≤7.3ns；保持数值、接口、VU37P、100MHz及2.7ns uncertainty。正式IP需版本与SHA证据。
- OOC标准：声明边界预算；route完成，setup WNS≥0/TNS0、hold WHS≥0/THS0，DRC及必需路径约束检查。自动布局与同RTL区域约束对照，位置取自实际资源和实例，不强制跨SLR。通信插级由真实路径决定，改RTL必须重验完整HLS。OOC结论不外推板级。

## 2 第1轮：正式IP和物理基线

### 预检与假设

- 本次skill调用起第1轮。服务器仍a6ba4d6，tracked干净、无独立构建进程，Vitis/Vivado2024.2可用，705G可用。SSH沙箱内别名解析失败；沙箱外连接成功，属于同轮基础设施重试。
- 尚无物理证据。首轮复用39ba509的正式Tcl：CSim/CSynth/CoSim全开、trace all、dataflow profiling、IP导出，不改计算代码或测试。
- 先确认正式IP结构及综合/功能不退化，再按实际封装编写可复现OOC脚本。74级K/V分发开销仍是待验证优化方向，不能据此预先宣称长线。
- 本轮当前未完成，已闭环0轮。

### 同步与正式执行

- 任务文档提交b3e4957e7454989198f58345e9ebcff1dac536d3已推送，服务器ff-only同步后核对精确HEAD。AGENTS.md未纳入；计算代码仍130c778，Tcl仍39ba509。
- 沙箱内Git push不返回，停止本地该进程后在沙箱外正常推送成功；无重复远端构建。
- 命令：交互Bash显式source ~/.bashrc后执行`./run_hls.sh fsa_stream_split_d`，日志`/tmp/codex_split_d_b3e4957_stage4_20261010.log`。全部原26事务、D4/H16/MAX4096，无环境缩减。
- 首次进度读取：CSim46s、数学24/24、严格24/24、exit0；CSynth进行中。CSim的stream最大深度112是软件仿真动态队列观察，不代表RTL FIFO深度。

### 正式HLS与IP结果

- b3e4957官方流程exit0，2026-10-10T10:59:11+08:00结束。CoSim7m13s、26事务80242cycles、数学/严格24/24；latency50/3095/18423、interval45/3207/18413，与130c778一致。顶层HLS7.300ns、DSP40/FF45091/LUT129592/BRAM8保持。
- 正式IP在`solution1/impl/ip/component.xml`及`xilinx_com_hls_fsa_stream_split_d_1_0.zip`。本地取证`build/stage4_b3e4957/ip_snapshot.tar.gz`，29739304bytes；包含报告/TV/正式impl，不将完整工具目录提交。
- 新OOC脚本`tools/split_d_ooc/run.tcl`/`constraints.xdc`/README读取同一次正式导出。边界固定100MHz、setup uncertainty2.7ns、全部同步输入/输出max2ns/min0ns；包括reset，无false path。HLS2.7ns对应setup调度余量，hold由物理时钟模型分析；不施加额外2.7ns hold余量。此为OOC合同，未声明板级环境。
- 官方原理依据：[UG939非工程IP流程](https://docs.amd.com/r/2024.2-English/ug939-vivado-designing-with-ip-tutorial/Step-8-Run-the-Script)、[UG903 OOC约束](https://docs.amd.com/r/en-US/ug903-vivado-using-constraints/Out-of-Context-Constraints)。下一步在相同正式IP上运行自动布局，区域取实际位置。
- 本地指纹工具初版错误地断言export/syn全部文件相同，且Windows路径分隔符令ZIP索引失败；已改为记录逐文件差异和POSIX相对路径。实际只发现导出top增加translate_off包围的仿真deadlock include/闲置信号及一个导出专用detect模块，其余Verilog相同，不将字节差异隐藏。OOC读取正式导出并设置include目录；后续检查无blackbox及真实资源。
- PowerShell未在该指纹检查失败时停止后续Git命令，f316a5c先提交了OOC脚本和已有HLS证据；IP清单补入下一提交。本地检查失败不写成验收通过，未因此启动错误版本的远端测试。
