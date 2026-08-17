# NM37 VU37P板级配置

## 已确认硬件

| 项目 | 配置 |
|---|---|
| FPGA | XCVU37P-FSVH2892 |
| Vivado Part | `xcvu37p_CIV-fsvh2892-2-e` |
| 板载时钟 | `CLK_100_DDR_P/N`，100 MHz差分时钟 |
| 时钟 P | `BH42`，`IO_L13P..._65` |
| 时钟 N | `BJ42`，`IO_L13N..._65` |
| 时钟 IOSTANDARD | `DIFF_SSTL12` |
| 复位 | `SLOT_CPU_RESET`，`BF2`，SW2按下为低 |
| 复位 IOSTANDARD | `LVCMOS18` |
| 调试 | FT2232 JTAG、Hardware Manager、VIO、ILA |

不要使用旧笔记中的 `BM43/BM42`。它们不是本板载时钟的差分对。

## XDC

```tcl
## NM37板载100 MHz差分时钟
set_property PACKAGE_PIN BH42 [get_ports board_clk_p]
set_property PACKAGE_PIN BJ42 [get_ports board_clk_n]
set_property IOSTANDARD DIFF_SSTL12 [get_ports {board_clk_p board_clk_n}]

## NM37板载SW2，低有效
set_property PACKAGE_PIN BF2 [get_ports reset_n]
set_property IOSTANDARD LVCMOS18 [get_ports reset_n]

## reset_n是异步板级控制，不作为普通同步数据输入分析
set_false_path -from [get_ports reset_n]
```

Clocking Wizard会为输入时钟生成约束。不要在用户 XDC中重复添加 `create_clock`，否则会出现 clock override警告。

## Clocking Wizard

- 输入源：Differential clock capable pin。
- 输入频率：100.000 MHz。
- 输出 `clk_out1`：100.000 MHz。
- 启用 `locked`。
- 启用高有效 `reset`。
- 不通过修改时钟周期来掩盖设计时序违例。

## 复位结构

使用外部复位直接异步清零复位移位寄存器，并在 Clocking Wizard锁定后同步释放：

```verilog
(* ASYNC_REG = "TRUE" *) reg [7:0] reset_shift = 8'b0;

always @(posedge clk_100m or negedge reset_n) begin
    if (!reset_n)
        reset_shift <= 8'b0;
    else if (!clk_locked)
        reset_shift <= 8'b0;
    else
        reset_shift <= {reset_shift[6:0], 1'b1};
end

wire rst_100m = ~reset_shift[7];
```

不要写成下面这样并让它直接驱动异步复位脚：

```verilog
wire reset_n_bad = reset_n & clk_locked;
```

这种组合逻辑可能触发 `LUTAR-1`并带来复位毛刺风险。

## 板级边界

- `DS2/DS3`用于 FPGA配置状态，不作为普通用户 PASS/FAIL LED。
- 固定向量测试优先通过 VIO启动和读取状态，通过 ILA查看波形。
- 若 Vivado报 `Common 17-345`，先处理 XCVU37P CIV器件综合许可证；不要换器件规避。
- `.bit`和 `.ltx`必须来自同一次 Implementation。

