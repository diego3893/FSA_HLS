# `fsa_dma_top` 迁移到 U280：Vivado 2024.2 操作与验收说明书

## 1. 说明书目标

本文是 `FSA_HLS/vivado_U280/` 工程包的操作手册。按顺序执行后，应得到以下独立工程：

```text
Linux主机
  │ PCIe Gen3 x8
  ▼
XDMA
  ├── M_AXI_LITE ──┬──> fsa_dma_top/s_axi_control
  │                 └──> u280_status_axil
  └── M_AXI ─────────────┐
                         ├──> SmartConnect ──> HBM AXI_00
fsa_dma_top/m_axi_gmem ──┘
```

复用的原FSA U280板级参数如下：

| 项目 | 固定值 |
|---|---|
| Part | `xcu280-fsvh2892-2L-e` |
| Board Part | `xilinx.com:au280:part0:1.1` |
| 系统时钟 | `BJ43/BJ44`，LVDS，300 MHz |
| HBM参考时钟 | `G31/F31`，LVDS，100 MHz |
| PCIe参考时钟 | `AR15/AR14`，100 MHz |
| PCIe PERST# | `BH26`，LVCMOS18，低有效，pull-up |
| PCIe | Gen3 x8，`PCIE4C_X1Y0`，`GTY_Quad_227` |
| XDMA | 256-bit、64-bit地址、4-bit ID、250 MHz |
| HBM | 8 GB、单stack、只启用`AXI_00`、XSDB开启 |
| HBM AXI_00 | 256-bit AXI3、225 MHz、只用低256 MiB |
| FSA | 4×4、100 MHz、64-bit AXI4 master |

当前只完成了文件生成和文本级静态检查；Vivado仿真、综合、实现、bitstream和U280板测均需按本文实际执行。

## 2. 工程包和执行规则

进入工程包：

```bash
cd FSA_HLS/vivado_U280
```

目录职责：

```text
config/project_config.tcl              公共参数
common/constraints/u280_pins.xdc       U280最终管脚
common/rtl/u280_pcie_reset.sv          PCIe重配置复位
common/rtl/u280_status_axil.sv          板级状态寄存器
common/rtl/u280_system_top.sv           物理顶层
common/tcl/create_system_project.tcl    阶段5/6 BD生成器
00_preflight/                           环境检查
01_hls_ip/                              U280 HLS IP导出
02_clock_reset/                         时钟复位工程
03_xdma/                                XDMA官方例程
04_hbm/                                 HBM官方例程
05_xdma_hbm/                            XDMA-HBM回环
06_fsa_system/                          完整工程
07_host/                                Linux板测程序
scripts/build_and_report.tcl            构建与报告
scripts/program_device.tcl              下载bit/ltx
scripts/run_status_sim.tcl              状态寄存器仿真
```

必须按阶段0→1→状态寄存器仿真→2→3→4→5→6→7执行。每阶段只有`通过`、`失败`、`未执行`三种结论；上一阶段失败时停止。

## 3. 准备Vivado 2024.2终端

Linux示例：

```bash
source /tools/Xilinx/Vivado/2024.2/settings64.sh
vivado -version
vitis_hls -version
cd <FlashAttention仓库>/FSA_HLS/vivado_U280
```

两条版本命令都必须显示2024.2。Windows应使用Vivado 2024.2 Tcl Shell进入相同目录；Linux板测命令仍在U280主机执行。

## 4. 阶段0：环境、器件和IP检查

### 4.1 执行

```bash
vivado -mode batch -source 00_preflight/check_environment.tcl \
  2>&1 | tee reports/00_preflight_console.log
```

脚本检查Vivado版本、U280器件/board part以及XDMA、HBM、Clocking Wizard、Processor System Reset、AXI Clock Converter、SmartConnect和ILA。

### 4.2 得到验收结果

```bash
tail -n 20 reports/00_preflight_console.log
cat reports/00_preflight.txt
```

必须看到：

```text
VIVADO_VERSION=2024.2...
TARGET_PART_COUNT=1
BOARD_PART_COUNT=1
IPDEF_xilinx.com:ip:xdma:4.1=1
IPDEF_xilinx.com:ip:hbm:1.0=1
...
PREFLIGHT_PASS
```

