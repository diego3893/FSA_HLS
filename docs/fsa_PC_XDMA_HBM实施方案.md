# FSA 主机 PC/XDMA/HBM 实施方案

> 更新日期：2026-08-25  
> 目标：在 NM37 的 XCVU37P 上形成“Linux 主机通过 XDMA 搬运数据并控制
> `fsa_dma_top`，`fsa_dma_top` 通过 HBM 完成一次完整 Attention”的第一版闭环。

## 1. 固定的系统路线

```text
Linux 主机 PC
├── XDMA H2C：把 Q/K/V 写入板卡 HBM
├── XDMA AXI4-Lite Master：配置并启动 fsa_dma_top
└── XDMA C2H：从板卡 HBM 读回 O
              │ PCIe
              ▼
NM37 / XCVU37P
├── XDMA IP（AXI Memory Mapped 模式）
├── AXI Clock Converter / SmartConnect
├── HBM Controller
└── fsa_dma_top
    ├── s_axi_control：地址、L、causal、start/done/status
    └── m_axi_gmem：主动从 HBM 读取 Q/K/V 并写回 O
```

第一版不使用板上 `ZU5EV` 的 ARM，也不在 VU37P 中加入 MicroBlaze。Linux 主机是唯一的
软件执行端；VU37P 只运行 XDMA、HBM、AXI 互连和 FSA 硬件逻辑。

这里的“DMA”有两层，不能混淆：

- **XDMA H2C/C2H**：负责主机内存与板卡 HBM 之间的数据搬运；
- **`fsa_dma_top/m_axi_gmem`**：负责板卡内部 HBM 与 FSA Core 之间的数据搬运。

主机不逐 tile 调用内部 `fsa_core`。一次 `ap_start` 对应一次完整任务，query/KV tile 循环
全部由 `fsa_dma_top` 内部完成。

## 2. 当前证据与验证边界

| 项目 | 当前状态 | 证据或限制 |
|---|---|---|
| HLS C simulation | 通过 | 当前构建的 `L=9`、4×4、非整 tile 样例 |
| HLS C synthesis | 完成 | 100 MHz 目标，当前导出 IP 对应 4×4 配置 |
| RTL co-simulation | 通过 | 单事务 27,566 cycles，详见 `fsa_dma综合报告.md` |
| 可注册 HLS IP | **已具备** | `fsa_dma_vivado/ip_repo/fsa_dma_top/component.xml`、RTL、驱动和 `xgui/` 均存在 |
| 固定向量 IP 板测 | **通过（用户确认）** | `test_pass=1`；板测使用片上 AXI RAM，不是 HBM |
| 固定向量工程 Implementation 时序 | **通过（用户确认）** | 尚未在仓库中保存 WNS/WHS/TNS/THS 数值和报告路径 |
| XDMA 链路 | 未执行/未提供 | 没有链路训练、BDF、`lspci` 或驱动日志 |
| HBM 初始化与读写 | 未执行/未提供 | 没有 `apb_complete_*`、HBM TG 或 XDMA-HBM 回环证据 |
| XDMA + HBM + FSA 端到端 | 未执行/未提供 | 没有真实 HBM 上的 Q/K/V/O 金标准结果 |

固定向量板测已经证明当前 `fsa_dma_top` IP 的 AXI4-Lite 控制、64-bit
`m_axi_gmem`、固定样例和 NM37 上的实现时序可以工作。它没有包含 XDMA、HBM Controller、
PCIe 驱动或主机软件，因此不能替代后续 XDMA-HBM-FSA 系统验证。

旧版 `docs/fsa_dma综合报告.md` 中“IP 导出未执行”的结论描述的是更早的 Solution 状态；
当前 `fsa_dma_vivado/ip_repo` 已经补齐导出 IP，应以本文件检查到的实际目录为准。

## 3. 已核对的 `fsa_dma_top` IP

### 3.1 IP 身份与接口

以下信息来自实际 `component.xml`、生成顶层 RTL 和 `xfsa_dma_top_hw.h`：

