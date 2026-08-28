# `fsa_core_request_top` 综合报告

## 1. 综合配置

本报告读取 `build/fsa_core_request_build/solution1` 的现有产物编写，没有重新运行
Vitis HLS，也没有修改源码、测试平台、Tcl、时钟、器件或接口。

| 项目 | 当前构建 |
|---|---|
| Vitis HLS | 2024.2，Build 5238294 |
| 综合顶层 | `fsa_core_request_top` |
| Solution | `solution1`，Vivado IP Flow Target |
| 目标器件 | `xcvu37p_CIV-fsvh2892-2-e`（Virtex UltraScale+ HBM） |
| 阵列配置 | `SA_ROWS=4`，`SA_COLS=4` |
| 时钟目标 | 10.00 ns，即 100 MHz |
| 时钟不确定度 | 2.70 ns |
| 有效组合逻辑预算 | 7.30 ns |
| RTL / 协同仿真 | Verilog / XSIM |
| 块级协议 | `ap_ctrl_hs` |
| 构建产物时间范围 | 2026-08-21 21:14:09 至 23:22:54（UTC+8） |

Tcl 加入请求顶层、控制器、执行计划、数据通路、SRAM、Delayer、SA、PE、CMP、
Accumulator 和算术实现，以及唯一测试平台 `test_fsa_core_request_top.cpp`。当前本地
Tcl、源码和测试的修改时间均早于构建开始时间；但产物内嵌的是 Linux 服务器路径
`/home/zhangchenxuan/FSA_HLS/...`，当前检查位置为 Windows 构建副本，无法证明两边
文件逐字一致。因此以下结论以所给产物为准，源码版本对应性为
**无法完全确认，构建结果可能过期**。

## 2. 流程结果

| 阶段 | 状态 | 证据与限制 |
|---|---|---|
| C 仿真 | 通过 | 日志包含双 KV block 在线 softmax 的 PASS 标记，0 error |
| C 综合 | 完成 | 顶层及 18 个子模块/循环综合报告存在 |
| RTL 协同仿真 | 通过 | Verilog/XSIM，3 个事务，总执行 4,072 周期 |
| IP 导出 | 完成 | `component.xml` 和 `export.zip` 存在 |
| Vivado 综合/实现 | 未执行/未提供 | 没有实现后资源、WNS、跨 SLR 布线或实际 Fmax |
| FPGA 板级验证 | 未执行/未提供 | 没有板上接口、复位和功能证据 |

C 仿真、C 综合、协同仿真和 IP 导出是四个独立结果；IP 打包成功不代表 Vivado
布局布线或板级验证通过。

## 3. 功能验证范围

测试平台通过综合顶层执行 3 次请求级事务：

1. 复位事务：检查 `request_ready=true`、`request_done=false`。
2. 第一个 KV block：`initialize=true`、`finalize=false`，检查请求完成、无
   `protocol_error`、没有提前归一化，逻辑步骤数为 71。
3. 第二个 KV block：`initialize=false`、`finalize=true`，检查请求完成、无
   `protocol_error`、最终归一化，逻辑步骤数为 89。

金标准独立计算 4 个 query 对两个 block、共 8 个 key 的 score、最大值、指数、L 和
O/L；最终检查 4 个 L 和 4×4 个 O 均为有限值，绝对误差不超过 0.12。最终结果同时依赖
两个 block，因此可以验证非零旧 L/O 和在线最大值在两个顶层事务之间得到复用。

当前测试尚未覆盖：

- `causal=true` 的掩码路径；
- 未初始化续传请求及预期 `protocol_error`；
- `request_valid=false`、单 block 初始化并立即结束、3 个以上 block；
- 非法地址/子 bank、接口冲突以及潜在越界告警涉及的其他访问组合；
- NaN、Inf、极端数值、随机矩阵和更严格的数值误差统计；
- 独立 `ap_rst` 时序、输入在长事务期间变化、系统级输出有效控制；
- Vivado 实现和 FPGA 板上行为。

## 4. 时序与请求吞吐

### 4.1 HLS 时序估算

