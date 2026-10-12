# FSA HLS Project Context

> 2026-10-12压缩版，已补本地源码与联网调研交接。只维护仓库根目录本文件；历史全文可从Git提交d3245a3恢复，实验过程保留于原报告、修改日志和证据目录。计算实施仍暂停；本次仅编写方案。

## 1 当前状态与任务

- 用户目标：先在4×4/head16把QK/ROW_SUM/PWL/PV实际发射II全部降到1并改善性能，达标后才测试16×16/head128；不进行CoSim。
- 最新任务：在交错实现前读取本地FSA、Split-D与两组示例，联网调研并编写后续对话的交接方案；不恢复实现或测试。第1轮已结束，第2轮未启动。2026-10-12 12:18只读确认NM37无Vitis/Vivado/XSIM/run_hls进程；这是当时快照，不代表后续实时状态。
- 被测代码：73110dc2d6eb799072c29fc4954f8f8d5427973b；仅将QK/ROW_SUM两处PIPELINE目标II5→1，未改变算术或状态。收尾文档d3245a3已推送；文档提交不算HLS重新测试。
- 官方4配置流程11:58:47—12:02:27退出0：数学/严格位模式24/24及2笔非法长度保护通过；实际II3/3/1/1，顶层估算7.887ns，II与≤7.300ns时序门槛未通过。
- 资源：DSP40/FF44855/LUT128938/BRAM18K8；实际2个PE块、2个Accumulator块，总16条PE乘法、4条Acc乘法和8条减法，未复制MAC。
- 用户已明确选择“四阶段都到II1，允许交错独立query上下文并增加状态存储”。新增Q/S/P/m/l/O上下文允许，逐query的FMA/量化顺序及MAC实例数保持；交错结构尚未实现。
- CoSim、IP导出、Vivado、板测、16配置及1024长测均未在本轮执行。没有1024序列硬件实测延迟。

## 2 授权、验收与禁止事项

- 当前暂停；只有用户恢复后才能推进下一轮。Git/SSH既有授权仍有效，可维护任务文档；不得把Git授权扩展成CoSim/IP/板测授权。
- 保持独立顶层fsa_stream_split_d、四路64-bit AXI master和AXI-Lite、VU37P器件、100MHz/10ns时钟、2.7ns uncertainty；不修改FSA-main或生产顶层fsa_stream。
- 每配置总D×D条PE RawFMA、D条Accumulator及既有倒数通路；允许两个D×B空间query块，禁止复制计算通路换吞吐。源码函数唯一、ALLOCATION或总DSP单项均不足以证明实例数。
- 保持FP16×FP16＋FP32 PE、FP32 Accumulator、RawFMA特殊值/舍入、PWL及FP32 max→FP16→SUB_MAX→SCALE→PWL量化顺序；不改变金标准/0.03容差、不重排逐query归约。
- 旧“每PE单份状态且禁止额外S/P副本”约束已被用户对本次II1候选的明确选择放宽；只放宽独立query上下文状态。报告新增容量、用途、批次总周期、折算tile吞吐与短序列空槽成本。
- 真实依赖不能标DEPENDENCE false。单元latency与II不同：依赖距离×II需覆盖完整反馈路径，并满足服务能力；交错后的发射II1不等于同一query每拍反馈。
- 每轮读取精确commit的功能、时序、阶段循环II/trip count/cycles、warning、RTL层次/运算符、资源后闭环。不以流程exit0宣称性能通过，也不沿用旧bitstream验收新源码。
- 标准入口仅仓库run_hls.sh；禁止替代Tcl、wrapper、临时testbench或收窄覆盖规避失败。允许在既有测试台补强覆盖；证据提取不替代测试流程。
- 当前明确范围为CSim＋CSynth，RUN_COSIM=0/EXPORT_IP=0/LONG_CSIM=0；默认Tcl包含CoSim/IP，执行时必须显式关闭。无轮数上限不撤销暂停指令。
- 不force push、reset/stash/checkout丢弃、git clean或批量暂存；只提交审查过的任务路径。保留用户AGENTS.md修改、.tmprun/、docs/evidence/split_d_9c49789_4x4/local_math_stubs_check.log及远端无关文件。
- C/C++和Markdown格式遵循用户最新AGENTS.md；函数签名与左花括号同一行，if/for括号前不加空格，逗号后空格；中文与英文/数字/行内代码之间不额外加空格。