| 项目 | 实际值 |
|---|---|
| IP 路径 | `fsa_dma_vivado/ip_repo/fsa_dma_top` |
| 原始压缩包 | `fsa_dma_vivado/ip_repo/xilinx_com_hls_fsa_dma_top_1_0.zip` |
| VLNV | `xilinx.com:hls:fsa_dma_top:1.0` |
| 生成工具 | Vitis HLS 2024.2 |
| 目标 Part | `xcvu37p_CIV-fsvh2892-2-e` |
| HLS 时钟目标 | 10 ns，即 100 MHz |
| 时钟/复位 | `ap_clk`；同步低有效 `ap_rst_n` |
| 控制接口 | AXI4-Lite slave，32-bit data、7-bit address |
| 数据接口 | AXI4 master `m_axi_gmem`，64-bit address、64-bit data、1-bit ID |
| 中断 | `interrupt`，可由 `ap_done`/`ap_ready` 事件产生 |
| AXI USER 端口 | 打包参数中禁用；以 Vivado 生成的 IP wrapper/stub 为准 |

`component.xml` 还记录了 16 个读/写 outstanding 和最大 16-beat 读/写 burst 的接口元数据。
系统集成时仍要以 Vivado 生成 wrapper、接口属性和实际波形为最终依据。

当前 IP 是 4×4 构建：`SA_ROWS=4`、`SA_COLS=4`、
`MAX_SEQUENCE_LENGTH=4096`。若重新导出 128×4 等配置，软件的数据尺寸、容量、金标准、
性能和 bitstream 必须与新 IP 一起更新；主机不能从现有寄存器自动读取这些编译期参数。

### 3.2 控制寄存器

| 相对偏移 | 位 | 含义与访问语义 |
|---:|---|---|
| `0x00` | bit 0 | `ap_start`，读写，握手后清零（COH） |
| `0x00` | bit 1 | `ap_done`，读清零（COR） |
| `0x00` | bit 2 | `ap_idle`，只读 |
| `0x00` | bit 3 | `ap_ready`，读清零（COR） |
| `0x00` | bit 7 | `auto_restart`；第一版保持 0 |
| `0x00` | bit 9 | 中断汇总状态，只读 |
| `0x04` | bit 0 | Global Interrupt Enable；第一版保持 0 |
| `0x08` | bit 1:0 | `ap_ready`/`ap_done` 中断使能；第一版保持 0 |
| `0x0c` | bit 1:0 | IP Interrupt Status，写 1 翻转清除（TOW） |
| `0x10`, `0x14` | 63:0 | Q 卡内地址，低 32 bit 在前 |
| `0x1c`, `0x20` | 63:0 | K 卡内地址，低 32 bit 在前 |
| `0x28`, `0x2c` | 63:0 | V 卡内地址，低 32 bit 在前 |
| `0x34`, `0x38` | 63:0 | O 卡内地址，低 32 bit 在前 |
| `0x40` | 31:0 | `sequence_length` |
| `0x48` | bit 0 | `causal` |
| `0x50` | 7:0 | `status`：0 成功、1 非法 L、2 core 协议错误 |
| `0x54` | bit 0 | `status_ap_vld`，读清零（COR） |

轮询 `0x00` 时必须保存第一次读到的 `ap_done=1`，因为该位读后会清零。完成后先读
`0x54` 确认 `status_ap_vld=1`，再读 `0x50`。第一版不启用 `auto_restart` 和中断。

## 4. NM37 原理图确认结果

以下映射来自 `docs/NM37_SCH_220331.pdf` 第 7 页，并已对原图做视觉核对。

### 4.1 PCIe lane

CN1 是 PCIe x16 边缘连接器，lane 0~15 全部连接到 XCVU37P GTY Bank 224~227：

| CN1 逻辑 lane | VU37P GTY Bank | Bank 内 channel 顺序 |
|---|---:|---|
| 0~3 | 227 | lane 0→channel 3，lane 3→channel 0 |
| 4~7 | 226 | lane 4→channel 3，lane 7→channel 0 |
| 8~11 | 225 | lane 8→channel 3，lane 11→channel 0 |
| 12~15 | 224 | lane 12→channel 3，lane 15→channel 0 |

Bank 内逻辑 lane 与 GTY channel 反序，XDMA 必须使用与该布线一致的 lane mapping/lane
reversal。不能只按连续 channel 顺序手写 XDC。

### 4.2 PCIe 参考时钟与复位

- CN1 的 `REFCLK+/-` 形成 `PCIE_REFCLK_DP/DN`，经 U12 `ICS85411AMLF` 缓冲；
- **`PCIE_CLK0_P/N` 接 Bank 227 的 `MGTREFCLK0P/N`，管脚 `AL15/AL14`；**
- **`PCIE_CLK1_P/N` 接 Bank 225 的 `MGTREFCLK0P/N`，管脚 `AR15/AR14`；**
- CN1 的 `PERST#` 经 R41 和 U13 电平转换成为 `PERST_PCIE_1V8`，进入 VU37P `BF5`。