| 指标 | 当前值 |
|---|---:|
| 目标周期 | 10.000 ns |
| 时钟不确定度 | 2.700 ns |
| 有效预算 | 7.300 ns |
| HLS 估算周期 | 7.200 ns |
| 计算得到的有效预算裕量 | **+0.100 ns** |
| 由估算周期直接换算的频率 | 约 138.89 MHz |

`7.200 + 2.700 = 9.900 ns`，因此本次 **100 MHz HLS 时序估算通过**，但只剩
0.100 ns 裕量。138.89 MHz 是由 HLS 估算周期换算的数值，不是布局布线后的 Fmax；
考虑到资源规模和跨 SLR 风险，必须以 Vivado 实现时序为准。

### 4.2 顶层延迟

顶层为非流水化 `ap_ctrl_hs` 事务，分支不同导致延迟变化很大：

| HLS 指标 | 最小 | 平均 | 最大 |
|---|---:|---:|---:|
| 延迟 | 1 | 542 | 3,754 周期 |
| 启动间隔 | 2 | 未单列 | 3,755 周期 |

这里的 HLS 平均值来自综合模型，不等同于实际工作负载平均值，也不能解释为固定 II。

协同仿真实际执行的事务为：

| 事务 | 语义 | 延迟 | 至下一事务间隔 | 100 MHz 时间 |
|---:|---|---:|---:|---:|
| 0 | 复位 | 1 | 2 | 0.01 μs |
| 1 | 初始化但不结束的第一个 block | 1,819 | 1,820 | 18.19 μs |
| 2 | 续传并归一化的第二个 block | 2,250 | 无下一事务 | 22.50 μs |

总执行时间为 4,072 周期，即 40.72 μs @ 100 MHz。协同仿真报告的平均延迟
1,356 周期包含 1 周期复位事务，不适合作为普通 KV block 的“典型延迟”。顶层不能在
一个请求尚未完成时接收下一个请求，局部子模块 II=1 也不会改变这一点。

## 5. 关键子模块、循环与并行性

### 5.1 调度结果

| 模块/循环 | 延迟 | Interval / II | 说明 |
|---|---:|---:|---|
| `preloadQKV` | 438 | 438 | Q、K、V 三段预装载 |
| 三个预装载外循环 | 各 142 | **II=35** | 目标 II=1，trip count=4；各包装模块延迟 144 |
| `writeSpadRow` | 34 | **II=18** | 每行按 sub-bank 写入 |
| `resetOnlineMax` | 90 | 89 | 内循环延迟 88、II=18、trip count=4 |
| `executeInstruction` | 124～584 | 120～580 | 指令长度随功能变化 |
| `executeInstruction` 内循环 | 122～582 | **II=20** | 目标 II=1，trip count=5～28 |
| `readAccRow` | 52 | **II=36** | 两步请求/响应读回 |
| O 行读回循环 | 213 | **II=53** | 目标 II=1，trip count=4；包装模块延迟 215 |
| SA stage | 16 | II=1 | 局部流水化 |
| `accumulator_step` | 15 | II=1 | 4 路 accumulator lane |

预装载、指令执行、读回等外层调度远慢于局部 SA/Accumulator 的 II=1。当前请求延迟主要
受状态推进依赖、函数调用延迟及读写辅助循环限制，而不是单个算术模块的局部 II。

### 5.2 4×4 阵列内部并行性

SA stage 综合报告和 RTL 均显示 16 个独立 `peExp2PWL` 实例；实例表还包含 32 个
浮点乘法、20 个浮点减法和 16 个浮点加法运算模块。其 DSP 可对账为：

`16×7（exp2） + 32×3（乘法） + 20×2（减法） + 16×2（加法） = 280 DSP`

PE 主体被内联，因此不应称为“16 个命名 PE RTL 模块”，但 16 个指数实例、复制的浮点
运算资源、4×4 配置和完整数组分区共同证明 SA 内部形成了 4×4 空间展开。独立的
`accumulator_step` 还包含 4 个 lane 实例，共使用 60 DSP。

### 5.3 请求级数据通路复制

一个完整的逐步数据通路组合约使用：

`280 DSP（SA stage） + 60 DSP（Accumulator） = 340 DSP`

顶层层级和嵌套 RTL 显示它被物理复制了 7 次：