失败处理：`TARGET_PART_COUNT=0`时安装UltraScale+ HBM器件支持；`BOARD_PART_COUNT=0`时安装U280 board files；任一`IPDEF_...=0`时补齐Vivado组件。此时`U280_HLS_COMPONENT_COUNT=0`是正常的，阶段1尚未导出IP。

## 5. 阶段1：针对U280重新生成HLS IP

现有 `fsa_dma_vivado/ip_repo` IP目标为XCVU37P，只能作接口参考，不能作为最终U280 IP。

### 5.1 执行完整HLS流程

```bash
vitis_hls -f 01_hls_ip/run_hls_u280.tcl \
  2>&1 | tee reports/01_hls_u280_console.log
```

脚本固定top=`fsa_dma_top`、part=`xcu280-fsvh2892-2L-e`、周期10 ns、4×4阵列、`L_MAX=4096`，依次运行C simulation、C synthesis、Verilog RTL co-simulation和IP export。

### 5.2 C simulation验收

```bash
grep -n "\[PASS\] test_fsa_dma_top" \
  build/hls_fsa_dma_u280/solution1/csim/report/fsa_dma_top_csim.log
```

必须返回包含`one start produced complete 9x4 O`的一行。无该行或退出码非0时停止。

### 5.3 C synthesis验收

```bash
grep -n -E "Target|Estimated|Latency|Interval|fsa_dma_top" \
  build/hls_fsa_dma_u280/solution1/syn/report/fsa_dma_top_csynth.rpt | head -n 40
less build/hls_fsa_dma_u280/solution1/syn/report/fsa_dma_top_csynth.rpt
```

目标周期必须为10 ns，Estimated Clock必须小于10 ns。接口仍应是32-bit数据/7-bit地址AXI-Lite和64-bit AXI master。HLS估算不能替代板级Implementation时序。

### 5.4 RTL协同仿真验收

```bash
cat build/hls_fsa_dma_u280/solution1/sim/report/fsa_dma_top_cosim.rpt
```

Verilog行Status必须为`Pass`。否则保存报告和`solution1/sim/verilog/`日志，停止。

### 5.5 解压并确认目标器件

```bash
python3 01_hls_ip/unpack_ip.py
find ip_repo -name component.xml -print
find ip_repo -name ReleaseNotes.txt -print -exec cat {} \;
```

Python命令必须打印`IP_UNPACK_PASS=...component.xml`；ReleaseNotes必须显示U280，不得仍是`xcvu37p_CIV`。若目标目录已存在，脚本会拒绝覆盖，人工确认旧目录后先移到备份位置。

## 6. 验证新增状态寄存器

状态寄存器不依赖XDMA/HBM模型，先单独仿真：

```bash
vivado -mode batch -source scripts/run_status_sim.tcl \
  2>&1 | tee reports/status_register_sim.log
grep -n "\[PASS\] u280_status_axil register test" \
  reports/status_register_sim.log
```

必须找到PASS且无`FATAL`。仿真实际检查：状态基址+`0x00`为`0x46534131`（`FSA1`）；`0x04` bit0/1/2分别是HBM完成、XDMA link、CATTRIP；`0x0c`为`0x10000000`。状态块在User BAR中的基址为`0x00010000`。

## 7. 阶段2：时钟、复位和普通IO

### 7.1 创建工程

```bash
vivado -mode batch -source 02_clock_reset/create_project.tcl \
  2>&1 | tee reports/02_clock_reset_create.log
```

必须打印`PROJECT_CREATED=...fsa_u280_02_clock_reset.xpr`。

### 7.2 综合并生成报告

```bash
vivado build/02_clock_reset/fsa_u280_02_clock_reset.xpr
```

在Vivado Tcl Console执行：

```tcl
launch_runs synth_1 -jobs 8
wait_on_run synth_1
open_run synth_1
file mkdir ../../reports/02_clock_reset
report_clocks -file ../../reports/02_clock_reset/report_clocks.rpt
report_clock_interaction -file ../../reports/02_clock_reset/clock_interaction.rpt
report_io -file ../../reports/02_clock_reset/report_io.rpt
report_cdc -details -file ../../reports/02_clock_reset/report_cdc.rpt
```

