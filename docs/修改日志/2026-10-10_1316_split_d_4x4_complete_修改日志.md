# Split-D 4×4存储优化与完整集成修改日志

## 1 本次调用信息

- 开始时间：2026-10-10 13:16+08:00。
- 当前状态：进行中，第3轮。
- 本地仓库：`C:/Users/30130/Desktop/workstation/FlashAttention/FSA_HLS`。
- 远端：`FSA-FPGA-NM37-tailBox:/home/zhangchenxuan/FSA_HLS`。
- 分支：`fsa_split_D`；起始commit：`de03998070c004388bd0ba34fa9d7759ce7700c5`。
- 工具：远端Vitis HLS/Vivado2024.2，每条SSH显式执行交互Bash并加载`~/.bashrc`。
- 最大轮数：未设置。
- 授权：用户“完成1/2/3，在4x4上做完所有实现和验收工作”，包含tile存储审计与优化、正式全流程/IP/OOC回归、真实AXI完整系统集成；Git/SSH按既有授权继续。16×16继续暂缓。
- 完整系统目标正在核查：已有NM37 HBM工程，U280工程器件不同；询问真实HBM＋板内AXI-Lite控制器或PCIe链路的选择。板卡烧写不由软件仿真替代。

## 2 可验证标准

- [ ] tile每元素写入覆盖与使用顺序有源码/RTL证据；物理存储成本按实际primitive核对。
- [ ] 正式CSim数学24/24、固定位模式24/24；CoSim26/26及完整C/RTL输出一致。
- [ ] 总16PE/4Accumulator、DSP40、QK/ROW_SUM/PWL/PV II5/5/1/1、HLS估算不超过7.300ns，接口位宽保持。
- [ ] 新正式IP和100MHz、2.7ns uncertainty的OOC内部setup/hold通过；记录BRAM/LUT/FF与吞吐变化。
- [ ] 真实NM37 AXI存储及控制集成，真实时钟/复位/接口合同下setup/hold、DRC、背压和数值验收。不能用小AXI RAM自检包装冒充HBM系统。

## 3 初始状态

- 本地与远端HEAD均为de03998，远端tracked工作树干净，704GB可用、无既有Vivado/HLS运行任务。
- 本地既有`AGENTS.md`用户修改、`.tmprun/`和旧`local_math_stubs_check.log`不进入本次提交。
- 计算基线130c778、正式HLS/IP b3e4957；OOC所选auto_context内部setup+0.230ns/hold+0.010ns。原顶层input hold未闭合，留完整系统处理。
- 物理tile为40RAMB18（每worker Q4/K8/V8），逻辑载荷5120bits；AXI缓冲7RAMB36。
- 预检首条复杂SSH命令被PowerShell引号解析截断，未发起远端任务；改用Python argv＋shlex.quote后预检成功。远端发现`vivado/HBM_test`及`vivado/hbm_example_ip_ex`，没有XDMA设备节点。

## 4 迭代总览

| 轮次 | 被测commit | 修改 | 本地 | 远端 | 状态 |
|---:|---|---|---|---|---|
| 1 | 4934747 | 删除Q/K/V重复初始化 | 数学24/24，严格19/24 | HLS/IP通过；OOC内部通过 | 已分析，存储问题仍在 |
| 2 | bf41d16 | 四lane feature分bank＋真实HBM集成包 | 数学24/24、严格19/24 | HLS/IP及内部OOC通过；unit通过，BD配置失败 | 已分析，保留存储方案 |
| 3 | 待提交 | 修复系统工程配置并真实HBM验收 | 待执行 | 待执行 | 进行中 |

## 5 逐轮记录

### 第1轮

#### 修改前判断与计划

