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

### OOC自动布局执行

- OOC脚本/证据提交8437255bf9ca8f8e6cd4aa62575410dc4adabc5d已推送并远端ff-only核对。运行前验证正式IP ZIP SHA256为d7e9e8cd4d5e2a6034d43bd200df4f8ba5f259de23df63ad3dd60e3f7fa23e03。
- 命令：`vivado -mode batch -source tools/split_d_ooc/run.tcl -tclargs hls/fsa_stream_split_d/fsa_stream_split_d_build/solution1/impl/verilog build/split_d_ooc_b3e4957/auto auto`；日志`/tmp/codex_split_d_b3e4957_ooc_auto_20261010.log`。
- 本地逐word核对24项全部相同，C/RTL输出SHA与25d6dda相同；详见本次verification.json。正式导出原始component.xml和全部impl指纹已提交；仿真注入的字节差异如实列出。
- Vivado已进入Timing Optimization，暂未得到实现时序。正式CoSim WDB和performance transaction XML已定位，尚未读取重叠时标。

### 新物理与并行证据（尚未闭环）

- Vivado综合网表：LUT51723（Logic48965/LUTRAM640/SRL2118）、FF26495、RAMB36=7/RAMB18=40、DSP40。分发器LUT452/FF918/SRL9；其K/V流水子模块LUT273/FF441/SRL9。HLS寄存器估算并非最终物理数量，撤回“仅凭10724FF优先改广播”的依据；需实际关键路径。
- 正式profiling压缩统计从.autopilot/db读取，无重跑/自建测试。60次worker启动，两块运行窗口交集68859cycles；两条QK循环各177次、区间完全一致，交集14160cycles（177×80）。例：QK0/QK1均[260,340)、[944,1024)、[1651,1731)。这是实际RTL活动窗口重叠证据，不声称所有PE每拍有效。
- 原始process/channel/loop ZIP、ID映射和推导结果在本次evidence/profile；monitor计数总80245，包含仿真启动采样偏移，不能替换顶层80242事务统计。WDB275MiB留在服务器；无需下载全量波形才证明已采样的QK重叠。

### 第1轮分析闭环

- 已解决：正式全流程/IP，24项位模式与基线完全相同，实例/II/资源保持；官方profiling证明真实QK并行，K/V峰值8、block1输出write stall1938cycles而26事务正常完成；OOC route完成，77220条可路由net全部完成、route error0。全部12项check_timing均0，无timing exception。
- 自动布局结果：setup WNS0.371ns/TNS0，hold WHS−0.080ns/THS−36.648ns，771失败endpoint。布线资源LUT49200/FF26459/RAMB36=7/RAMB18=40/DSP40。无SLR跨越、无level>5拥塞窗口；最差setup在worker0的FP32减法/softmax块内，delay6.910ns，logic4.078/route2.832，19级。不能据此给广播盲目插级。
- 仍存在：hold未通过（已见最差AXI输入→首级FDRE，边界0ns min+虚拟未路由input）；尚未完整分类所有771路径；区域对照未做，完整系统仍不在本次范围。DRC有DPIP/DPOP/RTSTAT warning、无Error/Critical Warning；TIMING-44提示2.7ns余量较大，保留用户固定余量，不放宽。
- 标准状态：功能/HLS/IP/route/setup/约束覆盖通过，hold失败，阶段4未验收。自动布局报告/实际位置已取回`build/stage4_b3e4957/auto/physical.tar.gz`（717573bytes）。已完成分析闭环1轮。
- 下一轮：保持所有时序约束，从同一routed.dcp分类内部/边界hold并尝试工具的post-route hold_fix；真实位置用于后续区域对照，不修改HLS数值。

## 3 第2轮：hold分类与物理修复

