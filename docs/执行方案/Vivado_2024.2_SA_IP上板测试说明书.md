# Vivado 2024.2 中 SA IP 的 FPGA 上板测试说明书

## 1. 文档目的

本文说明如何把 Vitis HLS 2024.2 导出的 `systolic_array_top` IP 加入 Vivado 2024.2 工程，生成 bitstream，通过 JTAG 下载到 FPGA，并使用 VIO 和 ILA 完成一次可重复的板上功能测试。

本文不是 DMA、HBM 或完整 FSA 系统的上板方案。本次先验证以下内容：

1. Vivado 能正确识别并例化 SA IP；
2. SA IP 的时钟、复位和 `ap_ctrl_hs` 握手能正常工作；
3. FPGA 能执行与 `tests/hls/test_systolic_array_top.cpp` 相同的 4×4 Attention Score 测试；
4. SA 能完成 `S = Q × K^T`；
5. CMP 能输出每一行的 rowmax；
6. 板上自动比较结果，并给出 `test_pass` 或 `test_fail`；
7. 可以用 ILA 查看 `input_r`、`output_r`、`ap_start`、`ap_ready` 和 `ap_done`。

本方案使用“固定测试控制器 + VIO 启动 + ILA 观察”的方式。它不需要 DMA、DDR/HBM、MicroBlaze、PCIe 驱动或 Python，适合第一次验证 SA IP。

---

## 2. 开始之前必须确认的事情

### 2.1 NM37 开发板已确认的硬件信息

本节信息来自 `NM37_SCH_220331.pdf`（2022-03-31）原理图。板上同时有 `XCVU37P` 和 `ZU5EV` 两颗 FPGA；本次 SA IP 上板的目标是 `XCVU37P`，不是 `ZU5EV`。

| 项目 | NM37 上的实际配置 |
|---|---|
| 目标 FPGA | `XCVU37P-FSVH2892` |
| Vivado Part | `xcvu37p_CIV-fsvh2892-2-e` |
| 板载时钟 | `CLK_100_DDR_P/N`，100 MHz 差分时钟 |
| 时钟 P 管脚 | `BM43` |
| 时钟 N 管脚 | `BM42` |
| 时钟所在 Bank | Bank 65，VCCO = 1.2 V |
| 推荐时钟 IOSTANDARD | `DIFF_SSTL12` |
| 板载复位 | `SLOT_CPU_RESET`，管脚 `BF2` |
| 复位电平 | 平时为高，按下 `SW2` 为低，即低有效 |
| 复位 IOSTANDARD | `LVCMOS18` |
| 下载与调试 | 板载 FT2232 JTAG，可用 Hardware Manager、VIO 和 ILA |

当前 SA HLS 工程的目标器件是：

```text
xcvu37p_CIV-fsvh2892-2-e
```

开发板与 HLS 工程使用同一个 Part，不需要更换器件。如果 Vivado 仍报 `Common 17-345`，这表示缺少支持 `xcvu37p_CIV` 的 Synthesis License，与 SA 代码和引脚无关；必须先修复许可证才能继续综合。

### 2.2 引脚使用边界

本文后面的 XDC 已根据 NM37 原理图填写，不再使用占位引脚。如果实验室有确认可用的 NM37 参考 XDC，应再对照其 Bank 配置和时钟缓冲写法；参考 XDC 与本文不一致时，以实验室已验证的 XDC 为准，不要同时保留两组冲突约束。

### 2.3 确认 IP 与源码是同一次构建

当前导出的 IP 压缩包通常位于：

```text
build/sa_build/solution1/impl/ip/xilinx_com_hls_systolic_array_top_1_0.zip
```

未压缩的 IP 仓库目录是：

```text
build/sa_build/solution1/impl/ip/
```

该目录中应存在：

```text
component.xml
hdl/
constraints/
xgui/
```

如果修改了 `types.hpp`、`control.hpp`、`systolic_array_top.hpp` 或顶层接口，必须重新运行 HLS 综合、协同仿真和 `export_design`，然后在 Vivado 中更新 IP。旧 IP 的端口位宽和位排列可能已经失效。

---

## 3. 本次板上测试的结构

下图中的“板上时钟”特指 NM37 的 `CLK_100_DDR_P/N`，即 `BM43/BM42` 上的 100 MHz 差分时钟。

```text
                ┌──────────────────────── Vivado 工程 ────────────────────────┐
                │                                                              │
板上时钟 ──────►│ Clocking Wizard ── 100 MHz ───────────────┐                 │
                │                                            │                 │
                │      VIO                                   ▼                 │
                │  run_test ──► SA 测试控制器 ── input_r ─► SA HLS IP         │
                │                │             ap_start      │                 │
                │                │◄──────── ap_ready/done ───┤                 │
                │                │◄──────── output_r ────────┘                 │
                │                │                                             │
                │                ├── test_pass/test_fail ─► VIO               │
                │                └── 所有关键信号 ─────────► ILA               │
                └──────────────────────────────────────────────────────────────┘
                                         │
                                         ▼
                                  JTAG / Hardware Manager
```

各部分职责如下：

| 部分 | 作用 |
|---|---|
| Clocking Wizard | 接收 `BM43/BM42` 上的 100 MHz 差分时钟，输出 SA 使用的 100 MHz 时钟 |
| SA HLS IP | 真正被测试的 4×4 SA |
| `sa_test_controller` | 自动生成 Q、K 和控制信号，执行测试并比较结果 |
| VIO | 在 Hardware Manager 中手动启动测试，并查看 pass/fail |
| ILA | 捕获握手、输入、输出和当前事务编号 |

控制器采用保守的串行发送方式：上一笔 SA 事务完成后才发送下一笔。这样最容易先验证功能，不会因为顶层当前的 II 而覆盖输入。它不是最终性能测试控制器。

NM37 原理图中的 `DS2` 和 `DS3` 分别用于 FPGA `INIT_B` 和 `DONE` 状态，不是普通用户 LED。本测试不把 `test_pass/test_fail` 连到这两颗 LED，而是通过 VIO 和 ILA 查看。

---

## 4. 当前 SA IP 的接口说明

当前导出的 RTL 顶层端口为：

```verilog
module systolic_array_top (
    input  wire         ap_clk,
    input  wire         ap_rst,
    input  wire         ap_start,
    output wire         ap_done,
    output wire         ap_idle,
    output wire         ap_ready,
    input  wire [121:0] input_r,
    output wire [131:0] output_r
);
```

### 4.1 `ap_ctrl_hs` 控制信号

| 信号 | 方向 | 含义 |
|---|---|---|
| `ap_clk` | 输入 | SA 的工作时钟，本方案使用 100 MHz |
| `ap_rst` | 输入 | HLS 顶层控制和内部寄存器复位，高电平有效，在 `ap_clk` 上采样 |
| `ap_start` | 输入 | 请求 SA 执行一次顶层事务 |
| `ap_ready` | 输出 | SA 已接受当前请求，并能够接收下一次请求 |
| `ap_done` | 输出 | 当前事务结果已经完成 |
| `ap_idle` | 输出 | SA 当前空闲 |

本方案的控制器遵守以下规则：

1. 先把 `input_r` 设置好；
2. 拉高 `ap_start`；
3. 在 `ap_ready` 出现之前，保持 `ap_start` 和 `input_r` 不变；
4. `ap_ready` 出现后拉低 `ap_start`；
5. 在 `ap_done` 出现时读取并锁存 `output_r`；
6. 然后准备下一笔事务。

