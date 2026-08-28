不要一次完成所有 TODO。按下面 0→5 阶段推进，每完成一阶段就把证据包交给我核对；前一阶段失败时先停下，不进入下一阶段。这样最容易定位是 HBM、PCIe、XDMA、地址映射还是 FSA 的问题。

这是按 `vivado-hls-ip-board-test` 的分层验收原则整理的：固定向量板测、HBM、XDMA、端到端分别取证，不能互相代替。

## 阶段 0：补齐现有板测证据

在已经 `test_pass=1`、时序通过的 Vivado 工程中执行：

```tcl
set out_dir [file normalize "./evidence/00_board_test"]
file mkdir $out_dir

open_run impl_1

report_timing_summary \
    -delay_type min_max \
    -report_unconstrained \
    -max_paths 20 \
    -file [file join $out_dir timing_summary.rpt]

report_utilization \
    -hierarchical \
    -file [file join $out_dir utilization.rpt]

report_drc \
    -ruledeck default \
    -file [file join $out_dir drc.rpt]

redirect -file [file join $out_dir run_status.txt] {
    puts "Vivado=[version -short]"
    puts "Part=[get_property PART [current_project]]"
    puts "Synth=[get_property STATUS [get_runs synth_1]]"
    puts "Impl=[get_property STATUS [get_runs impl_1]]"
}
```

再保存：

- VIO 中 `test_busy=0、test_done=1、test_pass=1、test_fail=0` 的截图；
- 本次使用的 `.bit`、`.ltx` 文件名和 SHA256；
- 如果有，保存 Hardware Manager Program Device 成功截图。

验收标准：

- Setup WNS ≥ 0；
- Hold WHS ≥ 0；
- TNS、THS 为 0；
- failing endpoints 为 0；
- 没有阻止 bitstream 的 DRC ERROR。

给我返回：

```text
00_board_test/
├── timing_summary.rpt
├── utilization.rpt
├── drc.rpt
├── run_status.txt
├── vio_pass.png
└── bit_ltx_sha256.txt
```

---

## 阶段 1：单独验证 HBM

### 1.1 创建 HBM 工程

在 Vivado 2024.2：

1. 新建工程，Part 选择 `xcvu37p_CIV-fsvh2892-2-e`。
2. 添加 `AXI High Bandwidth Memory Controller`。
3. 