## 3 当前架构与失败机理

- 输入分发→两个模板runQueryBlock→单输出汇聚为前向DATAFLOW；header FIFO深度2，Q/KV/O深度8。原始64-bit Q/128-bit KV字传输，在worker中解包。worker消费全部key输入后输出；需随结构变更重新审查有限FIFO等待关系。
- D4/B2和D16/B8分别总16/256PE；各worker内部QK、SUB_MAX、SCALE、PWL、ROW_SUM、PV共享runPeArray，FP32列运算共享runAccumulatorColumns。全局D/H与DMA步长不得替换成局部B。
- Q/K/V均按[token][feature]装载；PV消费方式不同。PV每次仅一条key行有效，总D个有效MAC位置；8个独立feature上下文覆盖真实反馈距离8，各feature按原key顺序累加。
- 73110dc：QK16次/52cycles/II3、ROW_SUM4次/15cycles/II3；PWL/PV II1，runPeArray latency3/II1。上述cycles是循环调度指标，不是完整attention延迟。
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
| ebe529b，4/16 | 时序修复CSim/CSynth | 数学/严格24/24、7.300ns，II5/5/1/1；DSP40/FF45881/LUT128820/BRAM8；QK16次/81cycles、ROW_SUM4次/20cycles、PWL8次/12cycles、PV80次/87cycles |
| ebe529b，16/128 | 时序修复CSim/CSynth | 数学22/22＋2非法，7.300ns、ROW_SUM7.272ns，II10/10/5/5；PE latency9/II5；256PE/16Acc，DSP352/FF231565/LUT1213113/BRAM8 |

- ebe529b的ROW_SUM把row_sum广播给所有行，只提交选中行结果，保留逐位行为；此前71025ff估算7.390ns，相关旧测量报告不是当前性能。
- 16/128尚无当前双块结构严格位模式基线、CoSim/IP/实现/板测验收；旧7事务结果不能外推满tile/多tile，4配置latency3不能外推16配置。
- 板测last_case_cycles包含自检流程；N1024的PV发射下界419.4304ms及循环模型445.89056ms是旧II5的推算，非测量或当前交错候选预测。
- 阶段4auto内部OOC setup＋0.230/hold＋0.010ns；hard区域setup−0.081ns，被拒绝。用户曾把min0ns边界hold留完整系统，真实系统现已闭合；旧OOC失败不删除，16尚无物理验收。
- 生产fsa_stream历史CSim/CSynth/CoSim通过，SA主循环II1、7.300ns；无IP/物理/板测，保持不改。

## 5 恢复后的第一步（仅计划）

1. 以[数据流与II1优化交接方案](docs/Split-D数据流与II1优化交接方案_20261012.md)为新入口，先完成工作包0的逐拍调度、数值顺序与有效MAC/存储账本，不直接实施固定8/16上下文。原交错授权仍有效，具体架构尚未确定。
2. 新推荐是同坐标唯一求值的有限共享引擎：优先评估ROW_SUM/PV沿key行传递FP32部分和；QK比较最少query上下文与feature-chunk空间归约。后者有H/D次装Q、部分和返回与填充成本，小D未必更快。
3. 原FSA的Q仅在QK驻留，每个key tile仍重装Q，softmax改写为S/P；Python限制head宽匹配阵列行，不能直接当作H>D的实现。gemm_hls用独立C地址覆盖反馈，不证明同一累加变量每拍更新。
4. 空间chunk必须将前chunk的FP32部分和作为下一chunk首个FMA输入，不能独立局部求和后相加。当前PV每调用仅一key行有效，II1仍有约1/D有效MAC率；先核对有效吞吐，不能仅追pragma。
5. 如空间ROW_SUM删除旧D次循环，须在实施前由用户确认连续合法token服务II与遍历latency的等价验收口径；不能自动宣布原四阶段II1完成。先4功能/位模式/实例/7.300ns与有效周期，再同候选16；所有测试仍待恢复。
6. build/ii1_original保留供差分，原4配置24项位模式冻结；16需补固定输入旧源码差分。CoSim/IP/Vivado/板测与1024长测保持关闭。当前不使用TAPA工具链，物理方案留后续授权。