Q完整覆盖`QUERY_BLOCK_COLS×QKV_WORDS_PER_TOKEN×DMA_ELEMS_PER_WORD`，K/V完整覆盖`PE_DIM×QKV_WORDS_PER_TOKEN×DMA_ELEMS_PER_WORD`；配置静态约束保证HEAD_DIM可整除每字元素数。装载循环无按有效长度跳写，distributor对尾部发零；首次QK/PV使用位于装载之后。因此这三数组的零初始化在C语义上冗余。先只删除这三处初始化，验证是否消除RTL第二写端口或BRAM；不提前断言物理收益，不改变其他状态初始化。

#### 实际修改与验证

- `split_d_controller.cpp`仅删除Q/K/V三数组`{}`，补充全覆盖注释；其他状态、FIFO、接口及测试未改。
- 本地`cmd /c tools\local_split_d_check.cmd 4 16`：编译成功，数学24/24、严格19/24、exit1；与基线相同5项宿主数学stub位差，不作为正式通过。证据`build/local_split_d_check/complete_round1.txt`。
- 正式远端标准入口即将运行，默认4×4/head16、完整CSim/CSynth/CoSim/IP；不更改正式流程。


- 被测commit：4934747d8a453a76f79a007cf28de23a22852ebe；push及远端ff-only pull成功，tracked干净且HEAD精确匹配。正式入口于13:20:46+08开始，日志`/tmp/codex_split_d_4934747_complete_hls_20261010.log`。13:24读取CSim数学/严格均24/24，CSynth仍进行，CoSim/IP未结束。

- 正式流程13:20:46—13:34:22+08，exit0；CSim与CoSim前/后数学和严格均24/24，CoSim26/26 PASS，IP导出成功。新CSynth：7.300ns、DSP40、FF45051、LUT127976、BRAM18K8，较b3e4957 FF−40/LUT−1616。Q RAM仍有两写端口，不能把初始化认作唯一原因。
- 读取进行中构建时第一次误读已归档旧`*_build`（报告时间10:48），立即更正到当前`build/solution1`（13:24）；旧报告不纳入本轮结论。官方入口结束才将build改名。
- OOC在13:37:35+08启动，同器件/10ns/2.7ns/HD.CLK_SRC48，输出`build/split_d_ooc_4934747/auto`；尚未出实现结果。证据归档正在传输。
- 独立准备NM37真实HBM集成：由服务器现有HBM_test.bd核实双stack/8GB、SAXI_00、225MHz AXI、100MHz APB/reference；SmartConnect连接四路原FSA master与自检master，100/225MHz转换。程序6745指令、8192×128bit ROM，26事务寄存器配置及全部非法O canary计数已静态核对。该新增系统包尚未提交/远端验证，不能声称集成通过。
- 后续存储方向依据AMD UG1399的cyclic分区/存储绑定文档：按4个DMA lane分feature bank，需实际检查single-write映射及LUT/BRAM收益，保留全覆盖装载与真实计算反馈。来源：https://docs.amd.com/r/2024.1-English/ug1399-vitis-hls/Array-Partitioning 。

- HLS证据已归档`docs/evidence/split_d_4934747_4x4_complete/`：24有效输入/O全部与25d6dda原始word一致，完整C/RTL O SHA保持73dd80b…bd0；2非法完整canary通过；16PE/4Acc/两块阵列，两块II均5/5/1/1、禁止warning均0。总72507cycles，较80242减少7735（9.64%），仍待OOC结果闭环。正式IP SHA56999c10…defde。

- 13:51用户明确选择“采用真实HBM与板内自检控制器”；系统目标已确定，不加入PCIe主机链路。参考clock按已实现NM37 example使用IBUFDS原始输出，避免驱动HBM内部reference BUFG时级联全局buffer。

#### 第1轮分析闭环（14:03+08）