HBM 的接口、初始化完成信号和 256-bit 对齐要求参考 [PG276](https://docs.amd.com/r/en-US/pg276-axi-hbm/Port-Descriptions)。

### 1.2 时钟候选

使用 NM37：

```text
CLK_100_DDR_P = BH42
CLK_100_DDR_N = BJ42
IOSTANDARD     = DIFF_SSTL12
```

通过差分输入缓冲和 Clocking Wizard/BUFG 产生 HBM reference、APB 和 AXI 时钟。必须以 HBM IP 的 DRC 结果确认该时钟满足 GCIO、SLR 和抖动要求。

不要用 PCIe `PCIE_CLK0` 代替 HBM 时钟。

### 1.3 生成报告

```tcl
set out_dir [file normalize "./evidence/01_hbm"]
file mkdir $out_dir

redirect -file [file join $out_dir hbm_properties.txt] {
    report_property -all [get_ips hbm_0]
}

report_drc \
    -ruledeck default \
    -file [file join $out_dir hbm_drc.rpt]

# Implementation完成后执行
open_run impl_1
report_timing_summary \
    -delay_type min_max \
    -report_unconstrained \
    -file [file join $out_dir hbm_timing_summary.rpt]
```

### 1.4 上板

1. 下载 HBM example design bitstream。
2. Hardware Manager 中检查 HBM 状态。
3. 确认所有启用 stack 的 `apb_complete_0/1=1`。
4. 启动 HBM Traffic Generator。
5. 等待读写比较结束。

验收标准：

- HBM 状态为 `Complete`；
- 启用的 `apb_complete_*=1`；
- CATTRIP 未置位；
- TG `error_count=0`；
- Implementation 时序通过。

给我返回：

```text
01_hbm/
├── hbm_properties.txt
├── hbm_drc.rpt
├── hbm_timing_summary.rpt
├── hbm_hw_status.png
├── hbm_tg_result.png
├── hbm_example.xdc
└── vivado.log
```

如果失败，再补充：

```text
<project>.runs/synth_1/runme.log
<project>.runs/impl_1/runme.log
```

---

## 阶段 2：单独验证 XDMA 和 PCIe 链路

### 2.1 创建 XDMA example design

新建同 Part 工程，加入 `DMA/Bridge Subsystem for PCI Express`，设置：

```text
Functional Mode       = DMA
Interface              = AXI Memory Mapped
PCIe                   = Gen3 x8
H2C channels           = 1
C2H channels           = 1
AXI4-Lite Master       = Enabled
AXI-Stream             = Disabled
```

Gen3 x8 属于该器件速度等级支持范围，见 [PG195](https://docs.amd.com/r/en-US/pg195-pcie-dma/Minimum-Device-Requirements)。

生成 XDMA example design，但先不要接受任意默认管脚。核对：

```text
lane 0~3 -> Bank 227，channel 3~0
lane 4~7 -> Bank 226，channel 3~0
REFCLK   -> PCIE_CLK0_P/N，AL15/AL14
PERST#   -> BF5
```

如果生成 XDC 与上述映射不一致，先停止，把 XDC 发给我，不要强行手改到能综合。

### 2.2 收集配置

```tcl
set out_dir [file normalize "./evidence/02_xdma"]
file mkdir $out_dir

redirect -file [file join $out_dir xdma_properties.txt] {
    report_property -all [get_ips xdma_0]
}

report_drc \
    -ruledeck default \
    -file [file join $out_dir xdma_drc.rpt]

open_run impl_1
report_timing_summary \
    -delay_type min_max \
    -report_unconstrained \
    -file [file join $out_dir xdma_timing_summary.rpt]
```

### 2.3 下载并检查 Linux 枚举

下载 bitstream 后，在主机执行：

```bash
lspci -nn | grep -i xilinx
sudo lspci -s <实际BDF> -vv
sudo dmesg -T | tail -n 200
```

验收标准：

- `user_lnk_up=1`；
- `LnkSta` 显示 `Speed 8GT/s`；
- `LnkSta` 显示 `Width x8`；
- 没有持续出现 Corrected/Uncorrected AER；
- endpoint 在重启后可以稳定枚举。

给我返回：

```text
02_xdma/
├── xdma_properties.txt
├── xdma_generated.xdc
├── xdma_drc.rpt
├── xdma_timing_summary.rpt
├── lspci_nn.txt
├── lspci_vv.txt
├── dmesg.txt
└── link_status.png
```

其中 `xdma_generated.xdc` 最重要，我会逐 lane 核对原理图。

---

## 阶段 3：集成 XDMA-HBM 回环

### 3.1 Block Design

按下面连接：

```text
xdma_0/M_AXI
    ↓ AXI Clock Converter
SmartConnect/S00_AXI
                       SmartConnect/M00_AXI
                              ↓
                         hbm_0/AXI_00
```

第一阶段 SmartConnect 只使用一个输入 master。设置要求：

- XDMA 侧保持其生成的数据宽度；
- HBM 侧必须是 256-bit AXI3；
- SmartConnect 完成 AXI4→AXI3 和时钟转换；
- HBM 地址空间从 `0x00000000` 开始；
- 任何 DMA 请求必须等 `apb_complete_*=1`。

### 3.2 导出地址图

Validate Design、Assign Address 后保存 Address Editor 截图，并运行：

```tcl
set out_dir [file normalize "./evidence/03_xdma_hbm"]
file mkdir $out_dir

redirect -file [file join $out_dir address_segments.txt] {
    foreach seg [get_bd_addr_segs -hier] {
        puts "$seg OFFSET=[get_property OFFSET $seg] RANGE=[get_property RANGE $seg]"
    }
}
```

把 Block Design 的 `.bd` 文件也返回给我。

### 3.3 安装驱动

在 Linux 主机记录版本：

```bash
uname -a
cd dma_ip_drivers
git rev-parse HEAD

cd XDMA/linux-kernel/xdma
make
sudo make install
sudo depmod -a
sudo modprobe xdma

cd ../tools
make

modinfo xdma
ls -l /dev/xdma* /dev/xdma/card* 2>/dev/null
```

把全部输出保存到 `driver_info.txt`。

### 3.4 回环测试

先运行：

```bash
./dma_to_device --help
./dma_from_device --help
```

确认当前版本参数，然后测试 32 B、4 KiB、64 KiB、1 MiB。典型形式：

```bash
head -c 1048576 /dev/urandom > input_1m.bin

./dma_to_device \
  -d /dev/xdma0_h2c_0 \
  -a 0x00000000 \
  -s 1048576 \
  -f input_1m.bin

./dma_from_device \
  -d /dev/xdma0_c2h_0 \
  -a 0x00000000 \
  -s 1048576 \
  -f output_1m.bin

cmp input_1m.bin output_1m.bin
sha256sum input_1m.bin output_1m.bin
```

验收标准：

- 所有命令退出码为 0；
- `cmp` 无输出；
- 输入和输出 SHA256 相同；
- AXI `RRESP/BRESP=OKAY`；
- 没有 HBM CATTRIP、DECERR 或 SLVERR。

给我返回：

```text
03_xdma_hbm/
├── design_1.bd
├── address_editor.png
├── address_segments.txt
├── driver_info.txt
├── loopback_commands.log
├── loopback_sha256.txt
├── lspci_vv.txt
├── dmesg_after_loopback.txt
├── timing_summary.rpt
└── drc.rpt
```

若回环失败，再导出第一次错误前后的 ILA 波形 CSV，至少包含：

```text
AWADDR AWLEN AWSIZE AWVALID AWREADY
WDATA WSTRB WLAST WVALID WREADY
BRESP BVALID BREADY
ARADDR ARLEN ARSIZE ARVALID ARREADY
RDATA RRESP RLAST RVALID RREADY
apb_complete_0 apb_complete_1
```

---

## 阶段 4：加入 FSA IP

在已通过回环的 BD 中增加：

```text
xdma_0/M_AXI_LITE
    ↓ AXI-Lite Clock Converter
fsa_dma_top_0/s_axi_control

fsa_dma_top_0/m_axi_gmem
    ↓
SmartConnect/S01_AXI

SmartConnect/M00_AXI
    ↓
hbm_0/AXI_00
```

地址固定为：

```text
FSA control = 0x00000000，相对 /dev/xdma0_user

Q = 0x00000000
K = 0x00100000
V = 0x00200000
O = 0x00300000
```

检查：

- XDMA 和 FSA 看到的 HBM segment 数值完全相同；
- FSA 是 64-bit AXI4；
- HBM 端是 256-bit AXI3；
- HBM 端 `AxSIZE=5`；
- FSA 保持 100 MHz；
- HBM 未完成初始化时不能启动 FSA；
- `ap_rst_n` 是同步低有效复位；
- `auto_restart=0`。

完成 Synthesis/Implementation 后，把以下内容交给我：

```text
04_full_system/
├── design_1.bd
├── design_1_wrapper.v
├── 所有用户XDC
├── address_editor.png
├── address_segments.txt
├── xdma_properties.txt
├── hbm_properties.txt
├── timing_summary.rpt
├── utilization.rpt
├── drc.rpt
├── clock_interaction.rpt
└── bit_ltx_sha256.txt
```

验收标准仍是 WNS/WHS≥0、TNS/THS=0、无关键未约束路径。

---

## 阶段 5：FSA 端到端测试

当前 IP 是 4×4，先使用 `L=9、causal=false`：

```text
Q/K/V逻辑大小 = 9 × 4 × 2 = 72 B
Q/K/V DMA大小 = 96 B，补齐到32 B

O逻辑大小     = 9 × 4 × 4 = 144 B
O DMA大小     = 160 B，补齐到32 B
```

调用顺序：

1. H2C 写入 Q、K、V；
2. 检查 `AP_CTRL.ap_idle=1`；
3. 写 Q/K/V/O 的64-bit地址；
4. 写 `sequence_length=9`；
5. 写 `causal=0`；
6. 写 `ap_start=1`；
7. 带超时轮询 `ap_done`；
8. 读 `status_ap_vld`，必须为1；
9. 读 `status`，必须为0；
10. C2H读回O；
11. 与独立CPU金标准比较，沿用当前 testbench 的绝对误差上限 `0.18`。

需要保存每次寄存器访问：

```text
WRITE 0x10 q_low
WRITE 0x14 q_high
...
WRITE 0x40 9
WRITE 0x48 0
WRITE 0x00 1
READ  0x00 <value>
READ  0x54 <value>
READ  0x50 <value>
```

给我返回：

```text
05_fsa_e2e/
├── register_trace.txt
├── q.bin
├── k.bin
├── v.bin
├── o_fpga.bin
├── o_cpu.bin或o_cpu.csv
├── compare_result.txt
├── program_stdout.txt
├── program_stderr.txt
├── lspci_vv.txt
└── dmesg_after_fsa.txt
```

通过后再测试：

- `causal=true`；
- `L=0`，预期 status=1；
- `L=4097`，预期 status=1；
- 连续运行10次；
- 更大的合法 L。

## 你现在先做什么

现在只做两个工作：

1. 完成阶段0，给我 `00_board_test`；
2. 完成阶段1，给我 `01_hbm`。

把它们压缩成：

```text
nm37_evidence_phase0_phase1.zip
```

放进工作区，或直接上传给我。遇到错误时不要只发截图：同时给我完整 `vivado.log`、对应 run 的 `runme.log`、DRC 和 timing report。我核对这两阶段后，再根据实际 HBM IP 参数帮你确定 XDMA-HBM Block Design，避免后面反复重搭。