- `preloadQKV` 的 Q、K、V 三个循环各实例化 1 套，共 3 套；
- `resetOnlineMax`、`executeInstruction`、直接 L 读回各 1 套；
- O 行读回循环内还有 1 套 `readAccRow` 数据通路。

再加上 `executeInstruction` 中 `make_execution_plan_step` 的 3 DSP，顶层总量为：

`7×340 + 3 = 2,383 DSP`

这些复制资源服务于顺序执行的请求阶段，并不表示可以同时处理 7 个请求。这是当前面积
膨胀的主要原因，也是后续最值得检查的结构问题。

## 6. 资源与存储映射

### 6.1 顶层资源

| 资源 | 使用 | 器件可用 | 计算利用率 |
|---|---:|---:|---:|
| BRAM | 0 | 4,032 | 0.00% |
| DSP | 2,383 | 9,024 | 26.41% |
| FF | 787,654 | 2,607,360 | 30.21% |
| LUT | 839,191 | 1,303,680 | 64.37% |
| URAM | 0 | 960 | 0.00% |

总器件资源尚未超过容量，但相对单个 SLR 的估算为 DSP 79%、FF 90%、LUT
**193%**。设计无法装入一个 SLR，至少需要跨 SLR 放置；HLS 报告不能判断跨 SLR
布线后的拥塞和时序。

### 6.2 顶层层级

| 实例 | DSP | FF | LUT |
|---|---:|---:|---:|
| `preloadQKV` | 1,020 | 342,702 | 365,039 |
| `executeInstruction` | 343 | 108,914 | 115,769 |
| O 行读回循环 | 340 | 111,767 | 132,761 |
| 直接 `readAccRow` | 340 | 107,919 | 108,454 |
| `resetOnlineMax` | 340 | 106,460 | 107,364 |
| 顶层实例合计 | 2,383 | 777,762 | 829,387 |
| 顶层总计 | 2,383 | 787,654 | 839,191 |

顶层额外开销包括 9,476 FF 寄存器、9,375 LUT 多路器，以及少量 memory/expression
逻辑。资源几乎全部来自辅助路径中的大模块实例。

尽管设计包含内部 Scratchpad 和 Accumulator RAM，BRAM/URAM 仍为 0。源码对 bank、
sub-bank 和数据维度进行了 complete partition/reshape，当前构建把存储和访问网络主要
实现为寄存器、LUT 和多路选择逻辑；这也是面积和布线压力的重要来源。

## 7. 接口

导出 IP 只有 8 个物理端口：

| 端口组 | 方向 | 位宽 | 协议 | 说明 |
|---|---|---:|---|---|
| `ap_clk/ap_rst/ap_start/ap_done/ap_idle/ap_ready` | 混合 | 各 1 bit | `ap_ctrl_hs` | 块级事务控制 |
| `input_r` | 输入 | 773 bit | `ap_none` | 完整请求结构体 |
| `output_r` | 输出 | 660 bit | `ap_none` | 完整响应结构体 |

773-bit 输入由 5 个控制标志和 Q/K/V 三个 4×4×16-bit 数组组成：
`5 + 3×4×4×16 = 773`。660-bit 输出由 4 个状态标志、16-bit
`executed_steps`、4×32-bit L 和 4×4×32-bit O 组成：
`4 + 16 + 4×32 + 4×4×32 = 660`。

C++ 的 output 参数是引用并在元数据中标为 inout，但生成的物理 `output_r` 仅为输出，
因为函数不读取旧 output。两个数据端口均无独立握手：输入必须在事务读取期间保持稳定；
消费者应以 `ap_done` 确认 `output_r` 可采样，再检查 `request_done`、
`protocol_error` 和 `normalized`。日志明确提示 `output_r` 的 `ap_none` 接口
缺少关联 data-valid；当前协同仿真通过不能消除系统集成中的有效性风险。

## 8. 警告与风险

Solution 日志共有 **397 条 warning、0 条 error**。

