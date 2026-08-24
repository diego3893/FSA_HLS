# FSA 主机 PC/XDMA/HBM 实施方案

## 1. 已确定的系统路线

本项目固定采用下面的第一版上板路线：

```text
Linux 主机 PC
├── XDMA H2C：把 Q/K/V 写入板卡 HBM
├── XDMA AXI4-Lite Master：配置并启动 fsa_dma_top
└── XDMA C2H：从板卡 HBM 读回 O
              │ PCIe
              ▼
NM37 / XCVU37P
├── XDMA IP
├── AXI 互连与必要的时钟域转换
├── HBM Controller
└── fsa_dma_top
    ├── s_axi_control：地址、L、causal、start/done/status
    └── m_axi_gmem：主动从 HBM 读取 Q/K/V 并写回 O
```

第一版不使用板上 `ZU5EV` 的 ARM，也不在 VU37P 中加入 MicroBlaze。主机 PC 是唯一的
软件执行端；VU37P 只运行 XDMA、HBM、AXI 互连和 FSA 硬件逻辑。

这里的“DMA”有两层，不能混淆：

- **XDMA H2C/C2H**：负责主机内存与板卡 HBM 之间的数据搬运；
- **`fsa_dma_top` 的 `m_axi_gmem`**：负责板卡内部 HBM 与 FSA Core 之间的数据搬运。

`fsa_dma_top` 是当前对外的 HLS IP 顶层。主机不直接逐条调用内部 `fsa_core`，而是一次
设置完整任务并写 `ap_start`，由 `fsa_dma_top` 内部完成所有 query/KV tile 循环。

## 2. 已确认项与 TODO

| 项目 | 当前决定或状态 |
|---|---|
| 开发板/计算 FPGA | NM37 / `XCVU37P-FSVH2892` |
| Vivado Part | `xcvu37p_CIV-fsvh2892-2-e` |
| HLS/Vivado 主版本 | 2024.2 |
| HLS 顶层 | `fsa_dma_top` |
| 控制协议 | `ap_ctrl_hs`，32-bit AXI4-Lite |
| 数据接口 | 单个 64-bit AXI4 master `gmem` |
| HLS 时钟目标 | 100 MHz，不通过放宽时钟掩盖违例 |
| 软件执行端 | Linux 主机 PC |
| 主机控制通路 | PCIe XDMA AXI4-Lite Master |
| 主机数据通路 | XDMA H2C/C2H ↔ HBM |
| 完成通知 | 第一版轮询 `ap_done`；中断作为后续扩展 |
| PCIe物理连接 | CN1 PCIe x16，lane 0~15直连VU37P GTY Bank 224~227 |
| XDMA实际lane数/代际 | **TODO：结合XDMA许可、Vivado配置和主机插槽确定；原理图只能证明物理x16布线** |
| XDMA H2C/C2H 通道数 | **TODO：第一版至少各 1 条，最终配置待 Vivado 工程确认** |
| XDMA BAR 大小和基地址 | **TODO：在 Vivado Address Editor 中确定并写回本文** |
| FSA 控制基地址 | **TODO：分配 `FSA_CTRL_BASE`** |
| HBM 卡内地址范围 | **TODO：根据 HBM IP 配置和 XDMA 地址空间确定** |
| HBM AXI 端口/伪通道 | **TODO：先选一个端口完成功能闭环，再评估多端口带宽** |
| XDMA/HBM/FSA 时钟关系 | **TODO：根据 IP 输出时钟决定 AXI Clock Converter 数量** |
| NM37 PCIe/HBM XDC | **TODO：必须来自板卡原理图或厂商约束，不复用 SA 固定测试 XDC** |
| Linux XDMA 驱动版本 | **TODO：记录主机内核、驱动提交版本和设备节点** |

任何 `TODO` 在确认前都不能被当成已实现或已验证参数。

## 3. NM37原理图已经确认的PCIe连接

以下结论来自 `docs/NM37_SCH_220331.pdf` 的VU37P Bank 224~227、PCIe插槽CN1、
参考时钟缓冲和复位电平转换页面：

