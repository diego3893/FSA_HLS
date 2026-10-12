# FSA HLS Project Context

> 2026-10-12压缩版，阶段0已完成，用户现已恢复阶段1、2的4配置修改及CSim/CSynth闭环。只维护仓库根目录本文件；历史全文可从Git提交d3245a3恢复。

## 1 当前状态与任务

- 用户目标：先在4×4/head16把QK/ROW_SUM/PWL/PV实际发射II全部降到1并改善性能，达标后才测试16×16/head128；不进行CoSim。
- 最新任务：用户要求“继续阶段1和2”，按已交付空间ROW_SUM等价口径恢复实现和CSim/CSynth。28笔固定输入/head消去/signed-zero及canary已加入，基线捕获准备提交1afc13e（DUT未改）。共享有限引擎、空间RS/PV阶段1及query batch阶段2候选已本地实现，正式旧DUT52数学/24旧严格通过，28笔3440word官方输出已冻结；M1候选CSim已通过全部原/新增检查，CSynth完成：共享循环II2、7.815ns，验收未过；第2轮准备Q广播和Acc输入寄存；当前日志docs/修改日志/2026-10-12_1437_split_d_stage12_修改日志.md。
- 当前本地/远端被测HEAD为2b0d51a（共享引擎及batch支持），官方M1候选CSim已通过52数学/24旧严格/28新严格及2非法，CSynth完成：共享循环II2、7.815ns，验收未过；第2轮准备Q广播和Acc输入寄存。阶段1同stub新增3440word差分0，数学52/52、旧严格19/24已知位差、exit1；正式新基线头已从1afc13e旧DUT冻结3440word，manifest保存来源/SHA。阶段0文档/证据已在1afc13e提交。
- 被测代码：73110dc2d6eb799072c29fc4954f8f8d5427973b；仅将QK/ROW_SUM两处PIPELINE目标II5→1，未改变算术或状态。收尾文档d3245a3已推送；文档提交不算HLS重新测试。
- 官方4配置流程11:58:47—12:02:27退出0：数学/严格位模式24/24及2笔非法长度保护通过；实际II3/3/1/1，顶层估算7.887ns，II与≤7.300ns时序门槛未通过。
- 资源：DSP40/FF44855/LUT128938/BRAM18K8；实际2个PE块、2个Accumulator块，总16条PE乘法、4条Acc乘法和8条减法，未复制MAC。
- 用户已明确选择“四阶段都到II1，允许交错独立query上下文并增加状态存储”。新增Q/S/P/m/l/O上下文允许，逐query的FMA/量化顺序及MAC实例数保持；交错结构尚未实现。
- CoSim、IP导出、Vivado、板测、16配置及1024长测均未在本轮执行。没有1024序列硬件实测延迟。

## 2 授权、验收与禁止事项

- 用户本次明确恢复阶段1、2；原自动审批因缺少精确上传目标授权而拒绝；用户现已明确允许该GitHub仓库/分支推送及NM37同步测试，阻塞解除。在阶段0具体提案后继续实施按其ROW_SUM口径进行。阶段3/4及CoSim/IP/板测不由本次恢复指令启动。
- 保持独立顶层fsa_stream_split_d、四路64-bit AXI master和AXI-Lite、VU37P器件、100MHz/10ns时钟、2.7ns uncertainty；不修改FSA-main或生产顶层fsa_stream。
- 每配置总D×D条PE RawFMA、D条Accumulator及既有倒数通路；允许两个D×B空间query块，禁止复制计算通路换吞吐。源码函数唯一、ALLOCATION或总DSP单项均不足以证明实例数。
- 保持FP16×FP16＋FP32 PE、FP32 Accumulator、RawFMA特殊值/舍入、PWL及FP32 max→FP16→SUB_MAX→SCALE→PWL量化顺序；不改变金标准/0.03容差、不重排逐query归约。
- 旧“每PE单份状态且禁止额外S/P副本”约束已被用户对本次II1候选的明确选择放宽；只放宽独立query上下文状态。报告新增容量、用途、批次总周期、折算tile吞吐与短序列空槽成本。
- 真实依赖不能标DEPENDENCE false。单元latency与II不同：依赖距离×II需覆盖完整反馈路径，并满足服务能力；交错后的发射II1不等于同一query每拍反馈。
- 每轮读取精确commit的功能、时序、阶段循环II/trip count/cycles、warning、RTL层次/运算符、资源后闭环。不以流程exit0宣称性能通过，也不沿用旧bitstream验收新源码。
- 标准入口仅仓库run_hls.sh；禁止替代Tcl、wrapper、临时testbench或收窄覆盖规避失败。允许在既有测试台补强覆盖；证据提取不替代测试流程。
- 当前恢复范围限CSim＋CSynth，RUN_COSIM=0/EXPORT_IP=0/LONG_CSIM=0；本次不启动16配置。默认Tcl包含CoSim/IP，执行时必须显式关闭。无轮数上限。
- 不force push、reset/stash/checkout丢弃、git clean或批量暂存；只提交审查过的任务路径。保留用户AGENTS.md修改、.tmprun/、docs/evidence/split_d_9c49789_4x4/local_math_stubs_check.log及远端无关文件。
- C/C++和Markdown格式遵循用户最新AGENTS.md；函数签名与左花括号同一行，if/for括号前不加空格，逗号后空格；中文与英文/数字/行内代码之间不额外加空格。

