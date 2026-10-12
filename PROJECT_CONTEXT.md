# FSA HLS Project Context

> 2026-10-12压缩版，阶段0、1、2、3均已完成；4配置供数/状态/尾批优化已通过CSim/CSynth。只维护仓库根目录本文件；历史全文可从Git提交d3245a3恢复。

## 1 当前状态与任务

- 22:08阶段4第1轮已闭环：官方6f0b40d M1/16/128，数学32/32、旧官方85824输出字严格差分0，top7.300ns、共享实际II1/Depth46、PE数组lat4/II1、Acc lat7/II1，RTL256PE/16Acc/32减法。DSP356、FF483088、LUT1418632超过1303680约8.8%，BRAM8；尚不能交付。归档build/split_d_stage4_6f0b40d_M1_20261012，run21:58:39/exit0。第2轮改D16 Q/K/V/O ram_t2p/bram/latency1、S/P ram_2p/lutram/latency1（bank和生命周期不变），验证默认M8/hop7/feedback8/Acc11＋L_MAX1024；等待推送正式冻结header和新源码。新增资源容量验收门槛。下方M1“仍运行”的旧接续记录已由本条覆盖。

- 2026-10-12T20:20+08:00最新用户指令恢复阶段4：16×16/head128，随后明确不跑1024长测试。CSim/CSynth及既有Git/SSH授权持续；CoSim/IP/Vivado/板测仍关闭。当前日志2026-10-12_2020_split_d_stage4_修改日志.md；先补16旧源码独立冻结输出，再重新验PE服务/hop/反馈/M/bank。下面阶段3“16关闭/不启动”的文字属于此前边界，已被本次16恢复覆盖。第1轮旧基线/服务诊断L_MAX4096，最终验收显式L_MAX1024；LONG_CSIM_TEST=0。

- 用户任务：“继续阶段3”已完成。仅4×4/head16，小batch前置＋状态生命周期复用/首key alpha冗余消除，经两轮同输入/模型/资源消融验收。原FP32顺序/FP16量化、16PE/4Acc、实际II1、VU37P/10ns/2.7ns/7.300ns保持。持续授权同仓库/分支推送及NM37标准CSim/CSynth；CoSim/IP/16/板测/1024长测关闭。日志1842，无轮数上限。
- 阶段3最终被测16fa1525332110188544031350e806d54dcfd90c：官方数学52/52、旧严格24/24、新严格28/28及保护检查通过；II1/depth46、7.300ns、DSP40/FF103620/LUT179562/BRAM8，RTL16PE/4Acc/8减法。尾批消融cd7d064同门槛通过，FF105480/LUT180448。小batch前置经DP1至1024tile证明；N36 causal模型12230→7492拍、KV17→10；N3为670→548但仍高于ebe529b235。删除next_max/alpha_delta/inverse_sum，payload4608→4224B（−8.3%）。两轮精确归档build/split_d_stage3_{cd7d064_tail,16fa152_state}_20261012/；本地3440word差分0，stub19/24限制不变。阶段3完成，不自动启动16迁移。
- 被测DUT提交902838ec6a2cefd24368e7be42e82d6ca29bc28f，第4轮M1诊断完整通过：数学52/52、旧严格24/24、新严格28/28、输出canary/2非法；正确S真实依赖下共享II1/depth46、顶层7.300ns，DSP40/FF71968/LUT148213/BRAM8/URAM0。阶段1已通过约定CSim/CSynth门槛。
- 阶段2最终被测代码`02b16663c14b84fda80446376385c04fce596656`已通过默认M8正式CSim/CSynth：容量8、PE年龄7、QK反馈8、S提交距离1、Acc年龄11。数学52/52、旧严格24/24、新严格28/28及保护检查通过；共享实际II1/depth46、顶层7.300ns、DSP40/FF105482/LUT180509/BRAM8/URAM0，实际16PE/4Acc/8减法。阶段1、2完成。
- 数值基线1afc13e保持73110dc DUT未改，官方28笔3440word已固定于tests/stream/split_d_stage12_baseline.hpp；来源/SHA在official_baseline_manifest.json，禁止候选改golden。完整N24/28/32覆盖M7−1/M7/M7＋1，N36覆盖尾批；全部52笔数学/52笔严格与2非法不收窄。
- 有限DATAFLOW distributor→两个D×B worker→gather；Q整批先装载、KV每key tile广播一次，causal只处理合法query tile后缀，尾batch显式计数并按原query顺序汇聚。单一PE/Acc所有者，空间ROW_SUM/PV保持key顺序，P完整/L完整两道屏障及phase排空保留。
- 第1轮Q重复读取端口II2/Acc输入7.815ns，第2轮Q/负max广播及Acc输入寄存使工具II1但V→PE7.961ns，第3轮PE输入寄存达7.300ns但age5真实依赖导致II2。审查纠正了前三轮错误的S distance：必须feedback−hop而非整个发射间距；此前II报告不能作为最终正确依赖验收。第4轮age6/反馈7/S距离1实际II1通过。
- 实际RTL完整递归（处理属性/缩进/多module/参数实例）确认16PE乘法/4Acc乘法/8减法；寄存helper无新MAC。所有实际构建按commit归档；第4轮额外保存精确源码快照。top latency仍未知，只允许报告明确范围的HLS阶段/模块预测预算，禁止冒充RTL/板上实测。
- 本地stub各候选数学52/52及新增3440word差分0，旧严格19/24/exit1为5项已知差异，不能代替官方。日志1437已闭合10轮：第7轮M8实际II1但数值转换写回7.625ns；第8、9轮转换helper为7.300ns但反馈II2；第10轮用viewEasA/viewAasE在score槽暂存最终FP16 P位模式，最终同时达到II1和7.300ns。