- 本轮从第1轮数据分析结束后开始；目标先解决WHS/THS，或获得模型边界不可修复的直接证据。新增repair_hold.tcl只读取routed.dcp、报告全部失败endpoint及内部hold，调用官方post-route hold_fix和路由，边界预算/clock/uncertainty全部保持。
- 原理依据：[UG8352024.2 phys_opt_design](https://docs.amd.com/r/2024.2-English/ug835-vivado-tcl-commands/phys_opt_design)明确hold_fix需显式开启，默认Default不包含此修复。本轮无C++/测试改变，复用正式b3e4957 IP；只增加布线/等价延迟缓冲，不增加算法事务周期。
- 首次脚本a0b890d读取DCP及内部hold成功（最差+0.019ns），列出全部负hold；但`phys_opt_design -post_route -hold_fix`的-post_route不是2024.2合法选项，exit1。修正为`phys_opt_design -hold_fix`，工具依已路由design state识别post-route；保留失败目录，使用auto_hold_retry重试，属于同一轮命令修正。
- 读取DCP时发现Timing38-242：未设HD.CLK_SRC，时钟偏斜估计受限。此前自动布局仅初步模型结果，不能作为完整物理验收。新增实际器件BUFGCE库存记录，后续以明确OOC时钟源假设重新固定自动/区域模型。周期/uncertainty及边界预算不放宽。
- TSV初版误输出字面量反斜杠t；修复以后脚本的分隔符，原自动布局文件保持原始字节，分析读取时识别该分隔符。不覆盖既有证据。
- 更正上一条：进一步读取原始字节，TSV分隔符实际为ASCII9（真正tab）；此前误把工具结果的转义显示当作文件内容。脚本对应替换没有产生run.tcl差异，原始TSV并未损坏，无需修复。

### 第2轮分析闭环

- a2d61f6的auto_hold_retry正常完成。以最终timing_summary.rpt为准：WNS0.371ns/TNS0、WHS−0.080ns/THS−34.418ns，721个负hold endpoint；内部hold仍+0.019ns。路由中间进度数字不能替代最终报告。
- 修复前771个失败endpoint全部以顶层input为起点；包括三组RDATA各128、control WDATA对应275个endpoint、reset48等。没有内部寄存器起点。默认hold_fix减少了50个失败endpoint，未满足固定min0ns边界合同，不宣称验收通过。
- 工具库存确认BUFGCE_X0Y48位于SLR0/X4Y2。初始auto及本轮诊断缺HD.CLK_SRC，仅作探索；需补齐一致的时钟模型后再比较区域布局。已完成分析闭环2轮。

## 4 第3轮：固定时钟模型与区域对照

- 不改HLS计算和正式IP，沿用b3e4957。新增clock_context.tcl明确BUFGCE_X0Y48为OOC假设（不是后续系统事实），周期、setup uncertainty和输入/输出预算不放宽。
- auto_context和regions从同一个auto/synthesized.dcp开始，均设置HD.CLK_SRC，再执行同一opt/place/phys_opt/route流程。先完成auto_context；regions的位置范围从该次实际primitive位置重新提取，保持VU37P和SLR0，不强迫跨SLR。
- 当前目标：验证时钟模型与内部/接口路径，并取得公平自动/区域对照。若边界min0ns仍不能满足，保留失败门槛，依据明确报告决定后续接口延迟修复或系统边界选择，不用放宽约束冒充通过。
- b3776dc6c43dafd8b54ea4bb800540d6025acff8已推送/服务器ff-only核对后运行auto_context。首次直接PowerShell SSH字符串转义导致test参数错误，未启动Vivado；改为Python传递固定SSH参数后正常运行，同一轮基础设施命令纠正，不重复构建。日志`/tmp/codex_split_d_b3e4957_ooc_auto_context_20261010.log`。
- 新增inspect.tcl用于只读核对内部路径及全部失败endpoint，analyze.py从最终报告取值并归档SHA；本地两份真实auto/auto_hold_retry报告抽取值已与报告核对。读取程序成功不代表物理gate通过；初次Python经Select-Object -First管道输出截断返回非零，改为完整输出/文件，不掩盖实际验证状态。
- 更新PROJECT_CONTEXT当前状态和独立阶段4报告，纠正“未分析并行时标”“优先按HLS FF优化广播”的旧结论。用户AGENTS.md及旧未跟踪文件仍不纳入提交。区域范围待auto_context真实位置，不提交旧auto派生候选。

### 第3轮分析闭环

- b3776dc auto_context在11:52:20—12:05:55执行，exit0；实际HD.CLK_SRC属性写入XDC，Timing38-242消失。最终setup+0.230ns/TNS0、hold−1.582ns/THS−6287.366ns，4560失败endpoint；76206条net全部完成、route error0、12项check_timing均0。DRC141Warning/10Advisory，无Error/Critical Warning。资源LUT49368/FF26415/RAMB36=7/RAMB18=40/DSP40。
- 新模型全部4560负hold仍起于顶层输入，其中reset3771；内部hold+0.010ns，内部setup+0.230ns。最差setup已变为分发循环icmp→V AXI读缓冲BRAM ENBWREN：6.477ns（logic0.540/route5.937，91.7%布线），clock skew−0.215ns。不能继续把旧auto的块内减法称为当前最差路径。
- 63b22e3的只读diagnostic已成功输出内部路径和全部负hold清单；最后查询clock.SETUP_UNCERTAINTY失败（2024.2不存在该属性），整体exit1。改为保存实际XDC及合法时钟属性，新diagnostic_retry目录重试；不覆盖失败目录，不影响已完成auto_context。
- 已依据新primitive位置产生两个hard Pblock（IS_SOFT=false、CONTAIN_ROUTING=false）。worker0 SLICE61..116/84..238、worker1 SLICE94..156/44..233；DSP/RAMB边界也逐类来自位置数据。区域可重叠，不代表互斥物理分区，不强迫跨SLR。新增make_regions.py可从归档位置TSV复现，记录其SHA。
- 已向用户提出边界设计选择：继续严格min0ns物理延迟修复，或内部/通信先验收、边界hold留完整系统。等待选择期间固定合同不变，独立区域对照继续。已闭环3轮；阶段4未验收。

## 5 第4轮：同模型区域约束

- 继承第3轮4560接口hold失败、内部setup/hold通过。主要假设：限制两个query worker在其真实资源范围是否改善控制/数据路由；同一正式IP、综合DCP、BUFGCE_X0Y48、100MHz/2.7ns及min0ns预算，唯一实现变化为两个Pblock。
- 本地生成区域后核对所有位置均SLR0、两处实例各匹配一个；运行regions，不改变HLS或事务。只读诊断属性修复作为第3轮命令重试同批同步，不改变布局算法。用户边界选择未收到前，不放宽hold门槛。
- 3c9a58ced49a3625b1a5e3cecdaa22ce11de624b已推送/服务器ff-only核对，regions在12:12:02启动，当前路由中。只读auto_context diagnostic_retry在12:12:46—12:14:11 exit0，HD.CLK_SRC/100MHz、blackbox0及实际XDC确认，4560负hold全部input、内部+0.010/+0.230ns确认。诊断归档初次tar glob相对于Shell仓库目录而非tar的-C /tmp展开，exit2；改成明确日志名归档成功，不重跑实现。
- 本地analyze.py增加模型/资源/完整gate及关键路径抽取，原始报告SHA与diag子目录均记录。模型gate为true，完整OOC gate为false；没有把内部通过伪装成边界hold通过。
- 新发现物理存储与HLS估算不一致：两个worker各Q4/K8/V8个RAMB18，共40；AXI Q/K/V各2、O1个RAMB36，共7。tile逻辑载荷5120bits。逐primitive在bram_mapping.json，stage3“无tile BRAM”更正为仅HLS估算。RTL为auto RAM、两个写端口，后续须先证明初始化/装载端口活动再评估LUTRAM，当前不盲目改综合输入。

### 第4轮分析闭环

- regions 3c9a58c在12:12:02—12:25:35 exit0，只读da12f37 diagnostic在12:25:59—12:27:35 exit0。最终setup+0.111ns、内部hold+0.010ns；hold−1.565ns/THS−5989.018ns，4553个失败endpoint全部input（reset3777）。资源LUT49228/FF26461/RAMB36=7/RAMB18=40/DSP40；76252条net全路由、error0。最差setup移到Accumulator，7.087ns（logic2.539/route4.548）。比auto_context的setup余量少0.119ns，此候选未显示收益。
- 进一步验收发现DRC有2个HDOOC-4 Critical Warning：placer为两个Accumulator ap_ce各插一BUFGCE，未LOC约束。更重要：effective/diagnostic XDC实际IS_SOFT=TRUE，109条primitive记录在预期范围外；脚本的硬约束目标未落实。归档为失败软区域候选，不能称硬floorplan已验收。
- 部分DSP/RAMB范围被Vivado按tile自动扩展，但不足以解释全部越界。待核实set_property CONTAIN_ROUTING 0对IS_SOFT的联动；禁止只通过删除placement gate规避失败。当前已完成分析闭环4轮，hold gate仍失败。

## 6 第5轮：修正floorplan属性与BUFG约束

- 主要假设：两个Pblock属性存在设置顺序联动；先从routed DCP只在内存中probe，记录每次赋值后的实际属性，再修正生成脚本并在实现前后断言硬约束。工具按tile调整的实际范围也要归档。
- HDOOC-4需要控制已插入BUFG的实际位置，保留其用途/接线，依据实际site固定LOC；不得屏蔽DRC。修正后重做同模型硬区域对照，原失败目录保留。
- hold aggressive选项仅本地准备，尚未执行，不算已验证；边界min0ns仍保持，用户选择待回复。

### 第5轮合同澄清与实际修正

- 用户明确选择“内部与通信先验收，边界hold留完整系统集成”。本次保留input/output min0ns及全部接口失败报告；新增范围内内部验收门槛，完整OOC gate仍不能写成true。撤回仅本地准备、从未执行的aggressive hold选项。
- 748e5cb只读property probe在12:32:30—12:33:43 exit0，证明设置CONTAIN_ROUTING=0会把IS_SOFT从0改回1；改为先CONTAIN_ROUTING后IS_SOFT，并在创建及route后断言0。保存实现前及最终XDC供实际tile范围核对。
- 对placer已经选定的BUFG site在route后显式固定LOC，并检查IS_LOC_FIXED，消除HDOOC-4的真实约束问题。auto_context另从原routed DCP仅更新LOC元数据，前后时序/资源对照，不重布线。硬区域使用新regions_hard目录，失败soft区域证据保留。

- dbaec019d19fe61b48ab03d31b28e4d6439bae63已推送并精确核对，regions_hard于12:39:56启动。auto_context LOC元数据核对12:40:27—12:42:13 exit0；前后setup0.230ns、hold−1.582ns完全相同，层次资源表逐行一致，两BUFG IS_LOC_FIXED=1，无Error/Critical Warning；没有opt/place/route。property probe与LOC前后原报告已一起归档metadata_probe。
- 本地抽取程序保留原full_ooc_gate；新增internal_ooc_gate响应用户范围，实际soft regions仍被属性/越界/DRC三项判false，auto_context内部为true。位置核对使用最终XDC的真实tile对齐范围，仅允许已观测的一格上界扩展，不能任意放宽坐标。

- 静态再核对当前通信协议：每块Q8字、每key tile KV16包、输出32个acc；尾tile/causal保持消费计数。输出发生于全部key输入以后，固定汇聚不会反向阻断仍需输入的worker。CoSim数学/严格24/24、完整位模式及官方QK177次重叠再次从归档核对；src/include/tests从130c778到b3e4957无差异。
- 新抽取程序要求全部可路由net完成、全部负hold分类数与最终endpoint数一致，并保留全部12项check_timing。真实auto内部通过、真实soft区域失败的回归已通过；Python语法检查及git diff --check通过。未因为Tcl/报告改动重跑相同C++测试。

### 第5轮最终结果与分析闭环

- dbaec01 regions_hard于12:39:56—12:56:54 exit0；只读diagnostic于12:56:18—12:57:53 exit0。最终WNS−0.081ns/TNS−0.273ns，4个内部setup endpoint失败；内部hold+0.010ns，4398负hold全部input（reset3668），WHS−1.570ns/THS−5982.450ns。route中间+0.037ns不能替代最终报告。
- 实际两个IS_SOFT=FALSE；worker0/1检查38456/38623条SLICE/DSP/RAMB primitive记录，越界0，全部SLR0。两处资源上界按tile各扩一格，与原seed同时保留。route76265/76265、error0，12项check_timing均0；DRC145Warning/10Advisory，无Error/Critical。LUT49220/FF26461/RAMB36=7/RAMB18=40/DSP40。硬区域原始压缩报告2024291bytes。
- 最差setup在分发ap_enable→K AXI读缓冲BRAM ENBWREN，7.049ns（logic0.777/route6.272）。内部setup比auto_context少0.311ns，候选不采用；不是靠放宽时钟或删gate获得通过。实际hard未插入BUFG，因此不能将其HDOOC-4消失单独归功于LOC规则。
- 为直接证明修复，旧soft仅对已有两个BUFG固定LOC，13:04:52—13:06:55 exit0：HDOOC-4从2降0，setup0.111/hold−1.565及全部层次资源不变，无opt/place/route。旧soft属性和越界仍失败，原始routed DCP/报告不覆盖。
- 最终选择auto_context/locked_buffers/routed.dcp；13:02:40—13:04:19只读核对内部setup+0.230/hold+0.010ns、HD.CLK_SRC、blackbox0，全部4560负hold均起于input。与原自动布局全局时序/1579行资源表一致，两个BUF LOC固定。新诊断及LOC单因素证据合并归档metadata_probe（136693bytes）。
- 已解决：正式HLS/IP及数值保持、真实QK重叠/通信等待证据、OOC时钟模型、硬属性顺序/实际位置、BUFG LOC缺失及验收口径。
- 仍存在：边界input min0ns hold失败；硬区域setup失败且弃用；未新增距离感知流水、未完成四组完整研究对照、任意AXI背压证明、完整系统及16×16。无新增硬件改动来掩盖这些结果。
- 验收标准状态：用户明确的内部与通信范围由所选auto通过；同条件硬区域对照已完成，失败候选保留。完整OOC gate仍false。原方案允许4×4物理感知收益不明显，不要求采纳失败floorplan，因此保留自动布局，不另设轮数上限，也不为制造收益盲目插级。
- 下一轮修改：本次OOC工作包无需新轮。后续完整系统需另明确范围，先建立真实AXI发起端延迟、时钟位置/布线及同步reset释放合同，再闭环边界hold和背压。TAPA后续物理划分还须纳入分发器/AXI缓冲控制，不能只限制worker。阶段5继续暂缓。
- 本轮闭环状态：已完成。

## 7 调用结束总结

- 状态：本次OOC/内部/通信范围收敛，保留自动布局；完整边界时序及整个研究计划不宣称完成。已完成分析闭环5轮，用户未设置上限，无未完成轮和运行中的远端任务。
- 正式HLS/IP被测b3e4957e7454989198f58345e9ebcff1dac536d3，计算130c778；最终物理脚本被测dbaec019d19fe61b48ab03d31b28e4d6439bae63。后续证据抽取/报告提交未重新运行HLS或OOC。
- 数学/严格24/24、CoSim26事务80242cycles、HLS7.300ns/DSP40/II5/5/1/1、正式IP导出及SHA、两个QK循环177次窗口完全重叠。所选auto内部setup/hold+0.230/+0.010ns；区域候选最终setup−0.081ns弃用；min0ns边界失败保留给系统集成。
- 独立报告：docs/综合报告/Split-D阶段4物理对照_20261010.md；对照验收JSON：docs/evidence/split_d_b3e4957_4x4_stage4/ooc/comparison.json。根PROJECT_CONTEXT与执行方案当前状态已同步。
- 最终记录随本节所在Git提交发布，精确hash由Git记录；用户AGENTS.md、.tmprun和旧未跟踪stub日志仍保留且不提交。

- 结束前复核：全部原始压缩报告SHA与comparison.json一致，DCP未提交；IP component.xml确认四路AXI RDATA/WDATA均64-bit。所有远端任务已退出，最终发布仅涉及工具/证据/文档，无src/include/tests变化。证据分析结束时间：2026-10-10T13:10:22+08:00。