## 3 当前架构与失败机理

- 输入分发→两个模板runQueryBlock→单输出汇聚为前向DATAFLOW；header FIFO深度2，Q/KV/O深度8。原始64-bit Q/128-bit KV字传输，在worker中解包。worker消费全部key输入后输出；需随结构变更重新审查有限FIFO等待关系。
- D4/B2和D16/B8分别总16/256PE；各worker内部QK、SUB_MAX、SCALE、PWL、ROW_SUM、PV共享runPeArray，FP32列运算共享runAccumulatorColumns。全局D/H与DMA步长不得替换成局部B。
- Q/K/V均按[token][feature]装载；PV消费方式不同。PV每次仅一条key行有效，总D个有效MAC位置；8个独立feature上下文覆盖真实反馈距离8，各feature按原key顺序累加。
- 73110dc：QK16次/II3，循环50cycles、模块52cycles；ROW_SUM4次/II3，循环13cycles、模块15cycles；PWL循环12/模块14，PV循环87/模块89，均II1。stage0已纠正旧文档循环/模块口径混用，不是完整attention延迟。
- QK失败路径：PE返回7.048ns＋反馈store0.387ns＝7.435ns；ROW_SUM另加32-bit sparsemux0.453ns，达到7.887ns。200-880共8、200-871/200-1016各4；只改目标pragma不能消除真实顺序FP32反馈。
- PE_ARRAY_II当前默认D≤4为1、D16为5。PE_BANK_*、peBankMacUnit等为无效遗留拆分接口；Tcl不透传相关II/bank环境宏，改环境变量不等于改综合输入。stageAccumulatorResult仍被调用，不能作为死代码删除。
- TAPA启发已用于空间query分块、通信/物理证据驱动对照；未接入TAPA编译器或自动floorplanning，未实现距离感知HLS插级。hard区域实验失败；不能声称已完成全部TAPA物理研究。

## 4 已验证基线（均为历史）

| 版本/配置 | 验证范围 | 关键结果 |
|---|---|---|
| 25d6dda，4/16 | 阶段1/2完整HLS | 数学24/24、严格20/20、26/26 CoSim；唯一未分块PE/Acc，7.300ns，II5/5/1/1 |
| 130c778，4/16 | 阶段3完整HLS | 两个4×2块，总16PE/4Acc；数学/严格24/24、26 CoSim、7.300ns；80242cycles，较25d6dda降14.78%；FF45091/LUT129592/DSP40/BRAM8 |
| bf41d16，4/16 | 正式HLS/IP/内部OOC | 数学/严格24/24、26 CoSim、72696cycles，II5/5/1/1，7.300ns；tile RAMB18由40降0，AXI RAMB36仍7，不能把HLS BRAM8当物理总量 |
| dfbf82e硬件＋fb06b16脚本，4/16 | NM37真实HBM板测 | 普通26＋背压26全通过，16MC ready；完整系统setup＋0.261ns/hold＋0.010ns，CDC/bus-skew审查；RTL覆盖初始化、忙中复位与首笔普通事务，不能称52笔RTL通过 |
| ebe529b，4/16 | 时序修复CSim/CSynth | 数学/严格24/24、7.300ns，II5/5/1/1；DSP40/FF45881/LUT128820/BRAM8；QK循环81/模块83、ROW_SUM循环20/模块22、PWL循环12/模块14、PV循环87/模块89 |
| ebe529b，16/128 | 时序修复CSim/CSynth | 数学22/22＋2非法，7.300ns、ROW_SUM7.272ns，II10/10/5/5；PE latency9/II5；256PE/16Acc，DSP352/FF231565/LUT1213113/BRAM8 |