- CN1是PCIe x16边缘连接器，lane 0~15全部连接到XCVU37P；
- lane 0~3连接GTY Bank 227，lane 4~7连接Bank 226，lane 8~11连接Bank 225，
  lane 12~15连接Bank 224；每个Bank内部的lane编号与GTY channel按反序连接；
- CN1的`REFCLK+/-`形成`PCIE_REFCLK_DP/DN`，经U12时钟缓冲后生成
  `PCIE_CLK0_P/N`和`PCIE_CLK1_P/N`；
- `PCIE_CLK0_P/N`进入Bank 225的`MGTREFCLK0P/N`，管脚为`AR15/AR14`；
- `PCIE_CLK1_P/N`进入Bank 227的`MGTREFCLK0P/N`，管脚为`AL15/AL14`；
- CN1的`PERST#`经R41和U13电平转换成为`PERST_PCIE_1V8`，进入VU37P管脚`BF5`；
- VU37P封装内含HBM，板级设计通过Vivado HBM Controller IP访问，不存在需要用户XDC
  约束的数据DQ/DQS引脚；板上外部PL DDR4不是本路线的数据存储器。

原理图没有规定XDMA IP参数，也不能证明主机插槽能力和最终链路协商结果。因此仍需：

- **TODO：**决定第一版XDMA使用x4、x8还是x16；优先采用Vivado/XDMA许可证和主机均
  支持、且容易时序收敛的最小可用配置；
- **TODO：**决定PCIe Gen3或Gen4，并用Hardware Manager、`lspci -vv`和XDMA日志确认
  实际协商的lane宽度与速率；
- **TODO：**依据最终XDMA核生成的端口和约束核对GT channel、参考时钟选择及管脚，
  不手写未经Vivado校验的整套PCIe XDC。

## 4. 两条硬件通路

### 4.1 控制通路

```text
主机对 XDMA user BAR 的读写
        ↓
XDMA AXI4-Lite Master
        ↓
AXI Interconnect / Clock Converter
        ↓
fsa_dma_top/s_axi_control
```

主机通过这条通路设置 Q/K/V/O 四个板卡 AXI 地址、`sequence_length`、`causal`，并读写
`ap_start/ap_done/status`。控制寄存器中保存的是板卡 HBM 地址，不是主机虚拟地址，也不是
主机 DRAM 物理地址。

### 4.2 数据通路

```text
                  ┌─ XDMA H2C/C2H AXI Master ─┐
主机内存 ↔ PCIe ─┤                           ├─ AXI互连 ─ HBM
                  └─ fsa_dma_top/m_axi_gmem ──┘
```

XDMA 和 `fsa_dma_top` 都是 HBM 的 AXI master，必须通过经过验证的 AXI 互连访问同一
卡内地址空间。第一版按顺序执行“写入Q/K/V → 启动FSA → 读回O”，不要求主机搬运和FSA
计算同时进行。

HBM数据格式、容量公式和算法调用语义见 `docs/fsa_dma软件调用方案.md`。

## 5. 当前已知 HLS 寄存器

当前生成驱动头文件中记录的相对偏移如下。最终必须用本次完整导出 IP 中的
`component.xml` 和 `xfsa_dma_top_hw.h` 再核对一次。

| 相对偏移 | 含义 |
|---:|---|
| `0x00` | `ap_start/ap_done/ap_idle/ap_ready/auto_restart/interrupt` |
| `0x04` | Global Interrupt Enable |
| `0x08` | IP Interrupt Enable |
| `0x0c` | IP Interrupt Status |
| `0x10`, `0x14` | Q 地址低/高 32 bit |
| `0x1c`, `0x20` | K 地址低/高 32 bit |
| `0x28`, `0x2c` | V 地址低/高 32 bit |
| `0x34`, `0x38` | O 地址低/高 32 bit |
| `0x40` | `sequence_length` |
| `0x48` | `causal` bit 0 |
| `0x50` | `status` bit `[7:0]` |
| `0x54` | `status_ap_vld` bit 0 |