- 已解决：冗余清零消除，24有效原始位模式/26事务保持、总周期−9.64%，结构/II/HLS预算不变。OOC13:37:35—13:56:51 exit0，内部setup+0.267ns/hold+0.010ns，DSP40、LUT49091、FF26441、RAMB36=7/RAMB18=40；75848/75848 nets routed、DRC无Error/Critical。
- 剩余：40个tile RAMB18仍在，Q两端口同拍分别写DMA lane0/2与lane1/3；初始化不是唯一原因。外部min0ns hold4985个（reset3922），WHS−2.328ns/THS−8855.124ns；内部全部通过但full_ooc_gate=false，必须由已授权真实系统关闭。
- 判据：本轮HLS/IP、内部OOC合格；存储优化及完整集成尚未合格。报告见新evidence/ooc/auto。
- 下一修改：仅改变Q/K/V feature banking为cyclic factor4（每DMA lane独立），不同时绑定存储类型；实际端口与primitive收益由新构建判断。

### 第2轮

#### 修改前判断与计划

继承问题：tile两写端口、小容量却40RAMB18，以及真实边界/系统验收未完成。依据装载四lane同拍写入，添加Q/K/V dim2 cyclic factor=DMA_ELEMS_PER_WORD，保留dim1空间partition、全部计算路径和时钟合同。独立集成包固定真实HBM＋板内自检；controller先跑错开AW/W、背压、错误字、超时保持/排空、忙中复位单元检查，再执行真实HBM BD仿真/实现。单元小memory不得当成系统验收。

新增包静态审查修复：超时不取消VALID，当前事务排空后停止repeat/poll，禁止发送下一beat；unit fixture加入排空验证。core_clock在impl重新获取，避免引用已关闭synth设计对象。HBM reference按既有真实example使用raw IBUFDS输出。

- 第2轮本地正式同入口检查：编译成功，数学24/24、严格19/24（同基线5项宿主stub位差），exit1；完整日志`build/local_split_d_check/complete_round2.txt`。新增被动core FSM起始→done周期观测，与包含预装载/检查的last_case_cycles区分。

- bf41d1602d5c59d35033b74c2ab82fddc232a528标准HLS14:17:29—14:30:55 exit0，数学/严格24/24（CSim、CoSim前/后共3处）、26CoSim、24有效O/输入及2非法canary保持，完整O SHA不变。16PE/4Acc/两阵列，II5/5/1/1、7.300ns、DSP40/FF45889/LUT129412/BRAM8；总72696cycles，较493多189（0.261%），较b3少9.404%。70源文件Git blob与服务器一致。OOC14:36:06—14:53:20完成，物理报告正在归档，尚未关闭本轮分析。
- 控制器unit于14:20:47—14:21:34：正常26事务、注入错误字检测02、超时检测ff并保持/排空VALID、忙中复位后26事务，4项全部PASS。unit小RAM不算HBM系统测试。
- NM37 BD生成（包bf41d16、IP493快照）14:21:30—14:22:16失败：clk_out2未生成；复位C_EXT_RESET_HIGH只读，不能直接配置。实际安装版bd.tcl确认由ext_reset_in的POLARITY推导。已修正CLKOUT2_USED=true、删除直接设置并加validate后低极性断言；该修正仍属第2轮集成调试，不额外计一轮。
- 两次只读SSH自动审批超时未启动；用户再次明确“允许继续重试并推进”，继续站立授权。OOC和HLS已运行的任务未重复启动。
- 只读JTAG发现两个target：210017937722A为唯一xcvu37p_CIV，210017401888A为xczu5+arm_dap。没有program/reset动作，真实HBM板测尚未执行。

#### 第2轮分析闭环（15:12+08）

