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
| 1 | `0bfd6d2` | 静态审查与初始化生命周期优化 | 数学32/32、旧stub全字一致 | 官方CSim32/32、CSynth完成，II1/7.300ns，LUT超限 | 完成，容量失败 |
| 2 | `69ea82d` | 简化PE规格化与exp2分段逻辑 | 214560行全字一致，独立PE边界测试通过 | 官方CSim32/32、CSynth完成，II1/7.300ns，LUT超限 | 完成，容量失败 |
| 3 | 待远端测试 | 窄化控制令牌与PE宽移位 | 214560行全字一致，独立PE边界测试通过 | 待运行 | 本地候选 |

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
- 16:28远端已ff-only同步0bfd6d2b4710321589d590afb86d53701f9a74dc，bash -n及非法预算输入检查通过。但预检set-u未在加载Xilinx settings前关闭，触发PYTHONPATH未绑定，尚未启动HLS。纠正为加载settings前set +u，加载后再set -euo pipefail；属于本轮环境重试，不算新迭代。

### 远端测试

- 被测提交：0bfd6d2b4710321589d590afb86d53701f9a74dc；源码SHA已保存。
- 已显式加载.bashrc与Xilinx settings，开始2026-10-13T16:29:24+08:00；当前进行中。
- 命令：RUN_CSIM=1 RUN_COSIM=0 EXPORT_IP=0 FSA_SPLIT_D_PE_DIM=16 FSA_SPLIT_D_HEAD_DIM=128 FSA_MAX_SEQUENCE_LENGTH=1024 FSA_SPLIT_D_LONG_CSIM_TEST=0 FSA_HLS_TIMEOUT_SECONDS=7200 ./run_hls.sh fsa_stream_split_d。
- unset全部engine覆盖及capture，默认M8/hop7/反馈8/Acc11。标准入口未替换Tcl或testbench。
- 远端证据：build/split_d_stage4_resume_0bfd6d2_M8_init_20261013/{COMMIT,source.sha256,start.txt,run.log}；结束后保存exit/end及完整build。
- 本轮分析尚未闭合，不能开始第2轮实现或声称达标。
- 官方CSim已完成：数学32/32、逐位32/32、0 errors；官方csim_design记录00:15:11。capture关闭，全部冻结输入和85824输出字直接核对；初始化修改的C功能门槛通过。当前进入CSynth，硬件门槛仍待结果。
- 只读证据工具extract_resume_summary.py已通过AST检查，并在旧M1完整归档重现256PE/16Acc；它要求本轮exit记录、完整csynth完成标记、顶层XML与RTL同时存在才标记CSynth完成。旧归档缺少exit.txt时保守标记false，不改写旧综合成功结论。配置来自官方Tcl默认及override输出，不能套用新参数到历史结果。
- 提取器历史对照中发现属性清理误识别always @(*)，已加排除并重新核对：167个module声明一致，历史130类已统计节点差分0，256PE/16Acc不变；新增26类计数为带同一行属性的sparsemux，旧计数未含这些辅助实例。本次不以辅助mux数量替代实际资源报告。
- 16:47官方Unroll/Inline最终指令2340869，上轮2440303，计算减少4.075%；尚未进入上轮待定异常的build-ssdm阶段，不能据此宣布解决耗时。
- 16:53官方Array/Struct峰值4842955，上轮6861689，计算减少29.420%；简化后2461763，上轮2559169。支持整结构初始化放大中间表示的判断，尚不能证明上轮build-ssdm根因、最终耗时或RAM/容量达标。
- 后续仅候选计划：若本轮仍不能完成或不达硬件门槛，依据UG1399运行时间建议评估标量PE独立流水函数边界，减少单个展开作用域的IR对象；保留每坐标实例与算术逻辑，必须重新核对服务II、反馈、时序与完整RTL。当前不修改下一轮源码。
- 资料补充：https://docs.amd.com/r/2024.2-English/ug1399-vitis-hls/Function-Inlining；https://docs.amd.com/r/2024.2-English/ug1399-vitis-hls/Function-Pipelining。明确INLINE off可阻止自动内联，函数流水会展开其内部及下级循环；这只是候选依据，不证明HLS一定保持所需256PE并行。
- 17:12完成前端Compiling Optimization and Transform，耗时1570.02秒；HW Transforms(step2)指令2097063，上轮2194401，约少4.44%。此时尚未完成调度，不能据此推断最终资源。
- 17:20后进入循环/函数优化及依赖分析；`runPeArray`内if-conversion信息769条，与上轮相同。PE年龄环RAW距离7与现有hop7一致，不将普通依赖枚举计作II违例。17:27仍无最终XML，测试继续，保留7200秒工具预算。
- 用户17:30要求暂停对话但保持远端进程，未停止作业；17:51恢复后只读确认同一被测提交、同一作业仍在运行。官方调度日志现已给出主循环`VITIS_LOOP_334_1` Final II=1/Depth47、`runPeArray` Final II=1/Depth5、`runAccumulatorColumns` Final II=1/Depth8，无II Violation。17:52转入`runSharedEngine`函数调度，最终XML/RTL尚无，不能宣称完整CSynth或资源通过。
- 第1轮最终结束2026-10-13T18:11:41+08:00，RUN_EXIT0，墙钟6137秒（1小时42分17秒），标准CSynth耗时1小时25分58秒，峰值工具内存22.002GB。前次3小时22分钟未完成的症状没有在本轮重复；但两轮源码与工具路径不同，不能据此单独证明上次根因就是整结构初始化。
- 官方数学/冻结逐位32/32、85824输出字，CSim0 errors。共享主循环PipelineII1、runPeArray/Accumulator服务interval1，无II违例或关键路径告警，顶层估算7.300ns；完整RTL递归256PE/16Accumulator，Q/K/V/O使用block RAM，S/reg使用distributed RAM。BRAM_18K360/4032、DSP356/9024、FF673002/2607360、URAM0/960通过。
- 顶层LUT1680612/1303680，超376932（约28.91%），因此阶段4容量门槛失败，不生成通过门槛后才允许的性能模型；顶层latency仍undef。两路worker各834896LUT，其中各自共享引擎642368、PE阵列472970、Accumulator56794。PE阵列两路合计945940LUT；M1与M8对照中PE/Accumulator面积不变，每路worker增加130996LUT，其中共享引擎增加61348。下一轮应同时关注PE算术与M8状态选择，不把函数边界编译优化等同于面积优化。
- 证据：远端`build/split_d_stage4_resume_0bfd6d2_M8_init_20261013/`含完整build/run.log/exit/end/source SHA；本地`docs/evidence/split_d_stage4_20261013/stage4_resume_0bfd6d2_summary.json`由只读提取器生成，`csynth_completed=true`。原始XML/RTL以远端归档为准。RTGEN估算共享引擎控制状态最大扇出11656，只是未来物理风险，不是本阶段已测布线结论。