**TODO：**取回完整 IP 后确认 VLNV、端口物理名、地址宽度、全部偏移和 clear-on-read
行为。若不一致，以实际导出物为准并更新本文。

## 6. 建议的卡内内存分配

第一版建议使用静态卡内地址，便于调试：

```text
HBM_BASE + TODO_Q_OFFSET  -> Q
HBM_BASE + TODO_K_OFFSET  -> K
HBM_BASE + TODO_V_OFFSET  -> V
HBM_BASE + TODO_O_OFFSET  -> O
```

**TODO：**确认 HBM 基地址、总容量、XDMA 对齐要求、AXI burst 边界和最大 `L` 后填写
offset。四段区域必须互不重叠，O 不得覆盖仍在读取的 Q/K/V。

## 7. IP 导出结束后的操作

### 7.1 验证并保存完整 HLS IP

导出成功至少应存在：

```text
<hls-build>/solution1/impl/ip/component.xml
<hls-build>/solution1/impl/ip/hdl/
<hls-build>/solution1/impl/ip/xgui/
```

在 HLS 服务器执行：

```bash
test -f <hls-build>/solution1/impl/ip/component.xml
find <hls-build>/solution1/impl/ip -maxdepth 2 -type f | sort | head -n 50
```

保存同一构建版本的：

- 完整 `impl/ip/`；
- `impl/export.zip`（若工具生成）；
- `csynth.rpt`、C simulation和RTL co-simulation日志；
- `xfsa_dma_top_hw.h` 等生成驱动；
- 对应HLS源码版本、`SA_ROWS/SA_COLS/MAX_SEQUENCE_LENGTH`和工具版本。

如果只有 `impl/vhdl`、`impl/verilog` 或 `impl/misc/drivers`，但没有 `component.xml`，不能
认为可注册的 Vivado IP 已完整取回。

### 7.2 检查实际导出接口

以生成 RTL 为端口名和位宽的最终依据，以 C++ 顶层和 testbench 为语义依据。记录：

- VLNV、顶层模块名和目标 Part；
- `ap_clk`、复位端口及极性；
- `s_axi_control` 地址宽度和全部寄存器；
- `m_axi_gmem` 的地址、数据、ID和burst信号；
- `interrupt` 是否存在；
- IP 使用的 Vitis HLS/Vivado 版本。

### 7.3 创建 Vivado 2024.2 板级工程

1. 选择 `xcvu37p_CIV-fsvh2892-2-e`；
2. 把完整 `impl/ip` 加入 IP Repository，并执行 `update_ip_catalog`；
3. 新建 Block Design，加入 `fsa_dma_top`、XDMA、HBM Controller 和 AXI 互连；
4. XDMA AXI4-Lite Master 连接 `fsa_dma_top/s_axi_control`；
5. XDMA H2C/C2H 数据主口连接 HBM；
6. `fsa_dma_top/m_axi_gmem` 也连接同一 HBM 地址空间；
7. 在 Address Editor 中分配 FSA 控制 BAR 和 HBM 地址窗口；
8. 根据各IP时钟加入AXI Clock Converter和复位同步逻辑；
9. 加入ILA，至少观察FSA启动/完成、`status`和`m_axi_gmem`错误响应；
10. Validate Design，生成 Output Products 和 HDL wrapper。

必须等待资料或实际工程确认：

- 已确认CN1物理x16布线、Bank 224~227、两组参考时钟和`BF5`复位；
- **TODO：**XDMA实际lane数、PCIe generation、GT channel选择和主机协商能力；
- **TODO：**XDMA AXI-Lite BAR 大小、基地址和 HBM window；
- **TODO：**HBM APB/参考时钟、AXI端口、地址映射和初始化完成信号；
- **TODO：**FSA 100 MHz域与XDMA/HBM域之间的CDC结构；
- **TODO：**最终 XDC。SA 固定测试确认的 `BH42/BJ42` 时钟与 `BF2` 复位不能替代
  PCIe/HBM约束。

### 7.4 先仿真，再生成 bitstream

按下面的验收门推进，前一阶段失败时不进入下一阶段：