旧版方案曾把 `PCIE_CLK0` 和 `PCIE_CLK1` 对应 Bank 写反，现已按原理图更正。第一版
x8 基线使用 lane 0~7，因此优先选 Bank 227 的 `PCIE_CLK0_P/N`。最终 PCIe block
location、GT channel 和 XDC 必须由 XDMA IP 的 location-specific 生成结果与原理图共同核对。

### 4.3 HBM 边界

XCVU37P 封装内含 8 GB HBM2，器件提供 32 个 HBM AXI 接口。HBM 数据 DQ/DQS 不经过
PCB 用户管脚，因此没有用户手写的 HBM DQ/DQS XDC；板上外部 PL DDR4 也不是本路线的
数据存储器。容量与接口数量可在
[AMD Virtex UltraScale+ HBM 产品表](https://www.amd.com/en/products/adaptive-socs-and-fpgas/fpga/virtex-ultrascale-plus-hbm.html)
核对。

HBM Controller 的用户侧是 256-bit AXI3 slave，要求 HBM 侧 `AxSIZE=5`，地址按 32 字节
边界对齐。当前 FSA 是 64-bit AXI4 master，**不能直接连接 HBM 端口**，必须通过
SmartConnect，或显式使用 AXI Protocol Converter + Data Width Converter。HBM 的接口与
对齐限制见 [PG276 AXI Considerations](https://docs.amd.com/r/en-US/pg276-axi-hbm/AXI-Considerations)。

## 5. 第一版工程基线

下表是为了尽快完成功能闭环而固定的建议基线。若 Vivado 对所选 PCIe block、器件许可或
主机插槽给出冲突，以实际工具结果为准，并把变更写回本文。

| 项目 | 第一版基线 |
|---|---|
| XDMA 功能模式 | DMA，AXI Memory Mapped，不使用 AXI-Stream |
| PCIe 代际/宽度 | **Gen3 x8**；物理使用 CN1 lane 0~7 |
| DMA 通道 | 1 H2C + 1 C2H |
| 主机控制接口 | 启用 32-bit AXI4-Lite Master |
| 中断 | XDMA 可保留 MSI；FSA 完成第一版采用轮询 |
| User BAR | 64 KiB 非预取窗口，PCIe-to-AXI-Lite translation 为 0 |
| `FSA_CTRL_BASE` | `0x0000_0000`，相对 `/dev/xdma0_user` 起点 |
| HBM 端口 | 先使用 `AXI_00`，启用全局寻址，功能通过后再拆分多端口 |
| HBM 数据域 | 100 MHz 正确性基线；后续再提高频率和带宽 |
| FSA 域 | 100 MHz，保持已验证时钟目标 |
| 数据调度 | H2C 完成后启动 FSA；FSA 完成后再 C2H，不并发 |

Gen3 x8 是本方案的正确性基线，不表示主机最终一定协商到 x8。XDMA 支持 AXI MM、
最多 4 条 H2C 和 4 条 C2H，Gen3 x8 也属于 `-2` UltraScale+ 支持范围；相关限制见
[PG195 Features](https://docs.amd.com/r/en-US/pg195-pcie-dma/Features) 和
[PG195 Minimum Device Requirements](https://docs.amd.com/r/en-US/pg195-pcie-dma/Minimum-Device-Requirements)。

## 6. Vivado Block Design 连接

### 6.1 数据通路

建议的第一版连接如下：

```text
xdma_0/M_AXI（XDMA user_clk 域）
        │
        └─ AXI Clock Converter ─┐
                                │
fsa_dma_top_0/m_axi_gmem ───────┼─ SmartConnect 2SI/1MI
        （100 MHz，64-bit AXI4） │   - 仲裁两个 master
                                │   - AXI4 → AXI3 协议转换
                                │   - 64/256-bit 数据宽度转换
                                ▼
                         hbm_0/AXI_00
                         （100 MHz，256-bit AXI3）
```

必须在 Elaborated Design 或接口属性中确认 SmartConnect 确实完成了协议和宽度转换；在
HBM 端用 ILA 确认 `AxSIZE=3'b101`、地址低 5 位为 0。FSA 产生的 8-byte 访问会由转换器
合并或转成带 `WSTRB` 的 32-byte HBM 访问，功能可行但带宽效率较低。

XDMA 和 FSA 访问的是同一 HBM 地址空间。第一版严格按“写 Q/K/V → 启动 FSA → 读 O”
串行执行，因此不需要两个 master 之间的软件 cache 一致性协议；但必须等每次 XDMA
系统调用返回后再启动 FSA，并等 `ap_done` 后再发起 C2H。

### 6.2 控制通路

```text
主机对 XDMA user BAR 读写
        ↓
xdma_0/M_AXI_LITE（XDMA user_clk 域）
        ↓ AXI Clock Converter
fsa_dma_top_0/s_axi_control（100 MHz）
```

User BAR 只承载低速控制寄存器，不承载 8 GB HBM 数据窗口。H2C/C2H 的卡内目标地址通过
XDMA DMA 描述符/设备文件偏移送到 XDMA 的 AXI MM master，与 User BAR 是两套地址通路。

### 6.3 时钟、复位和 HBM 初始化

至少区分三个概念：

1. PCIe 参考时钟和 `PERST#`：由 `PCIE_CLK0_P/N`、`PERST_PCIE_1V8(BF5)` 驱动 XDMA；
2. XDMA `user_clk/user_resetn` 域：服务 XDMA AXI master；
3. 100 MHz FSA/HBM AXI 域：服务 `fsa_dma_top`、SmartConnect 和 `hbm_0/AXI_00`。

HBM 还需要 `HBM_REF_CLK_x` 和 APB clock。候选来源是 NM37 的
`CLK_100_DDR_P/N(BH42/BJ42)` 经 Clocking Wizard/BUFG 产生的 100 MHz，但必须让 HBM IP
的 DRC 确认 GCIO、SLR 和抖动要求，不可仅凭固定向量板测可用就认定 HBM ref clock 合格。
HBM 参考时钟、APB clock 和 AXI port clock 的职责见
[PG276 Clocking](https://docs.amd.com/r/en-US/pg276-axi-hbm/Clocking)。

HBM 任何 AXI 访问之前，必须等待所有已启用 stack 的 `apb_complete_0/1=1`。把该状态接到
ILA，并用它保持 FSA/XDMA→HBM 数据通路处于禁止启动状态；不要在初始化完成前写 Q/K/V。
`apb_complete_*` 的含义见
[PG276 Non-AXI Ports](https://docs.amd.com/r/en-US/pg276-axi-hbm/Non-AXI-Ports)。

每个时钟域使用独立的 `proc_sys_reset` 或等价同步复位。外部复位可异步断言，但必须在
对应时钟域同步释放；不要用 `reset_n & locked` 或 `reset_n & apb_complete` 的组合 LUT
直接驱动异步复位脚。

### 6.4 建议 ILA 探针

- XDMA：`user_lnk_up`、`axi_aresetn`、H2C/C2H AXI 请求与 `BRESP/RRESP`；
- HBM：`apb_complete_0/1`、`DRAM_*_STAT_CATTRIP`、AXI_00 的 `AxADDR/AxLEN/AxSIZE`、响应；
- FSA 控制：对 `0x00` 的 AXI-Lite 写、`ap_done` 读回、`status_ap_vld/status`；
- FSA 数据：`m_axi_gmem` 首笔 AR/AW 地址、`RRESP/BRESP` 和超时状态；
- 系统级：当前阶段编号、首次错误码和超时计数器。

## 7. 地址规划

### 7.1 控制地址

第一版分配：

```text
/dev/xdma0_user + 0x0000_0000  -> fsa_dma_top/s_axi_control
映射范围                         -> 64 KiB
```

HLS 寄存器实际只使用 `0x00~0x54`，分配 64 KiB 是为了满足互连和 BAR 的简单粒度。这里的
`FSA_CTRL_BASE=0` 是用户设备节点 mmap 区域内的相对偏移，不是 Linux 虚拟地址。

### 7.2 HBM 数据地址

为同时容纳当前 4×4 IP 和未来允许的 128×4、`L<=4096` 配置，第一版固定预留：

```text
HBM_DATA_BASE = 0x0000_0000

Q = HBM_DATA_BASE + 0x0000_0000   # 1 MiB
K = HBM_DATA_BASE + 0x0010_0000   # 1 MiB
V = HBM_DATA_BASE + 0x0020_0000   # 1 MiB
O = HBM_DATA_BASE + 0x0030_0000   # 2 MiB
END(exclusive)       0x0050_0000
```

通用容量公式：

```text
Q_bytes = K_bytes = V_bytes = L * SA_ROWS * 2
O_bytes                         = L * SA_ROWS * 4

DMA_transfer_bytes = align_up(logical_bytes, 32)
```

`128×4096` 时单个 Q/K/V 正好 1 MiB，O 为 2 MiB，因此上述区间不会重叠。当前 4×4 IP
占用更少。主机为向上取整的尾部补 0；FSA 只访问逻辑矩阵范围。所有基地址都按 1 MiB
对齐，也满足 HBM 32-byte 边界要求。

Address Editor 中 XDMA 数据 master 与 FSA master 必须看到**同一个数值地址**。如果 HBM
segment 最终不是从 0 开始，则统一修改 `HBM_DATA_BASE`，不要只改主机或只改 FSA 寄存器。

## 8. Vivado 实施顺序与验收门

### 阶段 A：保留当前固定向量板测作为回归

当前 `fsa_dma_vivado` 包使用片上 AXI RAM，已由用户确认 `test_pass=1` 且 Implementation
时序通过。每次替换 HLS IP 后先重新跑该板测，要求：

```text
test_busy = 0
test_done = 1
test_pass = 1
test_fail = 0
```

并保存 `report_timing_summary`、`.bit` 和同一次 Implementation 的 `.ltx`。

### 阶段 B：HBM 独立初始化与读写

1. 创建 HBM Controller example design 或最小 Traffic Generator；
2. 只启用第一版需要的 AXI 端口，配置全局寻址；
3. 确认 `apb_complete_*` 拉高，Hardware Manager 的 HBM 状态为 Complete；
4. 用 HBM TG 写读固定地址，数据比较为 0 error；
5. 记录 HBM ref/APB/AXI clock、启用的 stack/MC/port 和实际地址范围。

HBM 独立测试失败时不要加入 XDMA 和 FSA。

### 阶段 C：XDMA-HBM 回环

1. 配置 XDMA Gen3 x8、AXI MM、1 H2C、1 C2H 和 AXI4-Lite Master；
2. 生成 XDMA example design/XDC，核对 block location、lane reversal、CLK0 和 `BF5`；
3. 连接 XDMA data master 到 HBM，暂不加入 FSA；
4. 完成 Synthesis、Implementation、DRC 和 bitstream；
5. Linux 侧向 `Q` 区写随机数据，再从同一地址读回；
6. 多组长度覆盖 32 B、4 KiB、非 4 KiB 整数倍和至少 1 MiB，逐字节比较。

只有回环完全一致，才能说明 XDMA、地址窗口、SmartConnect 和 HBM 数据通路正确。

### 阶段 D：加入 FSA

1. 加入 `fsa_dma_top_0`，控制口和数据口按第 6 节连接；
2. Address Editor 分配 `FSA_CTRL_BASE=0` 和统一 HBM segment；
3. 加入 HBM 初始化门控、各时钟域复位和 ILA；
4. Validate Design，生成 Output Products 和 HDL wrapper；
5. 使用 AXI VIP/AXI RAM 完成寄存器和 FSA 数据通路仿真；
6. Synthesis 后检查意外悬空、CDC 和宽度/协议转换；
7. Implementation 要求 Setup WNS、Hold WHS 均不小于 0，TNS/THS 为 0，无关键未约束路径；
8. 生成同一次实现的 `.bit/.ltx`，再进入主机测试。

### 阶段 E：端到端板上验证

按以下顺序逐级增加范围：

1. 读 `ap_idle`，确认 User BAR 控制通路可访问；
2. 重做 XDMA-HBM 回环，防止加入 FSA 后地址图变化；
3. 运行与 HLS testbench 相同的 `L=2×SA_COLS+1=9`、`causal=false` 固定样例；
4. 要求 `ap_done=1`、`status_ap_vld=1`、`status=0`，并比较完整 9×4 FP32 O；
5. 测试 `causal=true`；
6. 测试 `L=0` 和 `L=4097`，要求 `status=1` 且 O 不被写坏；
7. 测试连续多次 start、不同 L、超时恢复和重新枚举；
8. 最后才测大 L、吞吐和多 HBM 端口优化。

## 9. Linux 主机实施

### 9.1 驱动和设备节点

使用 AMD/Xilinx 的
[dma_ip_drivers](https://github.com/Xilinx/dma_ip_drivers/tree/master/XDMA/linux-kernel)，
记录仓库 commit，不要只写“最新版”。主机先安装与 `uname -r` 一致的 kernel headers，
然后在 `XDMA/linux-kernel/xdma` 编译安装，在 `tools` 编译测试工具。典型流程为：

```bash
uname -a
git rev-parse HEAD

cd dma_ip_drivers/XDMA/linux-kernel/xdma
make
sudo make install
sudo depmod -a
sudo modprobe xdma

cd ../tools
make

lspci -nn | grep -i xilinx
ls -l /dev/xdma* /dev/xdma/card* 2>/dev/null
modinfo xdma
dmesg | tail -n 100
```

预期至少出现 user、H2C0、C2H0 设备；具体名称可能是传统
`/dev/xdma0_user`、`/dev/xdma0_h2c_0`、`/dev/xdma0_c2h_0`，也可能同时出现
`/dev/xdma/card0/...` 别名，软件启动时应枚举并打印实际路径。

### 9.2 链路确认

```bash
sudo lspci -s <BDF> -vv
```

保存以下字段：Vendor/Device ID、BAR、MSI/MSI-X、`LnkCap`、`LnkSta`、AER 和 BDF。
通过标准至少为链路稳定、无持续 AER 错误，且 `LnkSta` 显示预期 Gen3 x8；如果只训练到
x4/x1，先检查主机插槽、电源、lane mapping、参考时钟和 PERST，不要直接进入性能测试。

### 9.3 XDMA-HBM 回环命令

在 `tools` 目录使用：

```bash
./dma_to_device \
  --device /dev/xdma0_h2c_0 \
  --address 0x00000000 \
  --size <bytes> \
  -f infile.bin

./dma_from_device \
  --device /dev/xdma0_c2h_0 \
  --address 0x00000000 \
  --size <bytes> \
  --file outfile.bin

cmp infile.bin outfile.bin
```

实际工具的文件参数以当前 commit 的 `--help` 为准。每次记录地址、长度、返回值和比较结果。

### 9.4 FSA 主机调用

`xfsa_dma_top` 目录中的驱动是 HLS 生成的 bare-metal 风格驱动，Linux PC 端不直接链接它；
PC 程序可以复用其中的寄存器常量，通过 `/dev/xdma0_user` 的 `mmap` 或 `pread/pwrite`
访问寄存器。

```cpp
bool sa_calc(...){
    const uint64_t q_card = HBM_DATA_BASE + 0x00000000ULL;
    const uint64_t k_card = HBM_DATA_BASE + 0x00100000ULL;
    const uint64_t v_card = HBM_DATA_BASE + 0x00200000ULL;
    const uint64_t o_card = HBM_DATA_BASE + 0x00300000ULL;

    xdma_h2c_write(q_card, q_padded, align_up(q_bytes, 32));
    xdma_h2c_write(k_card, k_padded, align_up(k_bytes, 32));
    xdma_h2c_write(v_card, v_padded, align_up(v_bytes, 32));

    fsa_write64(0x10, q_card);
    fsa_write64(0x1c, k_card);
    fsa_write64(0x28, v_card);
    fsa_write64(0x34, o_card);
    fsa_write32(0x40, L);
    fsa_write32(0x48, causal ? 1 : 0);
    fsa_write32(0x00, 1);

    if(!wait_ap_done_with_timeout()) return false;
    if((fsa_read32(0x54) & 1U) == 0U) return false;
    if((fsa_read32(0x50) & 0xffU) != 0U) return false;

    xdma_c2h_read(o_card, o_padded, align_up(o_bytes, 32));
    return compare_with_independent_cpu_golden(o, L, SA_ROWS, causal);
}
```

每次调用前确认 `ap_idle=1`。轮询必须有超时；超时后记录 AP_CTRL、HBM complete、XDMA
link、AXI `RRESP/BRESP` 和 ILA，不要立即重复写 `ap_start`。

### 9.5 bitstream 更新与 PCIe 重新枚举

JTAG 重新配置 VU37P 会使 PCIe endpoint 消失。不要硬编码其他机器的 BDF。推荐先记录本机
BDF，卸载驱动并 remove，编程后 rescan：

```bash
sudo modprobe -r xdma
echo 1 | sudo tee /sys/bus/pci/devices/<BDF>/remove

# 此处由 Hardware Manager 下载新的 .bit/.ltx

echo 1 | sudo tee /sys/bus/pci/rescan
sudo modprobe xdma
lspci -s <BDF> -vv
```

若 rescan 后设备不出现，执行主机 cold reboot 或整机断电重启，并把 NM37 的实际恢复顺序
记录为板卡操作规程。不要在驱动仍占用设备节点时反复下载 bitstream。

## 10. 尚未闭环的 TODO 与用户操作

| TODO | 你需要怎么做 | 回填到本文的证据 |
|---|---|---|
| 保存固定向量板测时序证据 | 在已通过的工程执行 `report_timing_summary -file fsa_dma_board_timing_summary.rpt`，保存 run 状态和 bit/ltx 路径 | WNS、WHS、TNS、THS、failing endpoints、Vivado run 名称 |
| 确认 XDMA block location 和 lane mapping | 在 Vivado 2024.2 按 Gen3 x8 生成 XDMA example design，核对 location-specific XDC 与原理图第 7 页 | PCIe block、GT Bank/channel、lane reversal、CLK0、PERST pin |
| 确认主机实际链路 | 插入目标 Linux 主机，运行 `lspci -s <BDF> -vv` | BDF、`LnkCap`、`LnkSta`、AER |
| 确认 HBM 时钟/端口配置 | 生成 HBM example design，运行 DRC 和 HBM TG；观察 `apb_complete_*` | ref/APB/AXI clock、启用 stack/MC/AXI port、HBM TG 结果 |
| 固化 Vivado 地址图 | 完成 Block Design 后导出 Address Editor 截图或 Tcl `assign_bd_address` 结果 | User BAR、FSA control segment、XDMA/FSA 看到的 HBM segment |
| 确认 AXI 转换 | 打开 Elaborated Design，检查 SmartConnect 自动转换；在 HBM 端加 ILA | HBM 端 256-bit AXI3、`AxSIZE=5`、32-byte 对齐、无 DECERR/SLVERR |
| 记录 Linux 驱动版本 | 在目标机执行 `uname -a`、`git rev-parse HEAD`、`modinfo xdma` | 内核、驱动 commit、模块版本、设备节点 |
| 确认 bitstream 后恢复流程 | 依次测试 remove/rescan、hot reset（若平台支持）、cold reboot | 本机可重复的命令、BDF 是否变化、成功次数 |
| 完成 XDMA-HBM 回环 | 用 `dma_to_device/from_device` 覆盖多种长度，逐字节比较 | 命令、地址、长度、`cmp` 结果、吞吐 |
| 完成 FSA 端到端金标准 | 用当前 IP 的 4×4、L=9 样例运行，读回完整 O 与独立 CPU softmax 比较 | AP_CTRL、status、O 误差、ILA 首笔地址和响应 |

以上 TODO 需要 Vivado 工程、目标 Linux 主机或真实板卡状态，无法仅从当前仓库文件完成。
其中最先应该做的是：**保存现有板测时序报告 → 生成 HBM example design 并跑 TG →
生成 XDMA Gen3 x8 example design 并核对第 7 页管脚 → 做 XDMA-HBM 回环**。

## 11. 结论分级

| 证据 | 可以得出的结论 |
|---|---|
| HLS C simulation PASS | 当前 C++ 功能模型在当前样例正确 |
| HLS C synthesis完成 | 当前源码可生成 HLS RTL |
| RTL co-simulation PASS | HLS RTL与当前 C testbench 在单事务中一致 |
| IP目录完整 | Vivado 可注册当前 `fsa_dma_top:1.0` |
| 固定向量板测 PASS | 当前 IP、AXI RAM、板测控制器在 NM37 上通过固定样例 |
| 固定向量工程时序通过 | 该片上 RAM 板测工程满足当前实现约束 |
| HBM TG PASS | HBM 初始化和独立存储器通路可用 |
| XDMA-HBM回环 PASS | 主机、PCIe、XDMA、互连和 HBM 数据搬运正确 |
| FSA端到端金标准 PASS | 当前固定样例在 XDMA + HBM + FSA 完整链路上正确 |

当前已到“固定向量 IP 板测和该工程时序通过”这一层；尚不能声称 XDMA、HBM 或 Linux
端到端已经通过。所有后续结论都应附上对应日志、报告、命令和读回数据。
