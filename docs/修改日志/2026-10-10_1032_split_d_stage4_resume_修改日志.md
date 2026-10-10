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