不要把 `ap_start` 随意只拉高一个时钟周期。若该周期 SA 没有接受输入，该笔数据会丢失。

### 4.2 两种复位不是一回事

当前设计有两层复位：

| 复位 | 位置 | 作用 |
|---|---|---|
| `ap_rst` | HLS IP 独立端口 | 复位 HLS 的控制状态机和 RTL 寄存器 |
| `input_r[0]` | `SystolicArrayInput.reset` | 作为一次正常事务，调用 `reset_systolic_array_state()`，复位 PE、CMP 和各级 pipe 的逻辑状态 |

正确启动顺序是：

1. 先让 `ap_rst=1` 保持若干个 100 MHz 时钟；
2. 释放 `ap_rst`；
3. 再发送一笔 `input_r[0]=1` 的正常 HLS 事务；
4. 等待该事务 `ap_done`；
5. 才开始装载 Q 和计算。

只做其中一种复位不够稳妥。

### 4.3 `input_r[121:0]` 的位排列

当前 IP 使用 `AGGREGATE compact=bit`，输入结构体被压成一个 122 位端口：

| 位范围 | C++ 字段 | 说明 |
|---|---|---|
| `[0]` | `input.reset` | SA 逻辑复位事务 |
| `[10:1]` | `input.pe_ctrl[0]` | 第 0 行 PE 控制字 |
| `[20:11]` | `input.pe_ctrl[1]` | 第 1 行 PE 控制字 |
| `[30:21]` | `input.pe_ctrl[2]` | 第 2 行 PE 控制字 |
| `[40:31]` | `input.pe_ctrl[3]` | 第 3 行 PE 控制字 |
| `[41]` | `input.cmp_ctrl.valid` | CMP 控制有效 |
| `[49:42]` | `input.cmp_ctrl.bits.cmd` | CMP 命令，当前为 8 位枚举 |
| `[57:50]` | `input.cmp_ctrl.bits.causalCounter` | causal mask 计数 |
| `[73:58]` | `input.pe_data[0]` | 第 0 行 FP16 数据位模式 |
| `[89:74]` | `input.pe_data[1]` | 第 1 行 FP16 数据位模式 |
| `[105:90]` | `input.pe_data[2]` | 第 2 行 FP16 数据位模式 |
| `[121:106]` | `input.pe_data[3]` | 第 3 行 FP16 数据位模式 |

每个 10 位 PE 控制字内部排列如下：

| 控制字位 | 字段 |
|---|---|
| `[0]` | `valid` |
| `[1]` | `mac` |
| `[2]` | `acc_ui` |
| `[3]` | `load_reg_li` |
| `[4]` | `load_reg_ui` |
| `[5]` | `flow_lr` |
| `[6]` | `flow_ud` |
| `[7]` | `flow_du` |
| `[8]` | `update_reg` |
| `[9]` | `exp2` |

### 4.4 `output_r[131:0]` 的位排列

| 列 | valid | data |
|---|---:|---:|
| 0 | `[0]` | `[32:1]` |
| 1 | `[33]` | `[65:34]` |
| 2 | `[66]` | `[98:67]` |
| 3 | `[99]` | `[131:100]` |

本测试中有两类输出：

1. `S` 元素经过 `viewEasA` 打包，FP16 位模式放在 32 位输出的低 16 位，不能直接把整个 32 位按普通 FP32 解读；
2. rowmax 由 `PROP_MAX` 输出为 `0-newMax`，是普通 FP32 的负数位模式。

> 以上位排列是根据当前生成的 `component.xml` 和 `systolic_array_top.v` 得出的。只要 C++ 结构体字段、类型宽度、数组大小或 HLS aggregate 方式发生变化，就必须重新检查，不能永久写死。

---

## 5. 固定测试数据和正确结果

板上测试使用与 C++ testbench 相同的数据：

```text
Q = [ 1  2  3  4 ]
    [ 2  1  0  1 ]
    [ 1  0  1  0 ]
    [ 3  2  1  0 ]

K = [ 1  0  2  1 ]
    [ 0  1  1  2 ]
    [ 2  1  0  1 ]
    [ 1  2  1  0 ]
```

金标准为：

```text
S = Q × K^T

S = [ 11  13  8  8 ]   rowmax = 13
    [  3   3  6  4 ]   rowmax =  6
    [  3   1  2  2 ]   rowmax =  3
    [  5   3  8  8 ]   rowmax =  8
```

由于这些值都能被 FP16 精确表示，板上比较可以直接比较位模式，不需要浮点误差范围。

---

## 6. 需要加入 Vivado 的 Verilog：测试控制器

在 Vivado 工程中创建 `sa_test_controller.v`，填入以下代码。

该控制器完成以下工作：

1. 等待 VIO 的 `run_test` 上升沿；
2. 发送 SA 逻辑复位事务；
3. 倒序把 Q 装入 PE.reg；
4. 发送三笔空事务，让控制和数据传播到最右列；
5. 按行错拍送入 K；
6. 控制 CMP 执行 `UPDATE` 和 `PROP_MAX`；
7. 在每个 `ap_done` 时检查输出；
8. 最后给出 `test_pass` 或 `test_fail`。

```verilog
`timescale 1ns / 1ps