## 2 授权、验收与禁止事项

- 用户本次明确恢复阶段3；已明确允许该GitHub仓库/分支推送及NM37同步测试，授权未撤回。沿用阶段0空间ROW_SUM口径；阶段4和CoSim/IP/板测不由本次指令启动。
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
- Q/K/V均按[token][feature]装载；当前空间PV按context→feature逐拍向row0发射，部分和以hop7沿key0→key3推进，不再逐行同步求值/丢弃其他行。历史73110dc的PV才是每次仅一条key行有效、8个feature上下文反馈。
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

## 5 阶段0结论与阶段1、2落地

1. 阶段0已完成，入口为[阶段0调度与账本](docs/Split-D阶段0调度与账本_20261012.md)，原[交接方案](docs/Split-D数据流与II1优化交接方案_20261012.md)继续提供数值、实例与时钟合同。模型复现为python docs/evidence/split_d_stage0_20261012/stage0_model.py；不运行DUT或HLS。
2. 决策：优先同坐标唯一求值的有限共享引擎＋空间ROW_SUM/PV，阶段1保留P完成及ROW_SUM/L完成屏障。4/16、假设hop4/Acc7时RS/PV窗口61拍，P装载再加假设1拍；后续去屏障窗口39拍，尚未实施或综合。
3. QK优先最少query交错，B空间归约保留对照。假设完整反馈4拍、服务II1，最小满速M为4，满4批次QK67拍；B假设hop4、每chunk装Q1/4拍为80/92拍。M=4、hop4、7.300ns修复均不是综合结论，实际M必须按反馈、bank和服务能力重选，不直接固定8/16。
4. 4配置每context Q/S/P/O/m/l保守payload512B，M4为2048B；flag/tag、FIFO、额外副本另算。当前满tile QK/RS/PV有用归约528MAC；空间候选减少丢弃行，不能靠复制MAC。masked/tail key原零FMA保留，原本不存在的causal未来tile不得新增L/O更新。
5. 96组预算仅含QK、RS/L/PV/O和理想KV冷装载，不是完整attention周期；未知softmax、倒数/归一化、输出、控制及AXI开销保留。N20 causal的固定3＋2批次部分预算优于4＋1，阶段3应比较尾部batch划分，不因增加M宣称无条件改善。
6. 用户已在具体提案后要求继续阶段1、2，实施按报告第10节空间ROW_SUM II等价口径：旧循环N/A、逐key顺序FMA、连续合法token服务II和真实事务间隔分开报告；单context没有每拍ROW_SUM需求，不能空发token验收。
7. 原4配置24项位模式冻结；新增16组固定输入配方/SHA已给出，已增加28笔测试、52笔数学总数，官方28笔3440word已从1afc13e未改DUT冻结；本地同stub差分只用于候选检查。16仍需独立旧源码固定输入差分，且历史PE II5必须先解决；只有4全部达标才测试16。CoSim/IP/Vivado/板测/1024长测关闭。

