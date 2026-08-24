# `fsa_dma_top` IP Vivado上板验证说明书

## 1. 目标与范围

本目录用于把当前 `fsa_dma_top` HLS IP加入 Vivado 2024.2工程，在NM37/XCVU37P上完成
一个不依赖主机软件的固定向量测试。

本测试采用纯硬件闭环：

```text
VIO run_test
    ↓
可综合测试控制器
    ├── AXI4-Lite：配置并启动 fsa_dma_top
    └── debug端口：预装Q/K/V并检查O
                           ↑
fsa_dma_top m_axi_gmem ↔ 片上AXI RAM
```

VIO只负责启动和读取状态，不承载Q/K/V/O。板上测试控制器通过AXI4-Lite访问HLS控制
寄存器，片上AXI RAM代替外部HBM。因此本次能验证：

- `fsa_dma_top` AXI4-Lite寄存器配置和 `ap_start/ap_done` 控制；
- HLS `m_axi_gmem` 对64-bit AXI存储器的读写；
- `L=9`、4-token tile、最后一个非完整tile的完整9×4 FlashAttention；
- `status=0`、36个FP32输出和输出边界canary。

本次**不验证**PCIe、XDMA、HBM Controller、外部HBM地址空间、Linux驱动、Python/C++负载、
中断、最大长度和性能。

## 2. 当前输入状态和必须补齐的IP

当前检查到：

| 项目 | 内容 |
|---|---|
| HLS/Vivado版本 | Vitis HLS 2024.2，目标Vivado 2024.2 |
| HLS顶层 | `fsa_dma_top` |
| 预期VLNV | `xilinx.com:hls:fsa_dma_top:1.0` |
| 目标Part | `xcvu37p_CIV-fsvh2892-2-e` |
| 时钟 | NM37板载100 MHz差分时钟 |
| HLS复位 | `ap_rst_n`，同步低有效 |
| 控制接口 | 32-bit AXI4-Lite，7-bit地址 |
| 数据接口 | 64-bit地址、64-bit数据的AXI4 master |

现有 `build/fsa_dma_build/solution1/impl/verilog/fsa_dma_top.v` 能确认物理端口，但当前
工作区内没有报告所称的 `fsa_dma_top` `export.zip/component.xml`。因此创建工程前必须：

1. 从原始构建服务器取回对应版本的 `export.zip`，或者在Vitis HLS重新执行：

   ```tcl
   export_design -format ip_catalog -rtl verilog
   ```

2. 解压为：

   ```text
   fsa_dma_vivado/ip_repo/fsa_dma_top/component.xml
   ```

3. 确认解压目录同时包含 `hdl/`、`constraints/`、`xgui/` 等IP文件。

工程脚本会在缺少 `component.xml` 或VLNV不一致时停止，不会使用裸 `impl/verilog/`
冒充完整HLS IP。

## 3. 目录内容

```text
fsa_dma_vivado/
├── config/project_config.tcl
├── constraints/nm37_board.xdc
├── ip_repo/README.md
├── rtl/
│   ├── fsa_dma_axi_ram64.v
│   ├── fsa_dma_board_top.v
│   ├── fsa_dma_control_system.v
│   └── fsa_dma_test_controller.v
├── sim/tb_fsa_dma_control_system.v
├── scripts/create_vivado_project.tcl
└── fsa_dma_IP上板验证说明书.md
```

各文件职责：

- `fsa_dma_test_controller.v`：预装向量、AXI4-Lite master、完成轮询和结果自检；
- `fsa_dma_axi_ram64.v`：4 KiB板测AXI RAM，一次接受一个读burst和一个写burst；
- `fsa_dma_control_system.v`：连接控制器、HLS IP和AXI RAM；
- `fsa_dma_board_top.v`：连接NM37时钟、复位、VIO和ILA；
- `tb_fsa_dma_control_system.v`：绕过板级差分时钟，用100 MHz时钟仿真同一控制系统；
- `create_vivado_project.tcl`：注册HLS IP并创建Clocking Wizard、VIO、ILA和工程。

## 4. 固定测试

### 4.1 数据与地址

测试复用 `tests/hls/test_fsa_dma_top.cpp` 的生成规则：

```text
L = 9
D = SA_ROWS = 4
causal = false

Q[token][feature] = (((token + 2*feature) % 5) - 2) * 0.25
K[token][feature] = (((2*token + feature) % 7) - 3) * 0.25
V[token][feature] = (((token + feature) % 6) - 2) * 0.5
```

片上RAM地址分配如下：

| 区域 | 字节地址 | 大小 | 格式 |
|---|---:|---:|---|
| Q | `0x000` | 72 B | 9×4 FP16，row-major |
| K | `0x100` | 72 B | 9×4 FP16，row-major |
| V | `0x200` | 72 B | 9×4 FP16，row-major |
| O | `0x300` | 144 B | 9×4 FP32，row-major |
| O canary | `0x390`、`0x398` | 16 B | `0x5A5AA5A55A5AA5A5` |