- ebe529b的ROW_SUM把row_sum广播给所有行，只提交选中行结果，保留逐位行为；此前71025ff估算7.390ns，相关旧测量报告不是当前性能。
- 16/128尚无当前双块结构严格位模式基线、CoSim/IP/实现/板测验收；旧7事务结果不能外推满tile/多tile，4配置latency3不能外推16配置。
- 板测last_case_cycles包含自检流程；N1024的PV发射下界419.4304ms及循环模型445.89056ms是旧II5的推算，非测量或当前交错候选预测。
- 阶段4auto内部OOC setup＋0.230/hold＋0.010ns；hard区域setup−0.081ns，被拒绝。用户曾把min0ns边界hold留完整系统，真实系统现已闭合；旧OOC失败不删除，16尚无物理验收。
- 生产fsa_stream历史CSim/CSynth/CoSim通过，SA主循环II1、7.300ns；无IP/物理/板测，保持不改。

## 5 阶段0结论与当前候选

1. 阶段0已完成，入口为[阶段0调度与账本](docs/Split-D阶段0调度与账本_20261012.md)，原[交接方案](docs/Split-D数据流与II1优化交接方案_20261012.md)继续提供数值、实例与时钟合同。模型复现为python docs/evidence/split_d_stage0_20261012/stage0_model.py；不运行DUT或HLS。
2. 决策：优先同坐标唯一求值的有限共享引擎＋空间ROW_SUM/PV，阶段1保留P完成及ROW_SUM/L完成屏障。4/16、假设hop4/Acc7时RS/PV窗口61拍，P装载再加假设1拍；后续去屏障窗口39拍，尚未实施或综合。
3. QK优先最少query交错，B空间归约保留对照。假设完整反馈4拍、服务II1，最小满速M为4，满4批次QK67拍；B假设hop4、每chunk装Q1/4拍为80/92拍。M=4、hop4、7.300ns修复均不是综合结论，实际M必须按反馈、bank和服务能力重选，不直接固定8/16。
4. 4配置每context Q/S/P/O/m/l保守payload512B，M4为2048B；flag/tag、FIFO、额外副本另算。当前满tile QK/RS/PV有用归约528MAC；空间候选减少丢弃行，不能靠复制MAC。masked/tail key原零FMA保留，原本不存在的causal未来tile不得新增L/O更新。
5. 96组预算仅含QK、RS/L/PV/O和理想KV冷装载，不是完整attention周期；未知softmax、倒数/归一化、输出、控制及AXI开销保留。N20 causal的固定3＋2批次部分预算优于4＋1，阶段3应比较尾部batch划分，不因增加M宣称无条件改善。
6. 用户已在具体提案后要求继续阶段1、2，实施按报告第10节空间ROW_SUM II等价口径：旧循环N/A、逐key顺序FMA、连续合法token服务II和真实事务间隔分开报告；单context没有每拍ROW_SUM需求，不能空发token验收。
7. 原4配置24项位模式冻结；新增16组固定输入配方/SHA已给出，已增加28笔测试、52笔数学总数，官方28笔3440word已从1afc13e未改DUT冻结；本地同stub差分只用于候选检查。16仍需独立旧源码固定输入差分，且历史PE II5必须先解决；只有4全部达标才测试16。CoSim/IP/Vivado/板测/1024长测关闭。

## 6 证据与历史入口