终端检查：

```bash
grep -n -E "100\.000|225\.000|300\.000" reports/02_clock_reset/report_clocks.rpt
grep -n -E "BJ43|BJ44|G31|F31|BH26" reports/02_clock_reset/report_io.rpt
```

必须有100/225 MHz输出、300 MHz输入和五个正确管脚。`u280_pcie_reset`应在每次FPGA配置后先保持PCIe reset，再同步释放，这是原FSA解决JTAG下载后仍显示golden image配置的修复。

## 8. 阶段3：独立XDMA验证

### 8.1 生成官方例程

```bash
vivado -mode batch -source 03_xdma/create_example_project.tcl \
  2>&1 | tee reports/03_xdma_create.log
```

打开`build/03_xdma_example/`中的XPR，依次执行Generate Output Products、Synthesis、Implementation和Generate Bitstream，然后下载并冷启动主机。

### 8.2 主机验收

```bash
lspci -nn | grep -i xilinx
sudo lspci -s <实际BDF> -vv | tee reports/03_xdma_lspci_vv.txt
dmesg | tail -n 200 | tee reports/03_xdma_dmesg.txt
ls -l /dev/xdma* | tee reports/03_xdma_devices.txt
```

`LnkSta`必须显示`Speed 8GT/s, Width x8`；必须是`Memory+`、驱动为`xdma`，并存在user、h2c_0、c2h_0设备节点。若只有x1/x4，检查插槽、lane和GT Quad；若Device ID仍是golden image，检查阶段2内部复位。本阶段不证明HBM。

## 9. 阶段4：独立HBM验证

### 9.1 生成官方例程

```bash
vivado -mode batch -source 04_hbm/create_example_project.tcl \
  2>&1 | tee reports/04_hbm_create.log
```

打开`build/04_hbm_example/`中的XPR，生成bitstream并下载。Example Design的100 MHz输入不是AXI_00时钟；AXI_00必须工作在225 MHz。

### 9.2 Hardware Manager验收

1. Open Hardware Manager并Auto Connect；
2. Program Device；
3. 打开HBM例程的VIO/ILA Dashboard；
4. 等待初始化稳定；
5. 触发唯一AXI_00 Traffic Generator；
6. 保存状态和TG统计截图到`reports/04_hbm/`。

必须同时满足：初始化`COMPLETE`、CATTRIP=0、TG写/读计数大于0、mismatch/error count=0。否则不连接XDMA。

## 10. 阶段5：XDMA-HBM回环

### 10.1 创建正式工程

```bash
vivado -mode batch -source 05_xdma_hbm/create_project.tcl \
  2>&1 | tee reports/05_xdma_hbm_create.log
```

脚本实际创建XDMA 250 MHz→Clock Converter→225 MHz SmartConnect→HBM AXI_00；AXI-Lite经250→100 MHz连接状态寄存器。

### 10.2 构建和报告

```bash
vivado -mode batch \
  -source scripts/build_and_report.tcl \
  -tclargs build/05_xdma_hbm/fsa_u280_05_xdma_hbm.xpr \
  2>&1 | tee reports/05_xdma_hbm_build.log

cat reports/fsa_u280_05_xdma_hbm/timing_gate.txt
cat reports/fsa_u280_05_xdma_hbm/drc_gate.txt
```

必须打印`BUILD_PASS`，并满足`SETUP_WNS>=0`、`HOLD_WHS>=0`、`DRC_ERROR_COUNT=0`。脚本遇到负setup/hold slack或DRC Error会直接失败。

### 10.3 下载

```bash
vivado -mode batch -source scripts/program_device.tcl \
  -tclargs <u280_system_top.bit> <对应ltx文件>
```

必须打印`PROGRAM_PASS`。JTAG重配置后进行cold reboot，或在无打开句柄时安全解绑驱动并remove/rescan。

### 10.4 逐字节回环

确保`dma_to_device`和`dma_from_device`在PATH中：

```bash
bash 05_xdma_hbm/run_loopback.sh 0 \
  | tee reports/05_xdma_hbm_loopback.log
```