每个64-bit Q/K/V字按HLS实现打包4个FP16，lane 0位于最低16 bit；每个64-bit O字打包
2个FP32，lane 0位于最低32 bit。

### 4.2 AXI4-Lite写入顺序

控制器按下表写入：

| 偏移 | 内容 |
|---:|---|
| `0x10/0x14` | Q地址低/高32 bit，值 `0x000` |
| `0x1C/0x20` | K地址低/高32 bit，值 `0x100` |
| `0x28/0x2C` | V地址低/高32 bit，值 `0x200` |
| `0x34/0x38` | O地址低/高32 bit，值 `0x300` |
| `0x40` | `sequence_length=9` |
| `0x48` | `causal=0` |
| `0x00` | `ap_start=1` |

之后带超时轮询 `0x00[1] ap_done`，读取 `0x54[0] status_ap_vld`，最后读取
`0x50[7:0] status`。

### 4.3 金标准比较

CPU金标准仍由普通softmax公式独立计算。板上控制器不用浮点运算器，而是把每个FP32
结果转换为保持数值顺序的整数key，检查是否落在对应CPU金标准 `±0.18` 的IEEE-754
边界内；NaN和Inf直接失败。这与当前C++ testbench的容差一致。

另外检查O后的两个64-bit canary保持不变，用于发现越界写。

## 5. 创建Vivado工程

在包含本说明书的目录执行：

```bash
vivado -mode batch \
  -source scripts/create_vivado_project.tcl \
  -tclargs config/project_config.tcl
```

创建成功后打开：

```bash
vivado vivado_project/fsa_dma_nm37_board_test.xpr
```

脚本自动完成：

1. 注册 `ip_repo/`；
2. 创建 `fsa_dma_top_0`；
3. 创建100 MHz差分输入的Clocking Wizard；
4. 创建VIO和ILA；
5. 加入RTL、testbench和NM37 XDC；
6. 设置综合顶层 `fsa_dma_board_top`；
7. 设置仿真顶层 `tb_fsa_dma_control_system`；
8. 生成各IP Output Products。

## 6. Vivado行为仿真

在Flow Navigator运行 **Run Simulation → Run Behavioral Simulation**。仿真时间已设置为
5 ms，testbench自身在4 ms处带有限超时。

通过时Tcl Console应出现：

```text
[PASS] fsa_dma_top AXI fixed-vector board-control test
```

若出现 `[FAIL]` 或 `[TIMEOUT]`，不要继续综合和上板。先查看：

- `debug_state`
- `debug_fail_code`
- `debug_status`
- `debug_check_index`
- AXI4-Lite和 `m_axi_gmem` 的valid/ready波形

## 7. 综合、实现和bitstream

行为仿真通过后依次执行：

1. Open Elaborated Design，确认 `u_test_controller`、`u_fsa_dma_top`、`u_axi_ram`、
   `u_vio`、`u_ila` 和 `u_clk_wiz` 均存在；
2. Run Synthesis；
3. Run Implementation；
4. 查看 Timing Summary、DRC和Methodology报告；
5. 只有时序和DRC合格后才Generate Bitstream。

最低时序验收条件：

- Setup WNS `>= 0 ns`；
- Hold WHS `>= 0 ns`；
- TNS和THS均为0；
- failing endpoints为0；
- 没有关键未约束路径；
- 没有阻止bitstream的DRC Error。

当前HLS报告的估算周期为7.935 ns，叠加2.7 ns uncertainty后相对100 MHz目标存在
0.635 ns风险。HLS估算不代替Vivado实现结果；不能用放宽时钟约束掩盖失败。

## 8. 下载和VIO操作

1. 打开Hardware Manager并连接NM37；
2. 确认识别到XCVU37P；
3. Program Device，同时选择同一次Implementation产生的 `.bit` 和 `.ltx`；
4. 下载后如有需要短按SW2，等待Clocking Wizard锁定和内部复位释放；
5. 打开VIO Dashboard；
6. 确认 `probe_out0/run_test=0`；
7. 先让ILA进入等待触发；
8. 将 `run_test` 从0改为1，再改回0；
9. 等待 `test_done=1`。

再次运行时必须重新产生 `run_test` 的 `0→1` 上升沿。

## 9. VIO和ILA探针

### 9.1 VIO

| Probe | 宽度 | 含义 |
|---|---:|---|
| `probe_out0` | 1 | `run_test` |
| `probe_in0` | 1 | `test_busy` |
| `probe_in1` | 1 | `test_done` |
| `probe_in2` | 1 | `test_pass` |
| `probe_in3` | 1 | `test_fail` |
| `probe_in4` | 6 | 控制器状态 |
| `probe_in5` | 8 | 首个失败码 |
| `probe_in6` | 8 | HLS `status` |
| `probe_in7` | 6 | 当前O检查字编号 |
| `probe_in8` | 32 | `ap_done`轮询次数 |