## 6 证据与历史入口

- 阶段0本地静态证据：docs/evidence/split_d_stage0_20261012/，包含模型、归档核对、逐拍CSV、A/B敏感性、96组部分预算和固定输入清单。对应[报告](docs/Split-D阶段0调度与账本_20261012.md)，没有新的数值或HLS验收。
- 当前日志：[阶段3日志1842](docs/修改日志/2026-10-12_1842_split_d_stage3_修改日志.md)，闭合2轮；正式报告[供数状态与尾批验收](docs/综合报告/Split-D阶段3_供数状态与尾批验收_20261012.md)，证据docs/evidence/split_d_stage3_20261012/。
- 阶段1、2日志[1437](docs/修改日志/2026-10-12_1437_split_d_stage12_修改日志.md)闭合10轮；第4轮阶段1、第10轮阶段2通过。[阶段1与2报告](docs/综合报告/Split-D阶段1与2_共享引擎与II1验收_20261012.md)。历史II1诊断为1148日志。
- 实施来源：[Split-D数据流与II1优化交接方案_20261012.md](docs/Split-D数据流与II1优化交接方案_20261012.md)。本地FSA/两示例固定版本、互联网主来源、候选取舍、工作包与数值/II/性能验收已转化为阶段0账本和阶段1、2实现。
- 阶段1、2完整证据：本地docs/evidence/split_d_stage12_20261012/；远端build/split_d_stage2_02b1666_defaultM8_20261012/。机器摘要、调度脚本、满批/短批预算、逐拍token表和序列预算均已归档。
- 第1轮73110dc诊断证据仍保存在本地build/resume_nm37/ii1_round1_4x4/及远端build/split_d_73110dc_4x4_ii1_evidence_20261012/，用于与最终结果对照，不再代表当前DUT。
- 时序基线：[0850日志](docs/修改日志/2026-10-12_0850_split_d_critical_path_修改日志.md)，docs/evidence/split_d_ebe529b_{4x4,16x16}_timing/；远端build/split_d_ebe529b_{4x4,16x16}_timing_evidence_20261012/。
- [修正与TAPA启发方案](docs/Split-D修正与TAPA启发的分阶段修改验收方案_20261009.md)、[阶段1/2验收](docs/综合报告/Split-D阶段1与2验收_20261010.md)、[阶段3验收](docs/综合报告/Split-D阶段3空间分块验收_20261010.md)、[阶段4物理对照](docs/综合报告/Split-D阶段4物理对照_20261010.md)。
- [4×4真实HBM上板验收](docs/综合报告/Split-D_4x4真实HBM上板验收_20261011.md)、[1952板测日志](docs/修改日志/2026-10-11_1952_split_d_4x4_board_修改日志.md)、[1316实现日志](docs/修改日志/2026-10-10_1316_split_d_4x4_complete_修改日志.md)。工具tools/split_d_nm37/，证据docs/evidence/split_d_nm37_dfbf82e_board/；本地340文件自包含包build/resume_nm37/split_d_nm37_delivery_c59cbf4.tar.gz。
- [16/128旧测量](docs/综合报告/Split-D_16x16_head128_CSim与综合测量_20261011.md)，docs/evidence/split_d_71025ff_16x16/。NM37原理图docs/NM37_SCH_220331.pdf。
- 远端标准solution1最新是阶段3的16fa152默认M8构建，运行会覆盖；已按commit归档，每轮须再核对。本地build/fsa_stream_split_d_build/solution1是2026-09-28拆分前旧产物，不用于描述当前源码。

