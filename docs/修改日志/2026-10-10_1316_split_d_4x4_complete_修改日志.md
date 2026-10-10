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
