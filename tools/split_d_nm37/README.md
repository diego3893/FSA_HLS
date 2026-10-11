# Split-D NM37真实HBM集成

## 1 范围与状态

这是4×4/head16 Split-D的完整存储与控制集成工程。计算IP仍由仓库标准入口`./run_hls.sh fsa_stream_split_d`生成；本目录的Vivado流程不替代、不缩减HLS测试。

当前状态（2026-10-11）：正式计算IP`bf41d16`全流程已通过。原生256位/SIZE5自检控制器unit通过；`4a5c803`完整实现setup/hold为+0.261/+0.010ns，默认RTL在首事务前置canary读回全X。已确认该工程开启了XSIM模型不支持的DBI；`dfbf82e`关闭16个MC读/写DBI，保持仿真与硬件一致，正在新工程执行完整RTL及实现。尚不能断言DBI是失败根因。bitstream和板测未执行。最新结果以本次修改日志与正式验收报告为准。

## 2 系统合同

- NM37：`xcvu37p_CIV-fsvh2892-2-e`，差分时钟BH42/BJ42、低有效复位BF2。
- 计算核/自检/AXI-Lite100MHz，计算时钟setup uncertainty2.7ns；HBM AXI225MHz、APB/reference100MHz。
- Clocking Wizard和每个时钟域的Processor System Reset生成真实时钟与复位。外部按钮仅作为异步复位输入排除；内部复位、AXI与数据路径保持时序分析。
- 既有NM37 HBM工程的双stack、总8GB配置，启用一个SAXI_00端口。四个64-bit计算bundle保留；SmartConnect汇聚四个计算master与一个自检master，并转换到真实256-bit HBM AXI端口。这是单HBM端口功能集成，不代表32端口峰值带宽。
- 16个MC的READ/WRITE_DBI_EN均为false。实际HBM XSIM模型不支持DBI，配置须与下载工程一致；参考[AWS官方修正脚本](https://github.com/aws/aws-fpga/blob/f2/hdk/common/verif/scripts/xsim_patch.tcl)。
- Q/K/V/O地址分别为0、1MB、2MB、3MB；都落在首256MB映射内，不重叠。AXI-Lite寄存器地址由正式导出RTL核对。

## 3 自检与诊断

`generate_program.py`把25d6dda原始TV固化为8192×128bit ROM（6745条有效指令）。硬件先写入真实HBM，再设置四个64-bit地址、length、causal和ap_start，轮询完成后按原始64-bit字比较。

- 24个有效事务：全部1576个O字、两端canary、输入Q/K/V保持检查。
- L0和4097：全部32768个O字与两个guard保持canary，status必须为1。
- AXI AW/W分别握手，阻塞时保持VALID和数据；B/R的RESP及单拍RLAST均检查。
- 自检使用32字节对齐、AxSIZE5的256位HBM访问，ROM仍按64位word和8字节步长执行。写WSTRB只使能目标8字节，读回选择该lane，保留其余word和canary；四路HLS接口仍为64位。HBM端被动检查SIZE5及地址对齐，不能假定SmartConnect对单拍窄访问自动放大SIZE。
- stress模式每四拍接受一次自检响应；它测试自检master的背压，不应单独宣称计算master的任意停顿已验证。HBM/SmartConnect实际给计算核施加的停顿另由系统波形核对。
- 超时报fail_code=ff，保留尚未被接受的AXI VALID直到peer接收，禁止取消事务。无法排空时需复位整个系统，不能直接启动下一笔。
- `last_case_cycles`包含预装载、控制轮询、计算和检查；不能当作纯核吞吐。

VIO输出：probe_out0=`run_test`、probe_out1=`stress_enable`。VIO/ILA输入顺序为flags4、fail_code8、cases_done6、pc13、last_case_cycles32、state4、actual_word64、init_done1。flags为`{fail,pass,done,busy}`；全部通过应为`0110`、cases_done=26、fail_code=0。

## 4 复现入口

在仓库根目录执行，必须使用正式解压IP目录和新的输出目录：

```bash
vivado -mode batch -source tools/split_d_nm37/scripts/create_project.tcl \
  -tclargs hls/fsa_stream_split_d/fsa_stream_split_d_build/solution1/impl/ip \
  build/split_d_nm37_<commit>
vivado -mode batch -source tools/split_d_nm37/scripts/simulate.tcl \
  -tclargs build/split_d_nm37_<commit>/project/split_d_nm37.xpr
vivado -mode batch -source tools/split_d_nm37/scripts/build_and_report.tcl \
  -tclargs build/split_d_nm37_<commit>/project/split_d_nm37.xpr
```

自包含交付由`python3 tools/split_d_nm37/package.py <正式IP目录> <新交付目录>`生成，包含正式IP、ROM、RTL、约束和相对路径`config/project_config.tcl`。上传后直接执行`vivado -mode batch -source scripts/create_packaged_project.tcl`，不需要修改绝对路径。

控制器unit fixture只验证错开AW/W、响应背压、错误字检测、超时及忙中复位；小型行为存储只用于这个unit test，不是系统HBM的替代。`unit_test.tcl`应从新的隔离目录执行。

完整系统testbench使用同一个HBM/SmartConnect/FSA/selftest BD，绕过的只有封装时钟输入缓冲和JTAG调试核。普通模式和stress模式各跑26笔，存在有限仿真超时。AMD说明XSIM使用HBM internal responder模型；真实HBM行为仍须板测确认：[PG276 Simulation](https://docs.amd.com/r/en-US/pg276-axi-hbm/Simulation)。

必须分别核对控制器unit、系统仿真、综合、实现setup/hold、CDC、DRC、bitstream和板测。成功创建工程或下载bitstream不等于数值验收。

实现后用`review_cdc.tcl <routed.dcp> <新审查目录>`导出CDC实际端点，分辨SmartConnect XPM FIFO数据路径及XSDB FIFO；脚本不豁免警告、不修改约束。还须人工核对全部bus-skew裕量。通过全部门槛后依次运行：

```bash
vivado -mode batch -source tools/split_d_nm37/scripts/write_bitstream.tcl \
  -tclargs <routed.dcp> <新bit目录>
vivado -mode batch -source tools/split_d_nm37/scripts/program_and_inventory.tcl \
  -tclargs <新bit目录>/split_d_nm37.bit <新bit目录>/split_d_nm37.ltx
vivado -mode batch -source tools/split_d_nm37/scripts/run_board_selftest.tcl \
  -tclargs <同一个ltx> <新板测证据目录>
```

板测只选择JTAG线缆`210017937722A`及唯一VU37P，采用易失JTAG配置。脚本运行普通/stress各26笔，保存每次新启动busy触发及终态ILA/CSV、VIO状态CSV。接受条件为flags=6、fail_code=0、cases_done=26、init_done=1；启动捕获防止复用上轮PASS。未执行这些脚本前不得宣称板测通过。用户2026-10-11选择上板前默认RTL验初始化、忙中整体复位及首笔完整事务；全部52笔验收由真实HBM板测完成。默认TB仍包含52笔，若首笔后受控停止，必须记录实际覆盖与停止原因，不能称完整系统RTL通过。

可追加官方HBM TLM功能对照：`simulate.tcl <新工程副本路径> hbm_tlm`。仅切换Vivado实际允许的HBM模型，FSA、自检、SmartConnect仍使用RTL，ROM、52事务、全部canary/位模式不变。TLM的APB为stub，不能建模流量后的全HBM复位，故仅该对照排除忙中复位并在日志声明；默认`rtl`仍完整执行初始化与忙中整体复位。TLM结果不得作为实际HBM延迟、吞吐或物理时序证据。

Vivado2024.2的官方HBM TLM在`hbm_sc.h`将APB和`apb_complete`声明为`xsc_stub_port`，不模拟校准。可选TLM功能对照仅在仿真宏下将BD内部`init_done`适配为clock_locked，日志显式标记FUNCTIONAL ONLY；普通RTL模式无此适配。TLM对照不证明HBM初始化/校准/复位恢复，不能替代默认RTL流程或板测。

板级时钟必须按NM37原理图第3页处理：U7为100MHz LVDS，经100nF电容交流耦合，BH42/BJ42位于1.2V Bank65。XDC保留DIFF_SSTL12并显式ODT=RTT_48，提供接收端split终端/偏置，最终设计断言防止使用缺该修正的旧DCP。SW2低复位同时连接ZU5，不能当FSA独占复位。板级来源、检查步骤与未完成项见`docs/综合报告/NM37板测依据与原理图核查_20261010.md`。