## 7 不能重复的错误

- 不恢复控制器同步请求/响应hls::task或Accumulator反馈DATAFLOW环：已证实RTL读空死锁，增FIFO/切FRP不能解决；不恢复pe_pipeline/cmp_pipeline inter false。
- 不让单DMA actor顺序写多路有限请求FIFO；不单凭函数定义/ALLOCATION推断共享。阶段3前两轮阵列复制/RTL错误由原始字广播修正，但具体缓存调度根因未最小化证明。
- 不把QK函数II、循环II、反馈latency或完整事务吞吐混用；H128 QK为128次逐feature，不是64拍。head点积与key softmax归约不同，D×P不能自动统一。
- 不用UG1399数组端口争用说明直接归因204-65；不把环境bank宏当空间拆分。保留现有层级，不混入未经验证的死代码清理。
- 16旧CoSim历史60分钟硬门槛保留，但7/7是旧用例数量，恢复CoSim时须按实际覆盖明确合同；当前不运行。

## 8 工作集、环境与复现

- 本地根C:/Users/30130/Desktop/workstation/FlashAttention/FSA_HLS；远端FSA-FPGA-NM37-tailBox:/home/zhangchenxuan/FSA_HLS；分支fsa_split_D，origin git@github.com:diego3893/FSA_HLS.git。
- 主要文件：include/fsa/stream/split_d/；src/stream/split_d/{split_d_controller,split_d_compute,split_d_dma,fsa_stream_split_d}.cpp；tests/stream/test_fsa_stream_split_d.cpp；hls/fsa_stream_split_d/run_hls.tcl、run_hls.sh。阶段3修改types/compute/controller；证据docs/evidence/split_d_stage3_20261012/，日志1842。
- 本地工具tools/local_split_d_check.cmd 4 16：MinGW＋仓库third_party/vitis_hls/include（含ap_int.h），FSA_LOCAL_MATH_STUBS/HLS_NO_XIL_FPO_LIB。数学24/24、严格19/24、exit1为既有5项stub位差，不能声称严格通过或替代官方验证。
- 每次SSH显式Bash加载~/.bashrc和/opt/Xilinx_2024.2/Vitis/2024.2/settings64.sh，再set -euo pipefail；先set-u会因settings读取未定义PYTHONPATH失败。PS脚本去CR并追加LF；原生Windows OpenSSH，避免MSYS signal pipe问题。
- Vitis HLS2024.2，xcvu37p_CIV-fsvh2892-2-e。下面命令仅供用户恢复后使用：

```bash
RUN_CSIM=1 RUN_COSIM=0 EXPORT_IP=0 FSA_SPLIT_D_PE_DIM=4 FSA_SPLIT_D_HEAD_DIM=16 FSA_SPLIT_D_LONG_CSIM_TEST=0 ./run_hls.sh fsa_stream_split_d
```

- 当前GitHub22握手曾连续超时，已用同origin、官方ssh.github.com:443和HostKeyAlias=github.com严格密钥验证成功；只按调用覆盖core.sshCommand，不改全局配置。远端origin正常22端口已成功。精确ff-only/HEAD/清洁tracked预检仍必须做。
- relevant技能：maintaining-project-context；恢复远端闭环时读fsa-hls-remote-iteration及其引用；报告/上板分别按vitis-hls-build-report、vivado-hls-ip-board-test。仓库skills/与用户安装副本可能不同，按当前可用技能读取。

## 9 对话接续摘要

