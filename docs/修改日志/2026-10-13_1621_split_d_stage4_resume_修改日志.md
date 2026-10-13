# Split-D阶段4恢复迭代修改日志

## 1 本次调用信息

- 开始时间：2026-10-13T16:21+08:00；状态：进行中。
- 用户要求：将上轮CSynth耗时视为待定异常，先静态检查、查阅资料、修改，再继续测试；最终目标沿用阶段4。
- 仓库：C:/Users/30130/Desktop/workstation/FlashAttention/FSA_HLS；远端FSA-FPGA-NM37-tailBox:/home/zhangchenxuan/FSA_HLS。
- 分支：fsa_split_D；起始HEAD：229177c9478654a262a79450052d02ff2ab11ac4；起始DUT：11cd35a29b7caf490109a06d504b35f3246f1fa9。
- 工具：Vitis HLS2024.2；每次SSH显式Bash加载~/.bashrc，测试另加载/opt/Xilinx_2024.2/Vitis/2024.2/settings64.sh。
- 迭代上限未设置；本次恢复覆盖此前暂停要求。沿用任务源码/测试/文档普通提交推送、NM37精确提交CSim/CSynth及证据收集授权。

## 2 验收合同

- 16×16、H128、默认M8，L_MAX1024；LONG_CSIM_TEST=0，不运行N1024；既有32项数学/独立官方逐位golden及保护检查全部通过。
- 最终完整CSynth：共享循环与PE/Acc真实II1，估算周期≤7.300ns；10ns时钟/2.7ns uncertainty、VU37P保持。
- 真实RTL总256PE/16Accumulator，保留既有减法/倒数通路；不复制MAC换吞吐、不隐藏真实依赖。
- 实际RAM映射、LUT/FF/DSP/BRAM/URAM不超过器件容量；报告短批/阶段预算与完整事务未知范围。
- FP16/FP32量化、逐query归约顺序、四路64bit AXI与顶层保持；CoSim/IP/Vivado/板测关闭。
- CSynth长耗时仅标记待定异常；将编译阶段/指令规模与硬件性能分开，不从CPU活动推断有效进展。

## 3 初始状态

- 暂存为空；用户AGENTS.md修改、.tmprun/和旧stub日志保留，不纳入提交。
- 上轮11cd35a官方数学32/32、逐位32/32；总运行12149秒后受控停止，build-ssdm约7799秒未完成。M1对应416.1秒，HW Transforms指令仅增长8.897%；具体根因未证明。
- 原始归档及停止报告保留，不改写历史测量。本次远端预检待执行。

## 4 迭代总览

| 轮次 | 被测提交 | 修改摘要 | 本地结果 | 远端结果 | 状态 |
|---:|---|---|---|---|---|
| 1 | 待定 | 静态审查与初始化生命周期优化 | 待执行 | 待执行 | 进行中 |

## 5 第1轮

### 静态检查、资料与修改前判断

- 在调用入口开始本轮，先审查state数组生命周期、共享函数边界、分区/绑定与有限FIFO，不启动旧源码重复长构建。
- 确认worker使用SharedEngineState state{}，对完整Q/K/V/S/P/O与标志实施整结构零初始化；多数数组随后在首次读取前完整覆盖。
- AMD资料指出自动局部数组初始化会形成每次调用的存储写入；分区、展开、内联增加编译对象与搜索空间。这支持审查初始化及表示复杂度，但不能直接证明上轮build-ssdm根因。
- 资料：https://docs.amd.com/r/2024.2-English/ug1399-vitis-hls/Initializing-and-Resetting-Arrays；https://docs.amd.com/r/2024.2-English/ug1399-vitis-hls/Improving-Synthesis-Runtime-and-Capacity。已读取2024.2对应正文。
- 候选方向：以显式、有限、流水初始化仅清零首次读取依赖的L/O/history，同时确保Q/K/V/S/P/alpha/valid先写后读；保留每次batch独立状态，不依靠上电初始化。
- 修改与远端结果尚未执行，不能确认能解决异常耗时或资源门槛。

### 实际修改与本地验证

- src/stream/split_d/split_d_controller.cpp：取消整结构值初始化，仅对活跃context初始化running_max/running_sum/history_valid/output_acc；O按feature流水、列并行。将max初始化从Q装载嵌套中移出，Q装载成为perfect loop候选。算术和phase顺序不变。
- 首次读取账本：Q每批完整装载；K/V每key完整装载；S/score_valid/probability_ready每key初始化；reg从完整S量化；alpha和tile_has_value由控制器写入；L/O/history必须显式初始化；环槽前HOP拍仅写，读取由年龄保护。
- run_hls.sh：可选FSA_HLS_TIMEOUT_SECONDS正整数，在标准工具调用处使用GNU timeout，TERM后30秒仍未退出才清理同组进程；设置时输出预算，不设置保持原入口。本轮7200秒工具墙钟预算，超时124仅记未完成，不修改功能/性能门槛。
- 本地16/128/M8/L_MAX1024：数学32/32，对官方逐位21/32，exit1；保持已知11项stub差异。capture与旧stub的128736输入字/85824输出字合计214560行全部一致、差分0；不作为官方逐位通过。
- 本地4/16/M8/L_MAX1024：数学52/52、新严格28/28、旧严格19/24、exit1；保持原5项stub差异。
- NM37预检：根/分支/origin正确，HEAD11cd35a，tracked干净；已有未跟踪工具产物保留，无vitis_hls/vitis-run活动进程。无拉取冲突。
- 原始本地检查日志：build/local_stage4_resume_{16,4,capture}.log。未执行官方测试前不宣称逐位/II/时序或资源通过。
- 本地Git Bash语法检查因MSYS命名对象权限0xC0000022未启动，未得到语法结果；改为NM37拉取后先bash -n和非法预算输入检查，通过后才启动HLS。git diff --check通过。