- 阶段0本地静态证据：docs/evidence/split_d_stage0_20261012/，包含模型、归档核对、逐拍CSV、A/B敏感性、96组部分预算和固定输入清单。对应[报告](docs/Split-D阶段0调度与账本_20261012.md)，没有新的数值或HLS验收。
- 当前日志：[2026-10-12_1437_split_d_stage12_修改日志.md](docs/修改日志/2026-10-12_1437_split_d_stage12_修改日志.md)。第1轮已闭合，完成轮数1；第2轮进行中；阶段1/2本地候选已实现，尚未官方验收。历史II1诊断为1148日志。
- 最新交接：[Split-D数据流与II1优化交接方案_20261012.md](docs/Split-D数据流与II1优化交接方案_20261012.md)。本地FSA/两示例固定版本、互联网主来源、候选取舍、工作包与数值/II/性能验收；替代直接固定上下文数的下一步，未实施。
- 当前完整证据：本地build/resume_nm37/ii1_round1_4x4/；远端build/split_d_73110dc_4x4_ii1_evidence_20261012/。247载荷SHA及92源码对73110dc已核对；tar SHA a41d69005492a12d863466e0163bffe5fe9e899c4efdc16ca8c946af37f15074。
- 远端日志/tmp/codex_split_d_73110dc_ii1_4x4_20261012.log；压缩包/tmp/codex_split_d_73110dc_4x4_ii1_evidence_20261012.tar.gz，本地build/resume_nm37/ii1_round1_4x4.tar.gz。
- 时序基线：[0850日志](docs/修改日志/2026-10-12_0850_split_d_critical_path_修改日志.md)，docs/evidence/split_d_ebe529b_{4x4,16x16}_timing/；远端build/split_d_ebe529b_{4x4,16x16}_timing_evidence_20261012/。
- [修正与TAPA启发方案](docs/Split-D修正与TAPA启发的分阶段修改验收方案_20261009.md)、[阶段1/2验收](docs/综合报告/Split-D阶段1与2验收_20261010.md)、[阶段3验收](docs/综合报告/Split-D阶段3空间分块验收_20261010.md)、[阶段4物理对照](docs/综合报告/Split-D阶段4物理对照_20261010.md)。
- [4×4真实HBM上板验收](docs/综合报告/Split-D_4x4真实HBM上板验收_20261011.md)、[1952板测日志](docs/修改日志/2026-10-11_1952_split_d_4x4_board_修改日志.md)、[1316实现日志](docs/修改日志/2026-10-10_1316_split_d_4x4_complete_修改日志.md)。工具tools/split_d_nm37/，证据docs/evidence/split_d_nm37_dfbf82e_board/；本地340文件自包含包build/resume_nm37/split_d_nm37_delivery_c59cbf4.tar.gz。
- [16/128旧测量](docs/综合报告/Split-D_16x16_head128_CSim与综合测量_20261011.md)，docs/evidence/split_d_71025ff_16x16/。NM37原理图docs/NM37_SCH_220331.pdf。
- 远端标准solution1最新是73110dc的4配置，运行会覆盖；必须按commit归档取证。本地build/fsa_stream_split_d_build/solution1是2026-09-28拆分前旧产物，不用于描述当前源码。

## 7 不能重复的错误

- 不恢复控制器同步请求/响应hls::task或Accumulator反馈DATAFLOW环：已证实RTL读空死锁，增FIFO/切FRP不能解决；不恢复pe_pipeline/cmp_pipeline inter false。
- 不让单DMA actor顺序写多路有限请求FIFO；不单凭函数定义/ALLOCATION推断共享。阶段3前两轮阵列复制/RTL错误由原始字广播修正，但具体缓存调度根因未最小化证明。
- 不把QK函数II、循环II、反馈latency或完整事务吞吐混用；H128 QK为128次逐feature，不是64拍。head点积与key softmax归约不同，D×P不能自动统一。
- 不用UG1399数组端口争用说明直接归因204-65；不把环境bank宏当空间拆分。保留现有层级，不混入未经验证的死代码清理。
- 16旧CoSim历史60分钟硬门槛保留，但7/7是旧用例数量，恢复CoSim时须按实际覆盖明确合同；当前不运行。

## 8 工作集、环境与复现