最新22:08：第1轮已完成，完整数据见第1节首条及日志2020。关键限制是LUT超容量；第2轮存储绑定候选已写入controller/compute，Q/K/V/O BRAM＋S/P LUTRAM，仅D16；本地16/128/M8/L_MAX1024数学32/32、对官方严格21/32（同11项stub位差），与旧同stub全部85824字差分0已确认。待审查/推送任务路径（含从旧官方生成的32项冻结头），远端标准入口CAPTURE_STAGE4=unset，全部engine覆盖unset，D16/H128/L_MAX1024、LONG_CSIM=0。M1标量mul_32s_7ns_32_1_1总2个解释额外4DSP，不是复制MAC。下方是较早过程记录，不表示M1仍在运行。

最新工作为阶段4，进行中。第1轮建立16测试冻结：22原有效＋2非法保持，追加固定N112/128/144因果/非因果、N33消去/带符号零，共32有效。f7e2839测试/Tcl叠加独立旧源码ebe529b，远端build/split_d_stage4_baseline_source_20261012；正式旧基线PID2129143。M1诊断6f0b40d只改变16 PE服务目标5→1，hop7/反馈8/Acc11待实际报告验收，PID2129587。日志/tmp/codex_split_d_stage4_{baseline,6f0b40d_M1}_20261012.log；两者CAPTURE_STAGE4=1事后差分，最终候选必须unset并使用冻结header。本地16 M8数学32/32/exit0，捕获不能作旧golden。第1轮未闭环，下一轮不得提前启动；既有testbench新增捕获/差分。标准Tcl仅优化宿主CSim（-O），不改变综合CFLAGS。最终验收L_MAX1024，禁止N1024测试、CoSim/IP/Vivado/板测。下段为阶段3已完成历史摘要。

21:16新增已确认：旧官方32/32 CSim完成，精确核对baseline vitis_hls PID2129335后TERM停止其重复综合，RUN_EXIT1属受控退出，不是功能失败；旧综合引用既有ebe529b报告。当前M1官方数学32/32，与旧官方128736输入字/85824输出字全部差分0；tests/stream/split_d_stage4_baseline.hpp已从旧官方冻结，manifest/freeze_stage4.py在docs/evidence/split_d_stage4_20261012/。M1 CSynth仍运行，尚无最终调度/时序/资源，不能宣布阶段4通过。当前M8本地与旧本地全部输入输出差分0；两者各数学32/32、对官方严格21/32，相同11项stub差异。tools/local_split_d_check.cmd加-O2（宿主限定，无fast-math），本地日志build/local_split_d_check/stage4_*。最终header/M8配置还未推送，本地HEAD6f0b40d；保留未提交当前日志/上下文/生成证据/TB长用例兼容guard。第1轮等待M1完整CSynth后闭环，再以实际报告决定M/hop并推送正式默认配置及冻结基线。

用户要求阶段3，现已完成2轮官方闭环。最终代码16fa1525332110188544031350e806d54dcfd90c：M8/hop7/feedback8/S距离1/Acc11，官方数学52/52、旧严格24/24、新严格28/28及保护检查，II1/depth46/顶层7.300ns，DSP40/FF103620/LUT179562/BRAM8，RTL16PE/4Acc/8减法；无II/时序违例。小batch前置（N36为1＋8）、running_max/alpha复用删除3份列数组、首key省略两次冗余alpha，数值payload4608→4224B。阶段2基线02b1666及tail-only cd7d064同输入消融保留；N36 causal模型12230→7492拍（−38.7%）、KV17→10，N3为670→548但对ebe529b235仍退化。报告Split-D阶段3_供数状态与尾批验收_20261012.md、日志1842、证据docs/evidence/split_d_stage3_20261012/由最终文档提交交付；远端build/split_d_stage3_{cd7d064_tail,16fa152_state}_20261012/均精确归档。源码测试闭环已结束，不再改DUT或重跑测试。top latency仍undef；CoSim/IP/16/Vivado/板测/1024关闭，阶段4须用户继续后重新选择16服务/hop/M/bank。Git/SSH持续授权；实际生成目录fsa_stream_split_d_build，用户AGENTS.md、.tmprun/、旧stub日志不提交。