- 已解决：按4 DMA lane cyclic feature分bank，将tile RAMB18从40降为0，AXI缓冲仍7RAMB36。实际LUTRAM320→960，LUT49091→50432（+1341）、FF26441→28058（+1617）；DSP40不变。内部setup+0.330ns、hold+0.010ns，77088 nets全部route，DRC无Error/Critical，黑盒0、所有check_timing计数0。
- 已保持：位模式/实例/II/HLS时钟合格，周期仅比493增加0.261%，因此接受存储分bank。没有另加BIND_STORAGE或改变QK/PV数学链。
- 剩余：min0ns边界hold4575（reset3752）、WHS−1.650ns/THS−6684.096，full_ooc_gate仍false；必须在完整系统关闭。unit4项通过，真实HBM BD首次配置失败，尚无系统仿真/实现/板测。
- 判据：工作包1存储与工作包2正式全流程/内部OOC已合格；工作包3未完成，整体任务未验收。
- 下一修改：发布已准备的clock/reset配置修正，并完善自包含交付；使用bf41d16正式IP继续真实HBM集成，计算源码保持已验收。

### 第3轮

#### 修改前判断与计划

继承只剩完整集成：ClockWizard必须显式CLKOUT2_USED，Processor System Reset按连接的ACTIVE_LOW推导C_EXT_RESET_HIGH并断言；不放宽时钟或误排除内部路径。准备自包含复制正式IP的交付生成器和相对路径config，随后真实HBM BD生成、52笔系统仿真、综合/route/setup/hold/CDC/DRC、bitstream及唯一VU37P板内自检。计算IP被测版本固定bf41d16，系统源码版本独立记录；脚本修正无需冒充新的HLS数值验收。

- 第3轮本地自包含交付验证：首次复制ip.tmp中间目录触及Windows长路径，部分交付保留在ignored build供诊断；正式component不引用该临时目录，生成器排除ip.tmp且逐项校验component引用。新`build/nm37_delivery`成功，正式component SHA保持a64c7be…a26dc。交付复制不构成Vivado验证。

- 35afc03工程生成15:44:19—15:45:25失败：clock第二输出连接已通过，进入module reference；Vivado明确拒绝SystemVerilog作为BD reference顶层。controller本身为Verilog-2001语法，修正该文件FILE_TYPE=Verilog，不改控制器语义；继续同一第3轮。原失败工程保留，新建目录重试。

- 00158f1工程生成16:04:02—16:05:10：Verilog module reference及5路AXI/HBM地址自动分配已执行；输出导出失败，make_bd_pins_external没有返回对象，旧脚本把空返回给set_property。改为按已核对宽度显式create_bd_port/connect_bd_net，同时加入系统逐事务progress打印；保持同一第3轮，尚未产生系统仿真。

- 446bdab 16:15:06—16:16:50：BD时钟/低复位断言及HDL/IP生成已执行，HBM_MEM00在全部5个master明确映射0/256MB；仍不能验收：AXI-Lite Reg未分配（critical），最终地址报告调用redirect不属于Vivado Tcl而exit1。正式component实际声明Reg窗口64KB、7位local address；加入独立1×1 Control SmartConnect供地址窗口解码/适配，显式映射0/64KB并断言，保留原FSA接口；地址报告改report_property -file并导出CSV，依据AMD UG835 assign_bd_address/report_property。继续同一第3轮。

- a37d2e0完整工程16:25:54—16:27:39成功创建，control映射/ACTIVE_LOW断言、全部BD/IP生成、地址CSV输出完成，日志无Critical Warning/Error。尚未系统仿真。系统TB补整体clock/reset/AXI/HBM忙中复位：首写已进入等待B后断言整体reset，重新等待clock_locked与init_done，再普通/stress各26完整事务；这与unit局部reset区分。

- a0c4329源码同步成功、bf41 component SHA复核；系统仿真16:38:54启动，真实HBM BD＋整体忙中复位＋52事务，日志`/tmp/codex_nm37_a0c4329_system_sim_20261010.log`。独立复制project到`build/split_d_nm37_a37d2e0_ipbf_impl/project`，16:39:42启动系统综合/实现，日志`/tmp/codex_nm37_a0c4329_system_impl_20261010.log`；两流程不同project，尚未结束，不声称通过。