脚本在`0x08000000`依次测试32 B、64 B、4 KiB、64 KiB和1 MiB，并用`cmp`逐字节比较。每个长度都要PASS，最后必须出现`XDMA_HBM_LOOPBACK_PASS`。发生超时时停止重复访问，检查HBM完成、256 MiB边界、Clock Converter复位和SmartConnect响应。

## 11. 阶段6：接入 `fsa_dma_top`

### 11.1 创建最终工程

```bash
vivado -mode batch -source 06_fsa_system/create_project.tcl \
  2>&1 | tee reports/06_fsa_system_create.log
```

若报没有`component.xml`，返回阶段1。地址图固定为：

```text
User BAR + 0x00000000 -> fsa_dma_top/s_axi_control
User BAR + 0x00010000 -> u280_status_axil

HBM AXI_00 0x00000000..0x0fffffff
Q=0x00000000 K=0x01000000 V=0x02000000 O=0x03000000
```

### 11.2 Validate Design和地址验收

```bash
vivado build/06_fsa_system/fsa_u280_06_fsa_system.xpr
```

Tcl Console：

```tcl
open_bd_design [get_files u280_system_bd.bd]
validate_bd_design
report_ip_status -file ../../reports/06_fsa_system_ip_status.rpt
get_bd_cells -hier
get_bd_addr_segs -hier
```

`validate_bd_design`必须无Error。层次中应各有一个`xdma_0`、`hbm_0`、`fsa_dma_top_0`、`status_regs_0`，并有三个Clock Converter和两个SmartConnect。Address Editor中XDMA和FSA必须映射到同一个256 MiB HBM segment；控制段只位于M_AXI_LITE空间。

### 11.3 综合实现

```bash
vivado -mode batch \
  -source scripts/build_and_report.tcl \
  -tclargs build/06_fsa_system/fsa_u280_06_fsa_system.xpr \
  2>&1 | tee reports/06_fsa_system_build.log

cat reports/fsa_u280_06_fsa_system/timing_gate.txt
cat reports/fsa_u280_06_fsa_system/drc_gate.txt
less reports/fsa_u280_06_fsa_system/post_route_cdc.rpt
less reports/fsa_u280_06_fsa_system/post_route_utilization.rpt
```

必须有`BUILD_PASS`、setup/hold slack非负、DRC Error为0。CDC重点检查250→100、250→225、100→225 MHz均经过Clock Converter；HBM完成和link只经两级同步进入状态寄存器。若100 MHz FSA路径失败，不能靠降频掩盖，应保存最差路径并判断是HLS core、64→256转换、SmartConnect还是跨SLR路径。

## 12. 阶段7：端到端板测

### 12.1 下载、冷启动和枚举

使用阶段10.3下载最终bit/ltx，然后cold reboot。检查：

```bash
lspci -nn | grep -i xilinx
sudo lspci -s <BDF> -vv | grep -E "LnkSta:|Kernel driver|Region"
ls -l /dev/xdma*
```

必须仍为8 GT/s x8，且user/H2C0/C2H0节点完整。

### 12.2 读取板级状态

```bash
sudo python3 07_host/fsa_u280.py --card 0 --status-only \
  | tee reports/07_status_only.log
```

程序先读`FSA1`标识，再轮询HBM和link。必须出现`BOARD_READY status=0x00000003`。bit2为1表示CATTRIP，程序会停止且不发HBM请求。

### 12.3 `L=9, causal=false`

```bash
sudo python3 07_host/fsa_u280.py --card 0 --length 9 \
  | tee reports/07_fsa_l9.log
```

程序实际生成与HLS testbench一致的Q/K/V，FP16 row-major打包，H2C写入，设置四个地址/L/causal，检查idle，启动并带超时等待done，检查status valid/status，C2H读回36个FP32，用独立CPU公式比较并检查O尾部canary。通过标志：

```text
FSA_BOARD_TEST_PASS L=9 causal=0 max_error=...
```

逐元素绝对误差上限为0.18。

### 12.4 causal、非法长度和连续运行