- 本地根C:/Users/30130/Desktop/workstation/FlashAttention/FSA_HLS；远端FSA-FPGA-NM37-tailBox:/home/zhangchenxuan/FSA_HLS；分支fsa_split_D，origin git@github.com:diego3893/FSA_HLS.git。
- 主要文件：include/fsa/stream/split_d/；src/stream/split_d/{split_d_controller,split_d_compute,split_d_dma,fsa_stream_split_d}.cpp；tests/stream/test_fsa_stream_split_d.cpp；hls/fsa_stream_split_d/run_hls.tcl、run_hls.sh。阶段0工作集为docs/Split-D阶段0调度与账本_20261012.md及docs/evidence/split_d_stage0_20261012/；当前DUT候选已改但未提交。
- 本地工具tools/local_split_d_check.cmd 4 16：MinGW＋仓库third_party/vitis_hls/include（含ap_int.h），FSA_LOCAL_MATH_STUBS/HLS_NO_XIL_FPO_LIB。数学24/24、严格19/24、exit1为既有5项stub位差，不能声称严格通过或替代官方验证。
- 每次SSH显式Bash加载~/.bashrc和/opt/Xilinx_2024.2/Vitis/2024.2/settings64.sh，再set -euo pipefail；先set-u会因settings读取未定义PYTHONPATH失败。PS脚本去CR并追加LF；原生Windows OpenSSH，避免MSYS signal pipe问题。
- Vitis HLS2024.2，xcvu37p_CIV-fsvh2892-2-e。下面命令仅供用户恢复后使用：

```bash
RUN_CSIM=1 RUN_COSIM=0 EXPORT_IP=0 FSA_SPLIT_D_PE_DIM=4 FSA_SPLIT_D_HEAD_DIM=16 FSA_SPLIT_D_LONG_CSIM_TEST=0 ./run_hls.sh fsa_stream_split_d
```

- 当前GitHub22握手曾连续超时，已用同origin、官方ssh.github.com:443和HostKeyAlias=github.com严格密钥验证成功；只按调用覆盖core.sshCommand，不改全局配置。远端origin正常22端口已成功。精确ff-only/HEAD/清洁tracked预检仍必须做。
- relevant技能：maintaining-project-context；恢复远端闭环时读fsa-hls-remote-iteration及其引用；报告/上板分别按vitis-hls-build-report、vivado-hls-ip-board-test。仓库skills/与用户安装副本可能不同，按当前可用技能读取。

## 9 对话接续摘要

用户要求继续阶段1和2，4×4/head16、逐query原FP32顺序/FP16量化、总16PE/4Acc、≤7.300ns和正式CSim/CSynth合同不变。当前HEAD2b0d51a已正常推送并ff-only同步NM37，包含共享引擎及batch支持；当前正式配置M1，CSim52数学/24原严格/28新严格及保护区、2非法全部通过，CSynth完成：共享循环II2、7.815ns，验收未过；第2轮准备Q广播和Acc输入寄存。用户明确授权精确GitHub目标后推送成功，旧DUT官方CSim/CSynth完成（52/52数学、24/24旧严格、7.887ns），完整构建已归档。本地共享有限引擎已接入空间ROW_SUM/PV并保留屏障，阶段1数学52/52和新增3440word同stub差分全相同，旧严格19/24/exit1是5项已知stub差异。阶段2query batch、causal后缀和尾批调度已写入，默认容量1，可配置M；M6数学52/52及3440word同stub差分通过，M1批次泛化后功能复核也通过，PE模块latency4/II1已测，但完整反馈仍需核对；当前hop5/feedback6/Acc11，第2轮加Q/负max列广播、Acc输入寄存，本地M1/M6均52数学及3440word差分通过，最小M不可认定。官方新增golden头已冻结28笔3440word，来源manifest在docs/evidence/split_d_stage12_20261012/，不允许候选再生成。实际RTL确认两个共享引擎/PE/Acc；第1轮II2来自Q重复读端口、7.815ns来自O读取/选择＋Acc输入。下一步显式列Q广播＋Acc输入寄存，复测M1，再按实际路径验证最小M。CoSim/IP/16/板测/1024长测关闭。当前日志1437，第1轮未闭合，不宣称阶段1/2完成。