- a0c4329系统仿真16:38:54—16:52:57结束，外层严格要求两处PASS，实际exit98。Fatal为SYSTEM TIMEOUT，busy/done/state均X；进一步查原始日志明确Time=0ps/Iteration1，不是HBM运行3s后死锁。原因是TB的无尺寸3000000000延迟超出32-bit signed，XSIM把它落在0时刻。修正为显式64位延迟，保持原本3s超时合同，加入10us clock诊断。计算核与硬件不改；保留失败，不用Vivado进程exit0当作仿真PASS。
- 系统综合已成功产出顶层DCP（16:47:24），控制器ROM实际1个/30RAMB36；独立实现继续routing，尚无完整timing/CDC结果。

- 系统实现16:39:42—17:08:46 exit0，98003/98003 nets routed，setup+0.226ns/hold+0.010ns，DRC无Error/Critical；物理门槛仍未通过：methodology有TIMING-4×2/TIMING-27×2，HBM内置hbm_ip.xdc在两个HBM_REF_CLK内部pin重复定义100MHz主时钟，覆盖board_clk_100传播。CDC为13项CDC-3/2项CDC-9 Info、694项CDC-15 Warning（SmartConnect双域Safely Timed/max_delay_datapath_only），需结合所有者/原始约束审查，不以正slack直接上板。768c12e修复TB后17:10:20重试仿真，仍模型编译中；本轮分析尚未关闭。下一步只读原routed DCP验证参考时钟生成关系，原证据不覆盖。

- 17:18:52—17:20:42只读routed DCP内存副本诊断：把两HBM reference改为同名divide_by1/combinational generated clock，MASTER_CLOCK=board_clk_100、PERIOD10ns，TIMING-4/27 Critical全部归零，WNS/WHS仍+0.226/+0.010ns，2.7ns uncertainty不变；有预期XDCC-1/7各2项覆盖告警。修正写入LATE core_budget.xdc并逐stack断言；不抑制告警、不更改HBM厂商源文件。正式实现另起新工程，加入bus-skew/clock报告及setup/hold/DRC/methodology失败返回非零门槛。首轮系统原证据归档docs/evidence/split_d_nm37_a0c4329_ipbf41d16/；尚未作为通过使用。

- 7892c75于17:33:12开始新工程/实现；18:03读到Designutils20-1307 Critical：XDC不支持foreach/if。此前core_budget中的if断言也不应放XDC，不能用普通Tcl内存诊断成功代替XDC读取器验收。修正为显式两条generated_clock＋uncertainty纯约束，断言移scripts/check_clocks.tcl在synth/route/bitstream打开设计后执行。计算硬件/IP不变，保留789失败。系统仿真仍未取得事务：离线WDB证明17:37时164395ns、17:57时380805ns，持续推进约11us/min；MEM文件与IP SHA一致，XSIM CPU活跃，不能归因缺MEM或零时刻死循环。

- 7892c75实现17:34:57—18:04:17最终exit1，timing仍+0.226/+0.010，但methodology4条Critical仍在，失败门槛正确拦截；XDC foreach未生效得到直接证据。纯约束修复将起新正式工程；新增TB每50us打印两stack apb_complete/控制器reset，增加观察能力且不改综合硬件。当前旧仿真不重启。

- a394c8d89cc8327c19f6f0ac0bb040fa509a78b3本地审查diff --check通过并推送；服务器新正式工程build/split_d_nm37_a394c8d_ipbf，计算IP仍bf41，执行create_project→build_and_report，Git/root/origin/branch/clean/ffpull/component SHA预检齐全。另18:09:53在build/nm37_init_diagnostic_xsim_1810复制已编译768快照，xsim只跑20us并get_value两stack/复位，作为诊断而非功能PASS；旧完整仿真继续运行。