```bash
sudo python3 07_host/fsa_u280.py --card 0 --length 9 --causal \
  | tee reports/07_fsa_l9_causal.log
sudo python3 07_host/fsa_u280.py --card 0 --invalid-length 0 \
  | tee reports/07_invalid_0.log
sudo python3 07_host/fsa_u280.py --card 0 --invalid-length 4097 \
  | tee reports/07_invalid_4097.log
sudo python3 07_host/fsa_u280.py --card 0 --length 9 --repeat 10 \
  | tee reports/07_repeat_10.log
```

预期：causal打印`FSA_BOARD_TEST_PASS ... causal=1`；非法长度打印`INVALID_LENGTH_TEST_PASS`且status=1、O不变；连续运行打印10次完整PASS且无超时。

最后检查：

```bash
dmesg | tail -n 300 | tee reports/07_dmesg_after.txt
sudo lspci -s <BDF> -vv | tee reports/07_lspci_after.txt
```

不得出现新增严重AER、链路降速或驱动超时。

## 13. HLS寄存器表和重新核对方法

| 偏移 | 功能 |
|---:|---|
| `0x00` | AP_CTRL |
| `0x10/0x14` | Q低/高32 bit |
| `0x1c/0x20` | K低/高32 bit |
| `0x28/0x2c` | V低/高32 bit |
| `0x34/0x38` | O低/高32 bit |
| `0x40` | sequence_length |
| `0x48` | causal |
| `0x50` | status |
| `0x54` | status_ap_vld，读清零 |

每次重新导出IP后执行：

```bash
find ip_repo -name xfsa_dma_top_hw.h -print -exec grep "ADDR_" {} \;
```

若偏移变化，先修改`07_host/fsa_u280.py`，不得继续使用旧表。

## 14. 常见失败和直接证据

| 现象 | 立即查看 | 判断 |
|---|---|---|
| Vivado找不到U280 | `reports/00_preflight.txt` | 器件支持未安装 |
| IP仍是VU37P | IP ReleaseNotes | 阶段1没有真正换Part |
| Device ID仍是golden image | `u280_pcie_reset.sv`、PERST | PCIe硬核未重新复位 |
| PCIe只有x1/x4 | `lspci -vv`、lane XDC | 插槽或GT配置问题 |
| HBM不Complete | HBM VIO、refclk、CATTRIP | 不允许发DMA |
| 读正常写超时 | HBM XSDB、B通道 | 检查XSDB是否开启 |
| 总线访问后卡死 | 访问地址 | 很可能超过`0x0fffffff` |
| User BAR读全1 | Memory+、地址图、复位 | 不启动FSA |
| start后不done | AP_CTRL、HBM、AXI响应 | 超时后停止重复start |
| status=1 | L | 非法长度 |
| status=2 | core协议 | 回到HLS协同仿真并保存ILA |
| O错误但status=0 | 数据布局、地址、误差 | 比较第一行和首个AR burst |
| 第二次运行失败 | idle/start、静态状态 | 用`--repeat 10`复现 |
| 实现失败 | timing/CDC/DRC报告 | 不下载失败设计 |

## 15. 最终完成判定

只有以下证据全部真实取得，才能宣布迁移完成：

| 层级 | 通过证据 |
|---|---|
| 环境 | `PREFLIGHT_PASS` |
| HLS功能 | C simulation PASS |
| HLS RTL | co-simulation Verilog Pass |
| HLS IP | ReleaseNotes目标为U280 |
| 状态块 | `u280_status_axil`仿真PASS |
| XDMA | 8 GT/s x8、节点完整 |
| HBM | Complete、CATTRIP=0、TG error=0 |
| XDMA-HBM | `XDMA_HBM_LOOPBACK_PASS` |
| 最终实现 | `BUILD_PASS`、时序和DRC门禁通过 |
| 板级状态 | `BOARD_READY status=0x00000003` |
| 非causal | `FSA_BOARD_TEST_PASS L=9 causal=0` |
| causal | `FSA_BOARD_TEST_PASS L=9 causal=1` |
| 非法长度 | 两个`INVALID_LENGTH_TEST_PASS` |
| 连续运行 | 10次PASS且无AER和超时 |

任一行没有实际运行结果就保持`未执行`。工程创建、bitstream生成和板上计算正确是三个不同结论，不能互相替代。