## 6 第2轮

### 修改前判断与实际修改

- 完整资源分解：PE阵列每worker472970LUT，其中表达式406408、选择器35200。表达式表中54bit右移共41216LUT、48bit右移19072LUT；逐位最高位查找形成的25输入选择器每PE约123LUT。M8增加的worker面积约130996LUT/路；单纯缩短CSynth运行时间或改变函数边界不足以解决376932LUT的总超限。
- `src/stream/pe_raw_fma.cpp`：c非零分支直接用24bit前导零数推导最高位指数及规格化移位量，删除逐位优先查找；exp2余数严格小于整数单位，分段编号只取乘8整数部分的3位，删除14bit双向动态移位后的冗余饱和判断。保留同一RawFMA、DSP乘法、所有特殊值与舍入路径；这只是一项面积候选，综合收益尚未知。
- 本地16/128/M8/L_MAX1024 capture与第1轮旧stub的128736输入字和85824输出字共214560行逐行相同，数学32/32、stage4严格32/32（capture模式）。独立PE RawFMA测试通过30000随机MAC、24480对齐边界、4608规格化边界、4596 exp2缩放边界及特殊值；D4/H16数学52/52、新严格28/28、旧stub严格19/24保持既有5项差异。编译无新增项目源码warning；这些仍不代替官方Vitis逐位与CSynth。
- 下一步：只提交本任务源码、日志、报告及摘要；NM37精确同步后标准入口完整CSim/CSynth，LONG_CSIM_TEST=0、CoSim/IP/Vivado/板测关闭。若LUT仍超限，依据实际表达式/状态选择分解决定下一次结构修改，不靠延长预算掩盖容量失败。
- 第2轮提交`69ea82ddb7d56fd91d5d279c91d4af1f608831a8`已推送`origin/fsa_split_D`，仅含任务文件，用户的`AGENTS.md`、`.tmprun/`和旧stub日志保留。NM37预检tracked干净、无活动HLS进程，ff-only后HEAD精确一致；2026-10-13T18:31:41+08:00启动标准CSim/CSynth，归档`build/split_d_stage4_resume_69ea82d_M8_pe_area_20261013/`，源码SHA、start及run.log已写入，待exit/end及完整build。
- 独立枚举全部65536个FP16位模式，除NaN/Inf外有49152个非零小数余数，新的3位piece公式与原饱和/双向移位公式逐一相同；这是算术等价附加证据，仍以官方CSim为功能门槛。
- 第2轮官方CSim于约18:47完成：数学32/32、冻结逐位32/32、0 errors，csim_design耗时00:15:12。18:47进入CSynth；II/时序/实例与面积尚无结论，不能使用第1轮硬件结果代替。
- 19:12前端`Optimization and Transform`完成：本轮1374.42秒，第1轮1570.02秒，缩短约12.5%；`HW Transforms`指令2074594，第1轮2196706，减少约5.6%。这些只是编译规模与耗时证据，不能换算成LUT节省。可综合性检查已完成，后续调度和最终报告仍待观察。
- 待验证的下一轮静态线索：第1轮共享循环RTL中`pe_control_context_127`是32位，而本配置有效context仅0..7；`pe_control_index_127`是10位，而已发射的feature/piece索引仅0..127。年龄环及流水寄存有多份副本。若第2轮仍超容量，可按编译期容量窄化`SharedToken`字段并重跑功能、II/时序/资源；此处不推断LUT节省，候选准备见第7节。
- 第2轮2026-10-13T20:02:50+08:00结束，RUN_EXIT0、墙钟5469秒（1小时31分9秒），比第1轮少668秒。官方数学/冻结逐位32/32，CSim0 errors；完整CSynth共享循环Final II1/Depth47、PE服务II1/Depth5、Accumulator服务II1/Depth8，无II Violation/critical路径告警，顶层估算7.300ns。RTL两路各128PE与8Accumulator，合计256PE/16Accumulator；Q/K/V/O BRAM、S/reg LUTRAM映射维持。DSP356、FF685278、BRAM_18K360、URAM0。
- 顶层LUT1653532/1303680，仍超349852（约26.84%）；相比第1轮恰少27080。每路worker减13540LUT，完全对应各自PE阵列472970→459430LUT，共享引擎642368→628828、Accumulator56794不变；PE阵列单路仍超过SLR434560LUT。第2轮面积门槛失败，不生成门槛后的性能模型。证据：远端完整归档`build/split_d_stage4_resume_69ea82d_M8_pe_area_20261013/`含exit/end/source SHA及build；本地`docs/evidence/split_d_stage4_20261013/stage4_resume_69ea82d_summary.json`，`csynth_completed=true`。原始XML/RTL以远端归档为准。

## 7 第3轮候选与本地验证

- 第2轮生成中的`runPeArray`模块报告显示每worker459430LUT，第1轮472970LUT，暂减少13540LUT/worker；每worker仍高于单SLR的434560LUT。此时顶层CSynth尚未完成，不能把局部差分当作顶层结论。
- 已在本地工作树尝试窄化`SharedToken`的context和index至编译期所需位宽；对`shiftRightJam27`与FP32缩放的guard/sticky处理改为16/8/4/2/1位固定移位级，避免54/48位动态右移。第2轮远端仍精确运行`69ea82d`，这些新改动不在被测提交内。
- 本地独立RawFMA测试通过30000随机MAC、24480对齐边界、4608规格化边界、4596exp2缩放边界及特殊值。16/128/M8/L_MAX1024数学32/32，capture模式stage4严格32/32；与第2轮旧stub的128736输入字及85824输出字共214560行逐行相同。4/16/M8数学52/52，stage12严格28/28，对旧stub严格19/24与第2轮相同。尚未运行候选的官方Vitis CSim/CSynth，不能判断II、时序、RTL资源或容量。