- 18:13短诊断完成：20us时clock_locked=1/reset_n=1，但两APB/AXI00/自检reset均0。实际生成的rst_100/rst_225 XCI均C_EXT_RESET_HIGH=0、C_AUX_RESET_HIGH=0；unused aux_reset_in却接常量0，形成永久有效辅助复位。此为真实配置错误，旧仿真没有开始事务，不能把11us/min推进率当有效HBM吞吐。修正create_project显式C_AUX_RESET_HIGH=1（unused低电平无效），validate后双域断言；TB在10us检查时钟/全部关键复位已释放。独立旧快照强制aux=1只作根因诊断，不作为功能验收。已精确核对PID1380185的命令/工作目录后SIGTERM停止无效旧仿真，保留日志/波形。a394物理运行继续供纯XDC验证，因旧复位错误不能作为最终系统验收。

- a0e137ac7831e1efc424a91010289627c5dd98da显式辅助复位极性与10us释放检查已审查、提交/推送，18:21:26在新目录build/split_d_nm37_a0e137a_ipbf启动工程生成→正式52事务仿真，标准精确提交/component预检完成。独立force诊断失败：XSIM不能force该跨HDL实体输入端口（Simtcl6-179）；无功能结果，不据此增加强制信号到正式TB。已归档789失败、20us原始复位诊断与XCI。789旧bus-skew报告18项，最小正slack3.718ns，但旧方法论仍失败，不当最终物理通过。

- 18:23:04 a0e137a工程生成成功，原始XCI双域C_AUX_RESET_HIGH=1/C_EXT_RESET_HIGH=0已递归核对；18:24:41在独立副本启动完整系统实现，正式RTL仿真同时编译中。自包含build/nm37_delivery_a0e137a静态验证287文件SHA/正式component/ROM/相对入口通过，尚不代表工具验收。
- 官方UG900和实际Vivado只读属性查询确认HBM/两SmartConnect均支持tlm/rtl，当前均rtl。增加可选hbm_tlm功能对照，仅HBM使用官方TLM，FSA/controller/SmartConnect保持RTL，不改变硬件配置、52事务/32768canary/金标准/复位覆盖；默认RTL流程继续。TLM不可用来声称实际HBM吞吐，实际吞吐须板测。TB释放检查改!==1确保X也失败。这是同一第3轮诊断补强，非新的HLS迭代。

- 3ac1786 TLM已通过10us关键复位释放检查，但1.6ms后APB完成仍0；只读官方生成hbm_sc.h确认APB/APB-complete为xsc_stub_port，TLM不建模校准，不能等待这些stub。精确停止该无效等待，保留日志。增加仅仿真宏NM37_HBM_TLM_FUNCTIONAL的readiness适配（BD内部init_done=clock_locked），明确FUNCTIONAL ONLY，不计HBM初始化/校准/复位恢复通过；默认RTL不启用宏，仍保留真实初始化和所有52事务门槛。硬件/计算IP不改。

- 用户新增联网板测调研和NM37原理图核查，继续同一第3轮。23页提取文本并放大相关时钟/复位/JTAG/HBM电路；BH42/BJ42/BF2及Bank电压符合现有映射，SW2复位同时连接ZU5。SiTime精确型号确认U7为100MHz LVDS；AC电容后的FPGA接收侧无图示偏置，当前XDC只有DIFF_SSTL12、默认ODT RTT_NONE，终端遗漏成立。依据AMD UG571表1-44/AC耦合章节，最小修正为split ODT RTT_48，保留1.2V标准/100MHz/2.7ns预算，加入普通Tcl断言。18:54内存DCP诊断接受双端口RTT_48，DRC无Error/Critical，原DCP未修改；需在新精确提交重做实现，DRC不等于模拟电气实测。官方来源及后续HBM/JTAG验收步骤见新增板级核查报告。
- a394纯XDC旧复位对照18:40:06实现exit0，setup/hold+0.226/+0.010ns，只证明约束修正。a0e137a修正复位版18:54:06实现exit0，setup/hold+0.200/+0.010ns，98003 nets全route、LUT60777/FF43700/R36=45/R18=0/DSP40；DRC/methodology无Error/Critical。已归档a0物理及I/O/ODT诊断到docs/evidence/split_d_nm37_a0e137a_board_review/，其最终board gate=false（还缺ODT新实现、系统功能与真实板测）。
- f2 TLM首笔尚未结束，独立100us快照确认ap_start=1、ap_idle=0、ap_done=0、length=1，control轮询读到0x1；继续查内部数据流。首次独立快照因未继承厂商simulate.sh的LD_LIBRARY_PATH缺libxtlm.so，补实际厂商路径后成功读取；正式Vivado仿真环境无此错误。默认RTL已推进50us且关键复位释放，APB校准仍未完成，不声称功能PASS。

