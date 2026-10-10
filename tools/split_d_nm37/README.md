# Split-D NM37真实HBM集成

## 1 范围与状态

这是4×4/head16 Split-D的完整存储与控制集成工程。计算IP仍由仓库标准入口`./run_hls.sh fsa_stream_split_d`生成；本目录的Vivado流程不替代、不缩减HLS测试。

当前状态：控制器4项单元检查与正式计算IP全流程已通过；真实HBM工程已生成。首轮实现数值时序通过，但参考时钟模型存在4条methodology Critical，已修正待新实现；系统仿真已定位辅助复位配置错误（低有效却接0），显式改active-high并加释放检查后待正式重试。bitstream和板测未执行。最终被测版本与结果应以综合报告和本次1316修改日志为准。

## 2 系统合同

- NM37：`xcvu37p_CIV-fsvh2892-2-e`，差分时钟BH42/BJ42、低有效复位BF2。
- 计算核/自检/AXI-Lite100MHz，计算时钟setup uncertainty2.7ns；HBM AXI225MHz、APB/reference100MHz。
- Clocking Wizard和每个时钟域的Processor System Reset生成真实时钟与复位。外部按钮仅作为异步复位输入排除；内部复位、AXI与数据路径保持时序分析。
- 既有NM37 HBM工程的双stack、总8GB配置，启用一个SAXI_00端口。四个64-bit计算bundle保留；SmartConnect汇聚四个计算master与一个自检master，并转换到真实256-bit HBM AXI端口。这是单HBM端口功能集成，不代表32端口峰值带宽。
- Q/K/V/O地址分别为0、1MB、2MB、3MB；都落在首256MB映射内，不重叠。AXI-Lite寄存器地址由正式导出RTL核对。

## 3 自检与诊断

`generate_program.py`把25d6dda原始TV固化为8192×128bit ROM（6745条有效指令）。硬件先写入真实HBM，再设置四个64-bit地址、length、causal和ap_start，轮询完成后按原始64-bit字比较。

- 24个有效事务：全部1576个O字、两端canary、输入Q/K/V保持检查。
- L0和4097：全部32768个O字与两个guard保持canary，status必须为1。
- AXI AW/W分别握手，阻塞时保持VALID和数据；B/R的RESP及单拍RLAST均检查。
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