### 9.2 ILA

| Probe | 宽度 | 含义 |
|---|---:|---|
| 0 | 1 | `run_test` |
| 1 | 6 | 控制器状态 |
| 2 | 4 | `{fail,pass,done,busy}` |
| 3 | 7 | AXI-Lite AWADDR |
| 4 | 6 | AXI-Lite `{bready,bvalid,wready,wvalid,awready,awvalid}` |
| 5 | 7 | AXI-Lite ARADDR |
| 6 | 4 | AXI-Lite `{rready,rvalid,arready,arvalid}` |
| 7 | 32 | AXI-Lite RDATA |
| 8 | 64 | `m_axi_gmem` ARADDR |
| 9 | 4 | `m_axi_gmem`读握手 |
| 10 | 64 | `m_axi_gmem` AWADDR |
| 11 | 64 | `m_axi_gmem` WDATA |
| 12 | 6 | `m_axi_gmem`写握手/响应 |
| 13 | 8 | 失败码 |
| 14 | 6 | O检查字编号 |

推荐ILA触发条件为 `run_test==1` 或 `test_fail==1`。

## 10. 通过条件和失败码

板上通过必须同时满足：

```text
test_busy = 0
test_done = 1
test_pass = 1
test_fail = 0
debug_status = 0
```

失败码：

| 范围 | 含义 |
|---:|---|
| `0x01` | AXI4-Lite写响应错误 |
| `0x02` | AXI4-Lite读响应错误 |
| `0x03` | 等待 `ap_done` 超时 |
| `0x04` | `status_ap_vld` 未置位或读取失败 |
| `0x05` | HLS `status` 非0 |
| `0x10`～`0x21` | 对应O的64-bit字比较失败 |
| `0x42`～`0x43` | O后canary被修改 |
| `0x40` | AXI RAM检测到地址、burst、size或last协议错误 |

## 11. 常见问题

### 找不到HLS IP

检查 `ip_repo/fsa_dma_top/component.xml` 是否存在，VLNV是否为
`xilinx.com:hls:fsa_dma_top:1.0`。不要只复制HLS的裸Verilog目录。

### `fsa_dma_top_0`端口不匹配

说明取回的 `component.xml` 不是当前RTL对应版本。以取回IP生成的stub为准，核对
64-bit地址/数据、7-bit控制地址、AXI user/id/lock端口；不要直接删掉不匹配端口。

### 一直没有 `ap_done`

依次检查：

1. `ap_rst_n`是否已释放；
2. AXI-Lite写 `0x00` 是否完成；
3. `m_axi_gmem` AR/AW是否出现；
4. AXI RAM是否返回R/B响应；
5. `debug_fail_code`和ILA中的valid/ready；
6. 是否超过当前100万次轮询上限。

### `test_done=1`但 `test_fail=1`

先用失败码区分寄存器、超时、状态、O数值或canary，再用
`debug_check_index`定位第一笔输出。数值错误时与
`tests/hls/test_fsa_dma_top.cpp`和HLS Co-sim结果对照。

### 复位DRC或时钟覆盖警告

- NM37时钟必须使用 `BH42/BJ42`，不能使用旧笔记的 `BM43/BM42`；
- SW2低有效复位使用 `BF2`；
- 不要在XDC重复 `create_clock`；
- 不要把 `reset_n & locked` 组合后直接驱动异步复位。

### ILA导致时序压力

先减小ILA深度或移除64-bit WDATA等宽probe。不要放宽100 MHz目标规避核心时序问题。

## 12. 当前验证状态

| 验证层级 | 状态 | 证据或限制 |
|---|---|---|
| 板测包生成 | 已完成 | RTL、Tcl、XDC、testbench和本文已生成 |
| 文本静态检查 | 已完成 | 配置、实例名、寄存器偏移和probe宽度已互相核对 |
| 完整IP输入 | **缺失** | 当前工作区没有 `fsa_dma_top component.xml/export.zip` |
| Vivado行为仿真 | 未执行 | 必须补齐IP后在Vivado 2024.2运行 |
| Synthesis | 未执行 | 无Vivado结果 |
| Implementation与时序 | 未执行 | 无WNS/WHS/TNS/THS证据 |
| Bitstream | 未生成 | 必须在时序合格后生成 |
| FPGA下载 | 未执行 | 无Hardware Manager证据 |
| 板上固定测试 | 未执行 | 无VIO/ILA结果 |

只有行为仿真、Implementation时序和VIO固定向量分别通过后，才能分别记录对应结论。
本板测通过也不能证明XDMA/HBM/主机软件链路已经通过。