1. **AXI-Lite寄存器仿真**：能写地址/L/causal并产生一次start；
2. **AXI RAM或HBM替代模型仿真**：预装Q/K/V后能写出O，且`status=0`；
3. **Vivado Synthesis**：无阻断错误；
4. **Implementation**：Setup WNS和Hold WHS均不小于0，TNS/THS为0，无关键未约束路径；
5. **Generate Bitstream**：保存同一次实现的`.bit`和调试`.ltx`；
6. **Program Device**：Hardware Manager正确识别XCVU37P并完成下载。

HLS Estimated Clock 不能替代 Vivado Implementation 的板级时序结论。

### 7.5 配置主机并做分层板上验证

1. 安装并加载与主机内核匹配的 Xilinx XDMA 驱动；
2. 按实际 PCIe BDF 对设备执行 remove/rescan；
3. 确认 user、H2C、C2H 设备节点出现；
4. 先读取一个只读版本/配置寄存器；
   - **TODO：**当前IP没有独立版本寄存器；决定由板级wrapper增加，或先读取`ap_idle`；
5. 只测试 XDMA H2C→HBM→C2H 回环，确认地址窗口和数据一致；
6. 使用 HLS testbench 的确定性 `L=2×SA_COLS+1` 样例运行FSA；
7. 要求 `ap_done=1`、`status=0`，并逐元素比较完整O；
8. 再测试非法L、causal和非tile整倍数；
9. 最后测试大L、连续多次调用、超时恢复和性能。

**TODO：**确认 NM37 下载 bitstream 后是否必须执行PCIe hot reset、整机重启或固定的
remove/rescan顺序，并记录准确命令；不能直接照搬其他板卡的BDF。

## 8. 第一版主机调用伪代码

```cpp
bool sa_calc(...){
    const uint64_t q_card = HBM_BASE + TODO_Q_OFFSET;
    const uint64_t k_card = HBM_BASE + TODO_K_OFFSET;
    const uint64_t v_card = HBM_BASE + TODO_V_OFFSET;
    const uint64_t o_card = HBM_BASE + TODO_O_OFFSET;

    xdma_h2c_write(q_card, q, q_bytes);
    xdma_h2c_write(k_card, k, k_bytes);
    xdma_h2c_write(v_card, v, v_bytes);

    fsa_write64(FSA_CTRL_BASE + 0x10, q_card);
    fsa_write64(FSA_CTRL_BASE + 0x1c, k_card);
    fsa_write64(FSA_CTRL_BASE + 0x28, v_card);
    fsa_write64(FSA_CTRL_BASE + 0x34, o_card);
    fsa_write32(FSA_CTRL_BASE + 0x40, L);
    fsa_write32(FSA_CTRL_BASE + 0x48, causal ? 1 : 0);
    fsa_write32(FSA_CTRL_BASE + 0x00, 1);

    if(!wait_ap_done_with_timeout()) return false;
    if(fsa_read32(FSA_CTRL_BASE + 0x50) != 0) return false;

    xdma_c2h_read(o_card, o, o_bytes);
    return true;
}
```

**TODO：**确定实际 `/dev/xdma*` 名称、BAR映射方法、udev权限、最大传输长度、对齐要求
和错误恢复流程。第一版使用带超时的轮询，不启用`auto_restart`。

## 9. 验证结论分级

| 阶段 | 可证明内容 |
|---|---|
| HLS C simulation PASS | C++功能模型在当前样例正确 |
| HLS C synthesis完成 | 可生成当前HLS RTL |
| RTL co-simulation PASS | HLS RTL与C testbench在当前样例一致 |
| IP目录完整 | Vivado注册该版本IP所需文件存在 |
| Vivado Synthesis完成 | 板级工程可综合 |
| Implementation时序通过 | 当前板级连接满足实际约束 |
| XDMA-HBM回环通过 | 主机与卡内HBM数据路径正确 |
| FSA板上金标准通过 | 当前固定样例在目标硬件上正确 |

本文固定的是架构与实施顺序，不表示 XDMA、HBM、Vivado实现、bitstream或真实板上运行
已经通过。每完成一项，必须用对应日志、报告或读回数据更新状态。