- 后续官方PG276/PG247核查确认新的硬件错误：HBM原生接口仅支持AxSIZE=5；SmartConnect对单拍小访问保留原SIZE，不会自动把64位LEN0/SIZE3改成合法HBM访问。100us实际BD网线读到AWADDR=0x300040、AWLEN=0、AWSIZE=3、WSTRB=0xff，证明自检预装载/检查接口不合格。默认RTL已完成两次初始化与忙中整体复位恢复，随后首笔输出检查失败（code02、pc26、actual全X），无功能PASS；TLM仍卡首笔K/V装载。不能把上述现象单独归因于SIZE而未复测，也不能忽略协议错误上板。
- 同一第3轮下一修改：仅板内自检master改为256位原生HBM拍、32字节对齐、AxSIZE5；ROM仍64位word与8字节逻辑步长，WSTRB按lane选择8字节、读回选择对应64位lane。正式FSA四路64位接口/IP/26笔金标准不变。单元模型检查各lane、相邻canary、独立AW/W、背压与超时；真实系统增加HBM端被动SIZE/地址检查以核实HLS访问。停止已确认不合格的旧TLM/旧物理任务前核对PID/命令/cwd，保留全部日志。d540时钟ODT实现19:03:57开始新工程、19:05:37生成完成，尚无最终报告；因仍含旧自检接口不作为最终候选。
- 已实现上述自检原生拍适配及unit各lane/邻居检查，ROM SHA仍6e087144e121bce523d6843e18f2dc23c21a39c693e2450e46848f7c1114f1ca，unit独立首字期望0x0123456789abcdef与原fixture一致，git diff --check通过。本地无Vivado，不称编译/RTL通过。TLM仅功能对照明确排除其APB stub无法表达的流量后复位，保留全部52数据事务；默认RTL初始化及忙中整体复位门槛不变。已核对PID/cwd后SIGTERM旧TLM1613615、旧实现1716122/1710535；未删除日志、工程或波形。
- 473a4f910728d28d9375d91afd7e99d9ad9450b1已提交/推送/远端ffpull并精确核对component。19:28:03—19:28:52新256位自检4项unit全部通过（正常、注错、超时保持/排空、忙中复位）；lane与邻居canary检查通过。官方TLM工程19:30:24生成完成并启动52数据事务；默认RTL新工程19:30:24—19:32:00生成并启动真实初始化/忙中复位/52事务。旧失败与SIZE证据归档docs/evidence/split_d_nm37_native_hbm_diagnosis/，没有功能PASS。
- 新TLM 100us独立快照HBM AWSIZE已5，但首笔K/V装载仍等待、无核完成；因此不能把修SIZE写成全部故障解决。正在用官方VCD记录并统计各AXI边界与内部pipeline等待，定位第二个问题。真实默认RTL尚未完成；未启动最终物理实现或上板。
- 独立100us VCD（15723182bytes）统计：Q/K/V各接受1个LEN3/SIZE3源请求，经SmartConnect变为HBM三个LEN0/SIZE5请求；HBM仅返回Q/K两个完整256位响应，源Q/K各收到4拍，V无返回，内部pipeline因V读等待。该证据定位到TLM HBM返回边界，不足以断言RTL/真实硬件同样丢响应；默认RTL对照继续。另实际xvlog prj/elaborate.log均无axi_watch/core_latency_watch/bind_axi_watch，bind文件被Vivado编译排序排除；之前不能据此声称通信统计或纯核计时有效。下一补强将被动监视器改TB显式实例化，并核对实际编译/运行。
- 4a5c8032ccc372438cf0ddbcc4fcaf22a6918796显式监视器诊断19:48:24—19:50:30完成：实际编译/运行Q/K/V AR=1/1/1、R=4/4/0，与原始VCD相符，无协议稳定性Fatal；这仅是100us观察，不是52笔PASS。core计时active=0暴露另一处观察错误：正式顶层ap_idle在ap_start有效时为0，start&&ap_idle不能检测启动。已读bf41正式RTL，实际launch谓词为ap_start&&ap_CS_fsm_state1；下一最小修正仅改仿真观察连接，重新确认active=1，不改计算IP或系统硬件。自包含4a5c包287文件/component/ROM静态SHA通过。4a5c物理任务将在该诊断后独立新工程执行；473默认RTL完整仿真仍运行，不重复。
- 用户要求“等正在跑的测试停止后就暂停，然后返回当前的进度”。20:02只读复核：4a5c物理工程19:52:03生成完成、20:01:13进入impl_1；473默认RTL最后50us心跳，HBM初始化尚未完成、cases=0。仅等待上述已启动任务结束并记录结果，不启动任何新测试/实现/bitstream/板测；本地core计时谓词修正保留为未提交、未验证。旧20us辅助复位诊断进程1495091/1495158仍驻留于build/nm37_init_diagnostic_xsim_1810，脚本含quit，不算新一轮验收结果。