module sa_test_controller (
    input  wire         clk,
    input  wire         rst,
    input  wire         run_test,

    output reg          ap_start,
    input  wire         ap_ready,
    input  wire         ap_done,
    input  wire         ap_idle,

    output reg  [121:0] input_r,
    input  wire [131:0] output_r,

    output reg          test_busy,
    output reg          test_done,
    output reg          test_pass,
    output reg          test_fail,

    output wire [5:0]   debug_transaction,
    output wire [2:0]   debug_state
);

    // 一共 25 笔逻辑事务：
    // 0       : SA 逻辑复位
    // 1..4    : 装载 Q
    // 5..7    : 空事务，冲刷水平 pipe
    // 8..24   : compute_cycle=0..16
    localparam [5:0] LAST_TRANSACTION = 6'd24;

    localparam [2:0] ST_IDLE        = 3'd0;
    localparam [2:0] ST_PREPARE     = 3'd1;
    localparam [2:0] ST_WAIT_ACCEPT = 3'd2;
    localparam [2:0] ST_WAIT_DONE   = 3'd3;
    localparam [2:0] ST_FINISHED    = 3'd4;

    reg [2:0] state;
    reg [5:0] transaction_index;
    reg       run_test_d;
    reg [131:0] last_output_r;

    wire run_test_rise = run_test & ~run_test_d;

    assign debug_transaction = transaction_index;
    assign debug_state = state;

    // 把本测试使用的非负小整数转换成 IEEE-754 binary16 位模式。
    function [15:0] half_small_integer;
        input integer value;
        begin
            case (value)
                0: half_small_integer = 16'h0000;
                1: half_small_integer = 16'h3c00;
                2: half_small_integer = 16'h4000;
                3: half_small_integer = 16'h4200;
                4: half_small_integer = 16'h4400;
                default: half_small_integer = 16'h0000;
            endcase
        end
    endfunction

    // Q[query][row]。函数返回 FP16 位模式。
    function [15:0] q_half;
        input integer query;
        input integer row;
        integer flat_index;
        begin
            flat_index = query*4 + row;
            case (flat_index)
                 0: q_half = half_small_integer(1);
                 1: q_half = half_small_integer(2);
                 2: q_half = half_small_integer(3);
                 3: q_half = half_small_integer(4);
                 4: q_half = half_small_integer(2);
                 5: q_half = half_small_integer(1);
                 6: q_half = half_small_integer(0);
                 7: q_half = half_small_integer(1);
                 8: q_half = half_small_integer(1);
                 9: q_half = half_small_integer(0);
                10: q_half = half_small_integer(1);
                11: q_half = half_small_integer(0);
                12: q_half = half_small_integer(3);
                13: q_half = half_small_integer(2);
                14: q_half = half_small_integer(1);
                15: q_half = half_small_integer(0);
                default: q_half = 16'h0000;
            endcase
        end
    endfunction

    // K[key][row]。函数返回 FP16 位模式。
    function [15:0] k_half;
        input integer key;
        input integer row;
        integer flat_index;
        begin
            flat_index = key*4 + row;
            case (flat_index)
                 0: k_half = half_small_integer(1);
                 1: k_half = half_small_integer(0);
                 2: k_half = half_small_integer(2);
                 3: k_half = half_small_integer(1);
                 4: k_half = half_small_integer(0);
                 5: k_half = half_small_integer(1);
                 6: k_half = half_small_integer(1);
                 7: k_half = half_small_integer(2);
                 8: k_half = half_small_integer(2);
                 9: k_half = half_small_integer(1);
                10: k_half = half_small_integer(0);
                11: k_half = half_small_integer(1);
                12: k_half = half_small_integer(1);
                13: k_half = half_small_integer(2);
                14: k_half = half_small_integer(1);
                15: k_half = half_small_integer(0);
                default: k_half = 16'h0000;
            endcase
        end
    endfunction

    // 生成一个打包后的 ValidData<PECtrl>，共 10 位。
    function [9:0] make_pe_ctrl;
        input mac_active;
        input flow_down_active;
        input load_active;
        reg [9:0] ctrl;
        begin
            ctrl = 10'b0;
            if (load_active) begin
                ctrl[0] = 1'b1; // valid
                ctrl[3] = 1'b1; // load_reg_li
            end else if (mac_active || flow_down_active) begin
                ctrl[0] = 1'b1;             // valid
                ctrl[1] = mac_active;       // mac
                ctrl[5] = mac_active;       // flow_lr
                ctrl[6] = flow_down_active; // flow_ud
            end
            make_pe_ctrl = ctrl;
        end
    endfunction

    // 根据事务编号生成完整 input_r。
    function [121:0] build_transaction;
        input [5:0] transaction;
        integer cycle;
        integer row;
        integer key;
        integer query;
        reg [121:0] word_value;
        reg mac_active;
        reg flow_down_active;
        begin
            word_value = 122'b0;

            if (transaction == 6'd0) begin
                // SystolicArrayInput.reset=true。
                word_value[0] = 1'b1;
            end else if (transaction >= 6'd1 && transaction <= 6'd4) begin
                // 与 C++ testbench 一致：query 按 3、2、1、0 倒序装载。
                cycle = transaction - 1;
                query = 3 - cycle;
                for (row=0; row<4; row=row+1) begin
                    word_value[1 + row*10 +: 10] =
                        make_pe_ctrl(1'b0, 1'b0, 1'b1);
                    word_value[58 + row*16 +: 16] = q_half(query, row);
                end
            end else if (transaction >= 6'd8 && transaction <= 6'd24) begin
                cycle = transaction - 8;

                // CmpControlCmd::UPDATE = 0，compute_cycle=4..7。
                if (cycle >= 4 && cycle < 8) begin
                    word_value[41]    = 1'b1;
                    word_value[49:42] = 8'd0;
                end
                // CmpControlCmd::PROP_MAX = 1，compute_cycle=8。
                else if (cycle == 8) begin
                    word_value[41]    = 1'b1;
                    word_value[49:42] = 8'd1;
                end

                for (row=0; row<4; row=row+1) begin
                    // 手动产生 InputDelayer 的行错拍效果。
                    key = cycle - (3-row);
                    mac_active = (key >= 0 && key < 4);

                    // 把 CMP 返回的数据向下送到阵列底部。
                    flow_down_active =
                        (cycle >= (5+row)) && (cycle < (10+row));

                    word_value[1 + row*10 +: 10] =
                        make_pe_ctrl(mac_active, flow_down_active, 1'b0);

                    if (mac_active)
                        word_value[58 + row*16 +: 16] = k_half(key, row);
                end
            end

            build_transaction = word_value;
        end
    endfunction

    // 当前 C++ testbench 的金标准位模式。
    // key=0..3 为 S 元素，低 16 位是 FP16 位模式；
    // key=4 为 PROP_MAX 输出的负 rowmax，是普通 FP32 位模式。
    function [31:0] expected_raw;
        input integer col;
        input integer key;
        integer flat_index;
        begin
            flat_index = col*5 + key;
            case (flat_index)
                 0: expected_raw = 32'h00004980; // 11
                 1: expected_raw = 32'h00004a80; // 13
                 2: expected_raw = 32'h00004800; // 8
                 3: expected_raw = 32'h00004800; // 8
                 4: expected_raw = 32'hc1500000; // -13.0f

                 5: expected_raw = 32'h00004200; // 3
                 6: expected_raw = 32'h00004200; // 3
                 7: expected_raw = 32'h00004600; // 6
                 8: expected_raw = 32'h00004400; // 4
                 9: expected_raw = 32'hc0c00000; // -6.0f

                10: expected_raw = 32'h00004200; // 3
                11: expected_raw = 32'h00003c00; // 1
                12: expected_raw = 32'h00004000; // 2
                13: expected_raw = 32'h00004000; // 2
                14: expected_raw = 32'hc0400000; // -3.0f

                15: expected_raw = 32'h00004500; // 5
                16: expected_raw = 32'h00004200; // 3
                17: expected_raw = 32'h00004800; // 8
                18: expected_raw = 32'h00004800; // 8
                19: expected_raw = 32'hc1000000; // -8.0f

                default: expected_raw = 32'h00000000;
            endcase
        end
    endfunction

    // 把某一列的 valid 和 data 从 output_r 中取出。
    function output_valid;
        input [131:0] packed_output;
        input integer col;
        begin
            case (col)
                0: output_valid = packed_output[0];
                1: output_valid = packed_output[33];
                2: output_valid = packed_output[66];
                3: output_valid = packed_output[99];
                default: output_valid = 1'b0;
            endcase
        end
    endfunction

    function [31:0] output_data;
        input [131:0] packed_output;
        input integer col;
        begin
            case (col)
                0: output_data = packed_output[32:1];
                1: output_data = packed_output[65:34];
                2: output_data = packed_output[98:67];
                3: output_data = packed_output[131:100];
                default: output_data = 32'h00000000;
            endcase
        end
    endfunction

    // 在 ap_done 有效时，用它检查这一笔事务产生的输出。
    reg output_check_error;
    integer check_cycle;
    integer check_col;
    integer check_key;
    reg expected_valid;
    reg actual_valid;
    reg [31:0] actual_data;

    always @(*) begin
        output_check_error = 1'b0;
        check_cycle = 0;
        check_key = 0;
        expected_valid = 1'b0;
        actual_valid = 1'b0;
        actual_data = 32'h00000000;

        for (check_col=0; check_col<4; check_col=check_col+1) begin
            if (transaction_index >= 6'd8 && transaction_index <= 6'd24) begin
                check_cycle = transaction_index - 8;
                check_key = check_cycle - 9 - check_col;
                expected_valid = (check_key >= 0 && check_key <= 4);
            end else begin
                // 复位、装载 Q 和冲刷 pipe 时，底部不应出现有效输出。
                check_key = 0;
                expected_valid = 1'b0;
            end

            actual_valid = output_valid(output_r, check_col);
            actual_data = output_data(output_r, check_col);

            if (actual_valid != expected_valid)
                output_check_error = 1'b1;
            else if (expected_valid &&
                     actual_data != expected_raw(check_col, check_key))
                output_check_error = 1'b1;
        end
    end

    always @(posedge clk) begin
        if (rst) begin
            state             <= ST_IDLE;
            transaction_index <= 6'd0;
            run_test_d        <= 1'b0;
            ap_start          <= 1'b0;
            input_r           <= 122'b0;
            test_busy         <= 1'b0;
            test_done         <= 1'b0;
            test_pass         <= 1'b0;
            test_fail         <= 1'b0;
            last_output_r     <= 132'b0;
        end else begin
            run_test_d <= run_test;

            // ap_done 的处理优先级最高。
            if ((state == ST_WAIT_ACCEPT || state == ST_WAIT_DONE) && ap_done) begin
                ap_start      <= 1'b0;
                last_output_r <= output_r;
                test_fail     <= test_fail | output_check_error;

                if (transaction_index == LAST_TRANSACTION) begin
                    state     <= ST_FINISHED;
                    test_busy <= 1'b0;
                    test_done <= 1'b1;
                    test_pass <= ~(test_fail | output_check_error);
                end else begin
                    transaction_index <= transaction_index + 1'b1;
                    state <= ST_PREPARE;
                end
            end else begin
                case (state)
                    ST_IDLE: begin
                        ap_start <= 1'b0;
                        if (run_test_rise) begin
                            transaction_index <= 6'd0;
                            test_busy <= 1'b1;
                            test_done <= 1'b0;
                            test_pass <= 1'b0;
                            test_fail <= 1'b0;
                            state <= ST_PREPARE;
                        end
                    end

                    ST_PREPARE: begin
                        // 先设置 input_r，再保持 ap_start，直到 ap_ready。
                        input_r  <= build_transaction(transaction_index);
                        ap_start <= 1'b1;
                        state    <= ST_WAIT_ACCEPT;
                    end

                    ST_WAIT_ACCEPT: begin
                        if (ap_ready) begin
                            ap_start <= 1'b0;
                            state <= ST_WAIT_DONE;
                        end
                    end

                    ST_WAIT_DONE: begin
                        ap_start <= 1'b0;
                    end

                    ST_FINISHED: begin
                        ap_start <= 1'b0;
                        // VIO 先写 0、再写 1，即可重新运行。
                        if (run_test_rise) begin
                            transaction_index <= 6'd0;
                            test_busy <= 1'b1;
                            test_done <= 1'b0;
                            test_pass <= 1'b0;
                            test_fail <= 1'b0;
                            state <= ST_PREPARE;
                        end
                    end

                    default: state <= ST_IDLE;
                endcase
            end
        end
    end

    // ap_idle 仅用于 ILA 观察，控制器不依赖它推进状态。
    wire unused_ap_idle = ap_idle;

endmodule
```

---

## 7. 需要加入 Vivado 的 Verilog：板级顶层

以下顶层直接对应 NM37 原理图：

1. `board_clk_p/n` 是 `BM43/BM42` 上的 100 MHz 差分时钟；
2. `reset_n` 是 `BF2` 上的 `SLOT_CPU_RESET`，按下 `SW2` 为低；
3. Clocking Wizard 实例名为 `clk_wiz_0`，输入和输出都是 100 MHz；
4. SA IP 实例名为 `systolic_array_top_0`；
5. VIO 实例名为 `vio_sa_0`；
6. ILA 实例名为 `ila_sa_0`。

创建 `sa_board_top.v`：

```verilog
`timescale 1ns / 1ps

module sa_board_top (
    input wire board_clk_p,
    input wire board_clk_n,
    input wire reset_n
);

    wire clk_100m;
    wire clk_locked;

    // 配置要求：
    // - Input Clock Source 选择 Differential；
    // - clk_in1 频率填写 100 MHz；
    // - clk_out1 = 100 MHz；
    // - reset 为高有效；
    // - 输出 locked。
    clk_wiz_0 u_clk_wiz (
        .clk_in1_p (board_clk_p),
        .clk_in1_n (board_clk_n),
        .reset     (~reset_n),
        .clk_out1  (clk_100m),
        .locked    (clk_locked)
    );

    // 复位异步拉高、同步释放。
    // clk_locked 后再额外等待 8 个 100 MHz 周期。
    reg [7:0] reset_shift;
    always @(posedge clk_100m or negedge reset_n) begin
        if (!reset_n)
            reset_shift <= 8'b0;
        else if (!clk_locked)
            reset_shift <= 8'b0;
        else
            reset_shift <= {reset_shift[6:0], 1'b1};
    end

    wire rst_100m = ~reset_shift[7];

    wire         sa_ap_start;
    wire         sa_ap_ready;
    wire         sa_ap_done;
    wire         sa_ap_idle;
    wire [121:0] sa_input_r;
    wire [131:0] sa_output_r;

    wire test_busy;
    wire test_done;
    wire test_pass;
    wire test_fail;
    wire [5:0] debug_transaction;
    wire [2:0] debug_state;

    wire vio_run_test;

    sa_test_controller u_test_controller (
        .clk               (clk_100m),
        .rst               (rst_100m),
        .run_test          (vio_run_test),
        .ap_start          (sa_ap_start),
        .ap_ready          (sa_ap_ready),
        .ap_done           (sa_ap_done),
        .ap_idle           (sa_ap_idle),
        .input_r           (sa_input_r),
        .output_r          (sa_output_r),
        .test_busy         (test_busy),
        .test_done         (test_done),
        .test_pass         (test_pass),
        .test_fail         (test_fail),
        .debug_transaction (debug_transaction),
        .debug_state       (debug_state)
    );

    // 在 IP Catalog 中把 SA 的 Component Name 设置成 systolic_array_top_0。
    systolic_array_top_0 u_sa (
        .ap_clk   (clk_100m),
        .ap_rst   (rst_100m),
        .ap_start (sa_ap_start),
        .ap_done  (sa_ap_done),
        .ap_idle  (sa_ap_idle),
        .ap_ready (sa_ap_ready),
        .input_r  (sa_input_r),
        .output_r (sa_output_r)
    );

    // VIO 配置：4 个 1 位输入 probe，1 个 1 位输出 probe。
    // probe_out0 初始值设为 0。
    vio_sa_0 u_vio (
        .clk        (clk_100m),
        .probe_in0  (test_busy),
        .probe_in1  (test_done),
        .probe_in2  (test_pass),
        .probe_in3  (test_fail),
        .probe_out0 (vio_run_test)
    );

    // ILA 配置见后文，采样深度建议 1024。
    ila_sa_0 u_ila (
        .clk    (clk_100m),
        .probe0 (sa_ap_start),
        .probe1 (sa_ap_ready),
        .probe2 (sa_ap_done),
        .probe3 (sa_ap_idle),
        .probe4 (sa_input_r),
        .probe5 (sa_output_r),
        .probe6 (debug_transaction),
        .probe7 (debug_state),
        .probe8 (test_done),
        .probe9 (test_fail)
    );

endmodule
```

### 7.1 NM37 时钟连接说明

NM37 给 VU37P 的是差分时钟，所以本顶层只使用 `board_clk_p/n`，不再保留单端 `board_clk` 写法。`CLK_100_DDR_P/N` 虽然名字中有 DDR，但在本次不使用 DDR4 控制器时，可以作为 SA 板级顶层的 100 MHz 输入时钟。

Clocking Wizard 负责例化差分输入缓冲和全局时钟缓冲，并提供 `locked`。因为输入已经是 100 MHz，`clk_out1` 仍设置为 100 MHz，不做变频。

### 7.2 NM37 复位连接说明

`SLOT_CPU_RESET` 由板上 4.7 kΩ 电阻上拉到 1.8 V，按下 `SW2` 时拉低，因此顶层端口名为 `reset_n`。HLS IP 的 `ap_rst` 高有效，顶层代码已通过 `reset_shift` 实现异步拉高、同步释放，不要把 `reset_n` 直接连到 `ap_rst`。

`SLOT_CPU_RESET` 同时连到板上其他部件，按下时可能也会复位 ZU5EV 或板级逻辑，这不影响本次独立 SA 功能测试。

---

## 8. NM37 的 XDC 约束

创建 `sa_board.xdc`，填入下面的实际约束：

```tcl
## NM37 板载 100 MHz 差分时钟 CLK_100_DDR_P/N
set_property PACKAGE_PIN BH42 [get_ports board_clk_p]
set_property PACKAGE_PIN BJ42 [get_ports board_clk_n]
set_property IOSTANDARD DIFF_SSTL12 [get_ports {board_clk_p board_clk_n}]

## NM37 板载 SW2：SLOT_CPU_RESET，低有效
set_property PACKAGE_PIN BF2 [get_ports reset_n]
set_property IOSTANDARD LVCMOS18 [get_ports reset_n]
```

`BM43/BM42` 所在的 Bank 65 由 1.2 V 供电，所以本文使用 `DIFF_SSTL12`。原理图中的 SiT9120 振荡器为 100 MHz 差分输出，信号经交流耦合后进入该 1.2 V Bank。如果实验室已有确认通过的 NM37 XDC，应对照其时钟 IOSTANDARD 和终端属性；不要在未确认电气连接的情况下自行改成 `LVDS_25`。

不要用下面的方法绕过错误：

```tcl
# 不要这样做：这只是压掉错误，没有真正约束引脚
set_property SEVERITY Warning [get_drc_checks UCIO-1]
set_property SEVERITY Warning [get_drc_checks NSTD-1]
```

出现 UCIO-1 或 NSTD-1 时，应补齐正确的引脚和 IOSTANDARD。

---

## 9. 在 Vivado 2024.2 中创建工程

### 9.1 新建 RTL Project

1. 启动 Vivado 2024.2；
2. 点击 **Create Project**；
3. 为工程选择一个不在 HLS `build/` 内的独立目录，例如：

   ```text
   FSA-HLS/vivado/sa_board_test/
   ```

4. Project Type 选择 **RTL Project**；
5. 可以暂时勾选 **Do not specify sources at this time**；
6. 在 Default Part 页面选择 **Parts**；
7. 选择与 HLS 目标一致的 `xcvu37p_CIV-fsvh2892-2-e`；
8. 完成创建。

本文已给出 NM37 的 XDC，不依赖 Vivado Board File。如果服务器无法取得支持该 Part 的 Synthesis License，应先联系管理员修复许可证，不要更换成其他 FPGA 器件来绕过。

### 9.2 加入 HLS IP Repository

推荐先解压 IP 压缩包，例如解压到：

```text
FSA-HLS/vivado/ip_repo/systolic_array_top_1_0/
```

确认该目录中直接或下一层存在 `component.xml`。

然后：

1. 打开 **Tools → Settings**；
2. 左侧选择 **IP → Repository**；
3. 点击 `+`；
4. 选择包含 `component.xml` 的 IP 根目录，或其仓库父目录；
5. 点击 **Apply**；
6. 点击 **OK**；
7. 打开 **IP Catalog**；
8. 搜索 `systolic_array_top`。

正确时应看到类似信息：

```text
xilinx.com:hls:systolic_array_top:1.0
```

如果搜不到，检查：

1. 是否选择了正确目录；
2. `component.xml` 是否存在；
3. IP 是否由 2024.2 导出；
4. 是否点击了 **Refresh All Repositories**；
5. 压缩包是否损坏或只复制了部分 `hdl` 文件。

### 9.3 创建 SA IP 实例

1. 在 IP Catalog 中双击 `systolic_array_top`；
2. 将 **Component Name** 设置为：

   ```text
   systolic_array_top_0
   ```

3. 点击 **OK**；
4. 在弹窗中点击 **Generate**；
5. 等待 **Generate Output Products** 完成。

必须保证 Component Name 与 `sa_board_top.v` 中例化的模块名一致。如果生成名称是别的名字，就相应修改 Verilog 例化名称。

### 9.4 创建 Clocking Wizard

1. 在 IP Catalog 搜索 **Clocking Wizard**；
2. 双击并把 Component Name 设置为 `clk_wiz_0`；
3. **Input Clock Source** 选择 **Differential clock capable pin**；
4. `clk_in1` 输入频率设置为 **100.000 MHz**；
5. Output Clocks 中只保留需要的 `clk_out1`；
6. 将 `clk_out1` 设置为 **100.000 MHz**；
7. 启用 `locked`；
8. 启用高有效 `reset`；
9. 生成 Output Products。

100 MHz 是 NM37 的实际板载输入时钟，也是当前 HLS 工程的时钟目标。若实现后出现时序违例，应分析并修改设计，不要通过放宽时钟周期掩盖问题。

### 9.5 创建 VIO

1. 在 IP Catalog 搜索 **Virtual Input/Output**；
2. Component Name 设置为 `vio_sa_0`；
3. Input Probe Count 设置为 `4`；
4. 四个输入 probe 的宽度都设置为 `1`；
5. Output Probe Count 设置为 `1`；
6. `probe_out0` 宽度设置为 `1`；
7. `probe_out0` Initial Value 设置为 `0`；
8. 生成 Output Products。

VIO 中各 probe 含义：

| VIO probe | 含义 |
|---|---|
| `probe_in0` | `test_busy` |
| `probe_in1` | `test_done` |
| `probe_in2` | `test_pass` |
| `probe_in3` | `test_fail` |
| `probe_out0` | `run_test` |

### 9.6 创建 ILA

1. 在 IP Catalog 搜索 **Integrated Logic Analyzer**；
2. Component Name 设置为 `ila_sa_0`；
3. Number of Probes 设置为 `10`；
4. Sample Data Depth 建议设置为 `1024`；
5. 各 probe 宽度按下表填写；
6. 生成 Output Products。

| ILA probe | 宽度 | 信号 |
|---|---:|---|
| `probe0` | 1 | `sa_ap_start` |
| `probe1` | 1 | `sa_ap_ready` |
| `probe2` | 1 | `sa_ap_done` |
| `probe3` | 1 | `sa_ap_idle` |
| `probe4` | 122 | `sa_input_r` |
| `probe5` | 132 | `sa_output_r` |
| `probe6` | 6 | `debug_transaction` |
| `probe7` | 3 | `debug_state` |
| `probe8` | 1 | `test_done` |
| `probe9` | 1 | `test_fail` |

1024 个采样点足够观察本方案的串行测试。若加入 ILA 后资源或时序压力明显增大，可优先减少采样深度或减少宽总线 probe，不要先修改 SA 的 100 MHz 目标。

### 9.7 添加 Verilog 和 XDC

1. **Add Sources → Add or Create Design Sources**；
2. 添加：

   ```text
   sa_test_controller.v
   sa_board_top.v
   ```

3. **Add Sources → Add or Create Constraints**；
4. 添加填写完成的 `sa_board.xdc`；
5. 在 Sources 窗口右键 `sa_board_top`；
6. 选择 **Set as Top**；
7. 检查 Sources 窗口中 SA、Clock Wizard、VIO、ILA 都已出现。

---

## 10. 上板前先运行一次 Vivado RTL 仿真

HLS 的 C/RTL 协同仿真验证了 HLS 顶层，但没有验证本说明书新增的 Verilog 测试控制器。因此，在综合板级顶层之前，应在 Vivado 中再运行一次 RTL behavioral simulation。

创建 `tb_sa_ip_board_control.v`，并作为 **Simulation Sources** 加入工程：

```verilog
`timescale 1ns / 1ps

module tb_sa_ip_board_control;

    reg clk = 1'b0;
    reg rst = 1'b1;
    reg run_test = 1'b0;

    always #5 clk = ~clk; // 100 MHz

    wire         ap_start;
    wire         ap_ready;
    wire         ap_done;
    wire         ap_idle;
    wire [121:0] input_r;
    wire [131:0] output_r;

    wire test_busy;
    wire test_done;
    wire test_pass;
    wire test_fail;
    wire [5:0] debug_transaction;
    wire [2:0] debug_state;

    sa_test_controller u_controller (
        .clk               (clk),
        .rst               (rst),
        .run_test          (run_test),
        .ap_start          (ap_start),
        .ap_ready          (ap_ready),
        .ap_done           (ap_done),
        .ap_idle           (ap_idle),
        .input_r           (input_r),
        .output_r          (output_r),
        .test_busy         (test_busy),
        .test_done         (test_done),
        .test_pass         (test_pass),
        .test_fail         (test_fail),
        .debug_transaction (debug_transaction),
        .debug_state       (debug_state)
    );

    systolic_array_top_0 u_sa (
        .ap_clk   (clk),
        .ap_rst   (rst),
        .ap_start (ap_start),
        .ap_done  (ap_done),
        .ap_idle  (ap_idle),
        .ap_ready (ap_ready),
        .input_r  (input_r),
        .output_r (output_r)
    );

    initial begin
        // 先执行 HLS RTL 复位。
        repeat (10) @(posedge clk);
        rst <= 1'b0;

        // 产生 run_test 上升沿。
        repeat (5) @(posedge clk);
        run_test <= 1'b1;
        @(posedge clk);
        run_test <= 1'b0;

        wait (test_done == 1'b1);
        @(posedge clk);

        if (test_pass && !test_fail)
            $display("[PASS] SA IP board controller test");
        else
            $error("[FAIL] SA IP board controller test");

        repeat (10) @(posedge clk);
        $finish;
    end

    // 防止握手错误导致仿真永远不结束。
    // 正常测试会在此前输出 PASS 并执行 $finish。
    initial begin
        #20000;
        $fatal(1, "[TIMEOUT] SA IP board controller test");
    end

endmodule
```

该测试需要超过 Vivado 默认的 `1000 ns`。如果不修改 XSim Runtime，Vivado 会在 1 μs 时暂停，此时 testbench 还在等待 `test_done`，所以 Tcl Console 不会出现 PASS。这不是 SA 卡死，而是仿真时间不够。

为了让以后每次点击 **Run Behavioral Simulation** 都自动跑到结束，先在 Vivado Tcl Console 中执行一次：

```tcl
set_property -name {xsim.simulate.runtime} -value {20us} \
    -objects [get_filesets sim_1]
```

`20us` 是超时上限，不代表正常仿真一定会跑满 20 μs。正常情况下，testbench 在看到 `test_done` 后会自动输出 PASS/FAIL，然后执行 `$finish` 提前结束。

也可以通过 GUI 设置：**Tools → Settings → Simulation → Simulation → xsim.simulate.runtime**，把 `1000ns` 改为 `20us`。

完整运行方法：

1. **Add Sources → Add or Create Simulation Sources**；
2. 加入 `tb_sa_ip_board_control.v`；
3. 在 Simulation Sources 中右键该文件，选择 **Set as Top**；
4. 确认 `xsim.simulate.runtime=20us`；
5. Flow Navigator → **Simulation → Run Simulation → Run Behavioral Simulation**；
6. 等待 testbench 自动执行 `$finish`；
7. Tcl Console 应看到：

   ```text
   [PASS] SA IP board controller test
   ```

如果仿真已经打开且仍停在 `1000 ns`，可以临时执行 `run 20 us`。这只是当次补跑；上面的 `set_property` 才是对后续仿真的持久设置。

如果报找不到浮点 IP 仿真模型，先对 `systolic_array_top_0` 执行 **Generate Output Products**，并确认 simulation target 已生成。若仿真失败，不要继续上板；先根据 `debug_transaction`、`input_r` 和 `output_r` 找到第一次错误。

仿真完成后，把 Design Sources 的顶层重新设为 `sa_board_top`。Simulation Top 和 Synthesis Top 可以不同，但必须确认综合顶层是板级顶层。

---

## 11. 先做 Elaborated Design 检查

在真正综合前先运行：

1. Flow Navigator → **RTL Analysis → Open Elaborated Design**；
2. 检查是否存在以下实例：

   ```text
   u_clk_wiz
   u_test_controller
   u_sa
   u_vio
   u_ila
   ```

3. 检查 `u_sa` 端口宽度：

   ```text
   input_r  = 122 bits
   output_r = 132 bits
   ```

4. 检查是否存在未连接端口；
5. 检查时钟和复位方向；
6. 检查顶层端口是否与 XDC 名称完全一致。

常见错误：

```text
module 'systolic_array_top_0' not found
```

处理方法：

1. 检查 SA IP 是否已经生成 Output Products；
2. 检查实际 Component Name；
3. 检查 `sa_board_top.v` 中例化名；
4. 在 IP Sources 中确认 `.xci` 文件存在。

---

## 12. 综合、实现和生成 bitstream

### 12.1 Run Synthesis

1. 点击 **Run Synthesis**；
2. 完成后点击 **Open Synthesized Design**；
3. 查看 **Report Utilization**；
4. 确认 16 个 PE 对应的浮点运算资源没有被意外删除；
5. 检查 VIO 和 ILA 是否存在；
6. 检查关键警告，不要只看“综合成功”。

### 12.2 Run Implementation

1. 点击 **Run Implementation**；
2. 完成后打开 Implemented Design；
3. 运行 **Report Timing Summary**；
4. 重点查看：

   ```text
   WNS
   TNS
   WHS
   THS
   Unconstrained Paths
   ```

合格条件至少包括：

| 项目 | 要求 |
|---|---|
| Setup WNS | `>= 0 ns` |
| Hold WHS | `>= 0 ns` |
| Unconstrained Paths | 不应存在关键未约束路径 |
| DRC | 不应有阻止生成 bitstream 的 Error |
| 时钟 | SA 实际工作时钟为 100 MHz |

HLS 报告中的 Estimated Clock 不是最终布线结果。只有 Vivado Implementation 的 timing summary 才能说明该板级工程是否真正满足 100 MHz。

如果 WNS 小于 0：

1. 查看最差路径的起点、终点和经过模块；
2. 判断问题在 SA、测试控制器、调试核还是跨层级连接；
3. 检查 Clocking Wizard 和 XDC 是否正确；
4. 若主要压力来自 ILA，减少 probe 或 depth 后重新实现；
5. 若主要压力来自 SA，回到 HLS/RTL 数据通路优化；
6. 不要直接把 10 ns 改成更宽的周期来宣布“通过”。

### 12.3 Generate Bitstream

1. 确认实现和时序检查已完成；
2. 点击 **Generate Bitstream**；
3. 等待完成；
4. 记录 `.bit` 和 `.ltx` 文件位置。

典型位置为：

```text
<Vivado工程>/<工程名>.runs/impl_1/sa_board_top.bit
<Vivado工程>/<工程名>.runs/impl_1/sa_board_top.ltx
```

`.bit` 是下载到 FPGA 的配置数据；`.ltx` 保存 ILA/VIO probe 对应关系。使用调试核时两者必须与同一次实现结果匹配。

可以在 Vivado Tcl Console 中补充生成报告：

```tcl
report_timing_summary -file sa_board_timing_summary.rpt
report_utilization -hierarchical -file sa_board_utilization.rpt
report_drc -file sa_board_drc.rpt
```

---

## 13. 连接 FPGA 并下载

### 13.1 硬件准备

1. 确认 NM37 供电和散热正常；
2. 确认 JTAG 选择电路已把板载 FT2232 连到 VU37P，而不是只连到 ZU5EV 或外部 Slot；
3. 连接板载 FT2232 对应的 USB/JTAG 线；
4. 若使用远程服务器，确认下载器连接在运行 `hw_server` 的机器上；
5. 上电并等待板卡电源状态稳定；
6. 注意 `DS3` 只能表示 FPGA `DONE` 配置状态，不代表 SA 计算通过。

### 13.2 打开 Hardware Manager

1. Vivado 左侧 Flow Navigator 选择 **Program and Debug → Open Hardware Manager**；
2. 点击 **Open Target → Auto Connect**；
3. 等待 JTAG 链中的器件出现；
4. 确认识别出的 Part 与工程目标一致。

若使用远程 `hw_server`：

1. 在连接开发板的机器上启动 `hw_server`；
2. Vivado 中选择 **Open New Target**；
3. 填写服务器 IP 和端口，默认常见端口为 `3121`；
4. 选择正确的 JTAG target 和器件。

### 13.3 Program Device

1. 右键目标 FPGA；
2. 选择 **Program Device**；
3. Program file 选择本次生成的 `sa_board_top.bit`；
4. Debug probes file 选择同次生成的 `sa_board_top.ltx`；
5. 点击 **Program**；
6. 等待完成；
7. 短按并释放板上 `SW2`，让 `SLOT_CPU_RESET` 确实产生一次低有效复位；
8. 等待 Clocking Wizard `locked` 且顶层的 8 拍复位延迟结束。

不要把旧 `.ltx` 与新 `.bit` 混用，否则 ILA/VIO 可能无法识别，或者 probe 映射错误。

---

## 14. 在板上运行测试

### 14.1 下载后初始状态

下载完成后：

1. 打开 Hardware Manager 中的 VIO dashboard；
2. 确认 `probe_out0/run_test=0`；
3. 正常初始状态应为：

   ```text
   test_busy = 0
   test_done = 0
   test_pass = 0
   test_fail = 0
   ```

如果 VIO 一开始就是 `run_test=1`，先把它改为 0。

### 14.2 设置 ILA 触发条件

1. 打开 ILA dashboard；
2. 把 `probe8/test_done` 设为触发信号；
3. 触发条件设为从 0 变为 1，或直接设为 `==1`；
4. Trigger Position 建议放在采样窗口靠后位置，例如 900/1024；
5. 点击 **Run Trigger**，先让 ILA 进入等待状态。

把触发点放在靠后位置，是为了保留测试完成前的大量握手和数据波形。

### 14.3 启动测试

1. 回到 VIO；
2. 把 `run_test` 从 0 改为 1；
3. 控制器检测到上升沿后开始运行；
4. `test_busy` 应变成 1；
5. 测试结束后 `test_done` 应变成 1；
6. 查看 `test_pass` 和 `test_fail`。

通过状态必须是：

```text
test_busy = 0
test_done = 1
test_pass = 1
test_fail = 0
```

失败状态是：

```text
test_done = 1
test_fail = 1
```

### 14.4 重新运行

1. 在 VIO 中把 `run_test` 改回 0；
2. 重新点击 ILA 的 **Run Trigger**；
3. 再把 `run_test` 改为 1；
4. 控制器会再次从逻辑复位事务开始，不需要重新下载 bitstream。

---

## 15. 如何阅读 ILA 波形

### 15.1 一笔事务的正常顺序

在 ILA 中，一笔事务应大致表现为：

```text
input_r 设置完成
       │
       ├── ap_start 拉高并保持
       │
       ├── ap_ready 出现：输入被接受，控制器拉低 ap_start
       │
       └── ap_done 出现：控制器读取 output_r
```

当前 HLS 报告中 SA 顶层大致为：

```text
Latency = 17 cycles
II      = 16 cycles
```

本测试控制器没有尝试重叠事务，因此实际看到的事务间隔还会多出控制器的准备状态。这不影响第一次功能验证，但不能用来代表 SA 的最终吞吐率。

### 15.2 `debug_transaction` 的含义

| 数值 | 含义 |
|---:|---|
| 0 | SA 逻辑复位 |
| 1..4 | 装载 Q |
| 5..7 | 空事务，冲刷 pipe |
| 8..24 | 计算阶段，对应 compute cycle 0..16 |

### 15.3 输出出现的时序

令 `compute_cycle = debug_transaction - 8`，输出中的 key 为：

```text
key = compute_cycle - 9 - col
```

其中：

| key | 含义 |
|---:|---|
| 0..3 | `S[col][key]` |
| 4 | 第 `col` 行的负 rowmax |
| 其他 | 该列 `valid` 应为 0 |

因此有效输出大致从 compute cycle 9 开始，最后一个 rowmax 在 compute cycle 16 从第 3 列出现。

### 15.4 建议的 ILA radix

| 信号 | 建议显示方式 |
|---|---|
| `debug_transaction` | Unsigned Decimal |
| `debug_state` | Unsigned Decimal |
| `input_r` | Hexadecimal |
| `output_r` | Hexadecimal |
| handshake/valid | Binary |

Vivado 默认不会自动把 16 位 binary16 显示为正常小数。本测试直接比较位模式，因此只需确认 `test_pass`，需要人工检查时再根据第 4 节的位范围拆分。

---

## 16. 常见故障与排查顺序

### 16.1 Vivado 中找不到 SA IP

依次检查：

1. IP repository 路径中是否存在 `component.xml`；
2. 是否加入了完整 IP，而不是只复制顶层 `.v`；
3. 是否刷新 repository；
4. Vivado 与导出 IP 是否都是 2024.2；
5. `component.xml` 中是否能看到 `xilinx.com:hls:systolic_array_top:1.0`。

### 16.2 `systolic_array_top_0` 未定义

1. 检查 IP 的 Component Name；
2. 检查 `.xci` 是否已加入工程；
3. 右键 IP，执行 **Generate Output Products**；
4. 必要时执行 **Reset Output Products** 后重新 Generate；
5. 修改 Verilog 中的实例模块名，使其与实际 Component Name 一致。

### 16.3 `synth_design` 报 `Common 17-345`

如果日志包含：

```text
A valid license was not found for feature 'Synthesis'
and/or device 'xcvu37p_CIV'
```

这不是 SA IP、Verilog 或 XDC 的错误，而是 Vivado 没有取得支持 `xcvu37p_CIV` 的综合许可证。应在 **Help → Manage License** 中检查许可证，或向服务器管理员确认 `XILINXD_LICENSE_FILE`、License Server 状态和可用席位。在许可证修复前，重复运行综合不会有效。

### 16.4 Clocking Wizard 不 locked

1. 检查 Clocking Wizard 是否选择 Differential；
2. 检查输入和输出频率是否都为 100 MHz；
3. 检查 P/N 是否分别约束到 `BM43/BM42`；
4. 检查时钟 IOSTANDARD 是否为 `DIFF_SSTL12`；
5. 检查 `reset_n` 是否一直为低；
6. 用 ILA 或板上已有时钟测试逻辑确认 `clk_100m` 是否运行。

### 16.5 `ap_start` 有，但没有 `ap_ready/ap_done`

1. 确认 `ap_rst` 已释放；
2. 确认 SA 和控制器使用同一个 `clk_100m`；
3. 确认 SA IP 端口没有接反；
4. 确认 `input_r` 宽度为 122；
5. 检查 Implementation 是否有时序违例；
6. 检查 ILA 采样时钟是否真正运行；
7. 确认没有使用旧 `.bit` 或旧 `.ltx`。

### 16.6 `test_done=1` 且 `test_fail=1`

先在 ILA 中找到第一次不符合预期的事务：

1. 查看 `debug_transaction`；
2. 查看该事务的 `output_r`；
3. 根据输出位表拆出每列 valid 和 data；
4. 判断是 valid 时序错误还是数据错误；
5. 若最初几笔就错误，先检查输入打包和 Q 装载；
6. 若只有 S 错误，检查 PE/MAC 和 K 的错拍；
7. 若 S 正确但 rowmax 错误，检查 CMP 控制传播；
8. 在 C/RTL 协同仿真中加入相同打包输入进行对照。

### 16.7 Hardware Manager 找不到 ILA/VIO

1. 确认下载时选择了 `.ltx`；
2. 确认 `.ltx` 和 `.bit` 来自同一次实现；
3. Refresh Device；
4. 检查实现后的 netlist 中是否存在 ILA/VIO；
5. 检查调试核时钟是否运行；
6. 重新 Program Device。

### 16.8 Generate Bitstream 被 UCIO-1 或 NSTD-1 阻止

说明顶层端口缺少真实引脚或电平标准。先确认顶层端口名与 XDC 一致，再检查 `BM43/BM42/BF2` 和 `DIFF_SSTL12/LVCMOS18` 约束是否生效。不要降低 DRC 严重级别来绕过。

### 16.9 实现后 100 MHz 时序不通过

1. 保存 timing summary；
2. 查看最差路径是否位于 SA 内部；
3. 查看是否由 ILA 宽总线扇出造成；
4. 先减少 ILA probe/depth 后复测；
5. 若仍在 SA 内部，则回到 HLS 修改数据通路和流水结构；
6. 重新 HLS 综合、协同仿真、导出 IP；
7. 在 Vivado 中更新 IP 后重新实现。

HLS 综合通过、IP 导出成功、Vivado 综合通过、Vivado 时序通过、bitstream 生成成功、JTAG 下载成功、固定向量测试通过，是六个不同层级的结论，不能互相代替。

---

## 17. 更新 SA IP 后的标准流程

以后修改 HLS 代码后，按以下顺序更新：

1. 运行 SA 的 C simulation；
2. 运行 C synthesis；
3. 运行 C/RTL co-simulation；
4. 导出 Vivado IP；
5. 备份新的综合报告；
6. 在 Vivado 中刷新 IP repository；
7. 若提示 IP 版本或 revision 变化，执行 **Report IP Status**；
8. 执行 **Upgrade Selected**；
9. 对 SA IP 执行 **Reset Output Products**；
10. 重新 **Generate Output Products**；
11. 重新综合和实现；
12. 重新检查 100 MHz timing；
13. 重新生成 `.bit` 和 `.ltx`；
14. 下载并重新运行板上测试。

如果端口宽度变化，还必须同步修改：

```text
sa_test_controller.v
sa_board_top.v
ILA probe 宽度
输入/输出位排列说明
板上金标准检查逻辑
```

---

## 18. 本次测试通过后可以得出什么结论

当同时满足以下条件时：

```text
Vivado Implementation 的 100 MHz setup/hold timing 通过
bitstream 成功下载
VIO 显示 test_done=1
VIO 显示 test_pass=1
VIO 显示 test_fail=0
ILA 中握手和输出时序正常
```

可以得出：

> 当前这一版 4×4 SA IP 已经在目标 FPGA 上完成固定矩阵的基本功能验证，PE、CMP、阵列状态推进、输出 valid、`S=QK^T` 和 rowmax 数据通路能够在实际硬件中工作。

但不能据此直接得出：

1. 所有浮点边界条件都正确；
2. exp2 的所有分段都已在板上覆盖；
3. 任意长度、任意矩阵都正确；
4. SA 已达到 Chisel 版本的吞吐率；
5. DMA、SRAM、Accumulator、HBM 或完整 FlashAttention 已经可用；
6. 长时间运行和极限频率已经稳定。

下一步建议按以下顺序进行：

1. 增加多组固定 Q/K 测试向量；
2. 增加负数、零、较大值和 FP16 边界值；
3. 单独覆盖 exp2 的 8 个 PWL 分段；
4. 编写能够按 `ap_ready` 连续发 token 的性能测试控制器；
5. 测量真实 Latency 和可接受输入间隔；
6. 再连接 Delayer、Accumulator 和 BankedSRAM；
7. 最后再加入 DMA/HBM/PCIe 等外围模块。

---

## 19. 官方参考资料

- `NM37_SCH_220331.pdf`：NM37 开发板原理图，本文的 VU37P 型号、时钟、复位和 JTAG 连接依据。
- [SiTime SiT9120AI-2C2-33E100.000000：100 MHz LVDS 差分振荡器](https://www.sitime.com/parts/sit9120ai-2c2-33e100000000)
- [AMD UltraScale SelectIO：I/O Standard 与 Bank 电压规则](https://docs.amd.com/r/en-US/ug861-ultrascale-selectio/Rules-for-Combining-I/O-Standards-in-the-Same-Bank)
- [AMD UG994：Creating a Design with Vitis HLS IP（Vivado 2024.2）](https://docs.amd.com/r/2024.2-English/ug994-vivado-ip-subsystems/Creating-a-Design-with-Vitis-HLS-IP)
- [AMD UG1118：Creating and Packaging Custom IP（Vivado 2024.2）](https://docs.amd.com/r/2024.2-English/ug1118-vivado-creating-packaging-custom-ip)
- [AMD UG908：Programming and Debugging（Vivado 2024.2）](https://docs.amd.com/r/2024.2-English/ug908-vivado-programming-debugging)
- [AMD UG1399：Vitis HLS User Guide](https://docs.amd.com/r/en-US/ug1399-vitis-hls)