| 告警代码 | 数量 | 风险说明 |
|---|---:|---|
| HLS 214-167 | 2 | `acc_ram_io.i.i` 和 `acc_ram_io.i.i135` 可能越界；当前无法确认是误报 |
| HLS 200-880 | 28 | reset、预装载、指令执行、读回等循环的状态/调用依赖使目标 II 失败 |
| HLS 200-875 | 64 | 48 条与 `writeSpadRow`、16 条与 `readAccRow` 调用延迟不兼容有关 |
| HLS 200-1995 | 16 | 编译初期设计达到 1,232,159 条 IR 指令，说明设计展开和综合复杂度很高 |
| HLS 214-366 | 6 | `std::array` 辅助函数因签名差异被复制，可能增加资源 |
| SYNCHK 200-23 | 1 | `accumulator.cpp:202` 的变量索引 range 选择可能影响 QoR |
| RTGEN 206-101 | 267 | 265 条寄存器上电初始化、1 条输出缺少 data-valid、1 条 RTL 重命名 |
| SYN 201-103 | 13 | 匿名命名空间函数名称合法化，主要影响命名而非功能 |

两条潜在越界告警优先级最高。C/RTL 测试只覆盖固定 4×4、非 causal、合法请求，不能
证明所有动态地址和 sub-bank 组合都安全；在定位其索引范围前，不应把它们判定为无害。

265 条寄存器上电初始化告警也不等于硬件上电状态可靠。虽然测试执行了算法 reset，
仍需在 Vivado/板级环境验证 `ap_rst`、静态状态、在线序列状态以及复位后的首个请求。

## 9. 当前合格性结论

| 检查项 | 结论 |
|---|---|
| C 仿真 | 通过：复位加两个 KV block，最终 L/O 与独立金标准匹配 |
| C 综合 | 完成 |
| 100 MHz HLS 时序估算 | **通过，但计算裕量仅 +0.100 ns** |
| 请求级固定 II | 不具备；顶层非流水化且事务延迟依赖请求类型 |
| 4×4 SA 内部空间展开 | 由 16 个 exp2 实例和复制算术资源确认 |
| 两 block 在线状态复用 | 当前 C/RTL 测试通过 |
| 潜在越界安全性 | **未确认：存在 2 条 HLS 214-167** |
| 资源可实现性 | **未确认：64.37% 总 LUT，单 SLR LUT 193%** |
| RTL 协同仿真 | 通过：3 个事务，总执行 4,072 周期 |
| IP 导出 | 完成 |
| Vivado 实现 / 板级验证 | 未执行/未提供 |

因此，本构建可以认定为“指定双 block 用例在 C/RTL 仿真中通过，综合和 IP 导出完成，
100 MHz HLS 估算时序通过”，但不能认定为可直接上板。潜在越界、请求级数据通路重复、
跨 SLR 资源压力和输出有效协议都需要继续处理或验证。

## 10. 后续工作

| 优先级 | 建议 |
|---|---|
| 高 | 定位两条 HLS 214-167 对应的 AccRAM 动态索引，证明完整取值范围或修正访问；增加边界地址/sub-bank 测试 |
| 高 | 将顺序请求阶段重构为共享的一套 `fsa_core_datapath_step` 硬件控制结构，避免 7 套 340-DSP 数据通路被辅助函数物理复制 |
| 高 | 在结构优化后重新综合；随后执行 Vivado 综合、布局布线，重点检查跨 SLR 拥塞、WNS 和实际资源 |
| 中 | 针对 `writeSpadRow` II=18、`readAccRow` II=36、执行循环 II=20 和输出循环 II=53 分别检查真实状态依赖与调用边界 |
| 中 | 为 `output_r` 制定明确的 `ap_done`/响应有效采样规范，必要时增加显式有效信号或流式接口适配层 |
| 中 | 增加 causal、非法续传、单/多 block、随机/特殊浮点、独立复位和多事务连续测试 |

## 11. 结果文件

以下路径均相对于 `build/fsa_core_request_build`：

- 顶层综合：`solution1/syn/report/fsa_core_request_top_csynth.rpt`
- 设计规模：`solution1/syn/report/csynth_design_size.rpt`
- 子模块和循环：`solution1/syn/report/`
- C 仿真：`solution1/csim/report/fsa_core_request_top_csim.log`
- RTL 协同仿真：`solution1/sim/report/fsa_core_request_top_cosim.rpt`
- 事务明细：`solution1/sim/report/verilog/result.transaction.rpt`
- Solution 日志：`solution1/solution1.log`
- IP 元数据：`solution1/impl/ip/component.xml`
- 导出 IP：`solution1/impl/export.zip`