### 第3轮分析结束：用户要求暂停，整体未验收

- 473a4f9默认RTL19:32:00—20:17:46自然结束：真实初始化及忙中整体复位恢复通过；109335ns Fatal，cases0/code02/pc26/actual全X，外层验收exit1。核对generate_program.py及ROM后，pc26为O区前置canary READ，地址0x002ffff8、期望0x0000000012345678；不是O数值比较。此前笼统的“首笔输出检查”在此纠正为前置保护字，不能把错误直接归因attention算术。原生SIZE5修正尚未解决全部系统功能问题。
- 4a5c803完整实现19:52:04—20:22:18自然结束、exit0：setup+0.261/hold+0.010ns、TNS/THS0、99857 nets全route；LUT61894/FF45210/R36=45/R18=0/DSP40，时钟RTT_48/100MHz/2.7ns断言通过，DRC/methodology无Error/Critical。CDC13/2 Info＋696 CDC15 Warning，详细复核未完成；18组bus-skew最小slack+3.739ns。
- 新完整报告/日志19文件归档docs/evidence/split_d_nm37_4a5c803_pause/，原始字节SHA留manifest；DCP/WDB留服务器。20:25核对两项主任务已退出，旧有限20us诊断残留PID1495091/1495158经准确cwd/script核对后SIGTERM关闭，无剩余本任务Vivado/XSIM进程，没有删除文件。
- 已解决：自检单元lane/邻居canary通过、时钟ODT遗漏、完整实现自动门槛、显式AXI监视器实际编译/运行。未解决：首个canary全X、TLM V无返回、core计时谓词补丁未验证、CDC详细复核、完整52笔系统功能和板上验收。bitstream/JTAG/板测均未执行。
- 当前第3轮远端数据已读并分析，部分验收失败；按用户要求暂停，不启动第4轮或任何补跑。本地core监视器补丁未提交，保留待恢复；用户AGENTS.md及无关未跟踪文件不动。工作包1/2仍通过，工作包3及整体任务未完成。