## 6 证据与历史入口

- 当前日志：[2026-10-12_1148_split_d_ii1_修改日志.md](docs/修改日志/2026-10-12_1148_split_d_ii1_修改日志.md)。已闭环1轮，验收未通过，待恢复方案未实施。
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
- 主要文件：include/fsa/stream/split_d/；src/stream/split_d/{split_d_controller,split_d_compute,split_d_dma,fsa_stream_split_d}.cpp；tests/stream/test_fsa_stream_split_d.cpp；hls/fsa_stream_split_d/run_hls.tcl、run_hls.sh。当前结构候选尚无未提交源码修改。
- 本地工具tools/local_split_d_check.cmd 4 16：MinGW＋仓库third_party/vitis_hls/include（含ap_int.h），FSA_LOCAL_MATH_STUBS/HLS_NO_XIL_FPO_LIB。数学24/24、严格19/24、exit1为既有5项stub位差，不能声称严格通过或替代官方验证。
- 每次SSH显式Bash加载~/.bashrc和/opt/Xilinx_2024.2/Vitis/2024.2/settings64.sh，再set -euo pipefail；先set-u会因settings读取未定义PYTHONPATH失败。PS脚本去CR并追加LF；原生Windows OpenSSH，避免MSYS signal pipe问题。
- Vitis HLS2024.2，xcvu37p_CIV-fsvh2892-2-e。下面命令仅供用户恢复后使用：

```bash
RUN_CSIM=1 RUN_COSIM=0 EXPORT_IP=0 FSA_SPLIT_D_PE_DIM=4 FSA_SPLIT_D_HEAD_DIM=16 FSA_SPLIT_D_LONG_CSIM_TEST=0 ./run_hls.sh fsa_stream_split_d
```

- 当前GitHub22握手曾连续超时，已用同origin、官方ssh.github.com:443和HostKeyAlias=github.com严格密钥验证成功；只按调用覆盖core.sshCommand，不改全局配置。远端origin正常22端口已成功。精确ff-only/HEAD/清洁tracked预检仍必须做。
- relevant技能：maintaining-project-context；恢复远端闭环时读fsa-hls-remote-iteration及其引用；报告/上板分别按vitis-hls-build-report、vivado-hls-ip-board-test。仓库skills/与用户安装副本可能不同，按当前可用技能读取。

## 9 对话接续摘要

本对话从核查TAPA及DeepSeek交接开始，已修测试判错与实例证据、完成两个query空间块、OOC对照及4×4真实HBM上板。16/128广播修复达到7.300ns，但吞吐未改善。四阶段II1诊断73110dc得到4配置II3/3/1/1和7.887ns，功能/实例通过、性能未验收，交错未实施，计算任务已暂停。用户随后要求实现前读取FSA、Split-D与两示例并联网形成交接方案；本次仅完成调研文档。新入口见第5/6节：先调度账本，再比较最少query交错与空间部分和，关注PV低有效MAC率；原FSA每key tile重装Q，不能理想化为永驻Q。保持原MAC、数值、时钟与4先16后合同；空间ROW_SUM的II等价口径待实施前明确，不CoSim。旧详细全文在d3245a3。
