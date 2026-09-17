# `fsa_stream_request_top` 综合报告

本报告依据 `build/fsa_stream_request_build/solution1` 的最新 Vitis HLS 产物修改。
构建产物时间范围为 **2026-09-02 12:44:42 至 12:46:12（UTC+8）**。
本地 Tcl、顶层、核心源码和 testbench 的修改时间均早于本次构建，`hls.app`
记录的源文件、顶层和 4×4 编译参数也与当前工程一致，未发现构建过期迹象。
日志保留的是 Linux 服务器路径，本文使用当前 Windows 工作区中的对应副本。

## 1. 综合配置

| 项目 | 配置 |
|---|---|
| 工具 | Vitis HLS 2024.2，Build 5238294 |
| 顶层 | `fsa_stream_request_top` |
| Solution | `solution1`，Vivado IP Flow Target |
| 目标器件 | `xcvu37p_CIV-fsvh2892-2-e` |
| 阵列规模 | `SA_ROWS=4`，`SA_COLS=4` |
| 数据类型 | Q/K/V 为 FP16，在线状态及 L/O 为 FP32 |
| 顶层控制 | `ap_ctrl_hs` |
| 数据接口 | 聚合后的 `ap_none` 输入/输出 |
| 目标周期 | 10.000 ns，即 100 MHz |
| 时钟不确定度 | 2.700 ns |

Tcl 中 `RUN_CSIM=1`、`RUN_COSIM=0`、`EXPORT_IP=0`。本次只读取已有
产物，没有重新执行 HLS、Vivado 或板级流程。

### 最新构建对比

| 指标 | 上次构建 | 当前构建 | 变化 |
|---|---:|---:|---:|
| 产物结束时间 | 02:07:56 | 12:46:12 | 更新 |
| HLS 估算周期 | 7.833 ns | 7.296 ns | -0.537 ns |
| 计算时序裕量 | -0.533 ns | +0.004 ns | +0.537 ns |
| 顶层最大延迟 | 2,098,169 | 782 | -2,097,387 周期 |
| DSP | 399 | 217 | -182 |
| FF | 127,087 | 72,669 | -54,418 |
| LUT | 210,256 | 134,931 | -75,325 |
| warning | 200 | 93 | -107 |
| HLS 200-880 II 违例 | 114 | 0 | -114 |

主要变化来自把各计算 phase 统一到一个 `runFmaMesh`：旧构建的两组 16 路
PE 算术硬件已收敛为一组 16 路，持久状态引起的 II=16 也已消失。

## 2. 流程结果

| 阶段 | 结果 | 证据与说明 |
|---|---|---|
| C 仿真 | **通过** | `[PASS] test_fsa_stream_request_top`，0 error |
| C 综合 | **完成，HLS 估算边界满足** | 已生成综合报告及 Verilog/VHDL；无 `HLS 200-871` |
| RTL 协同仿真 | **未执行** | `RUN_COSIM=0`，未发现 cosim/transaction 报告 |
| IP 导出 | **未执行** | `EXPORT_IP=0`，未发现 `component.xml` 或导出 ZIP |
| Vivado 综合/实现 | **未执行** | `impl/verilog` 只是 HLS 生成的 RTL 副本 |
| FPGA 上板 | **未执行** | 未发现板级验证证据 |

## 3. 功能验证范围

实际加入 Tcl 的 testbench 是 `tests/hls/test_fsa_stream_request_top.cpp`。它直接
调用综合顶层并执行一个 4×4 请求：

- `reset=true`、`request_valid=true`、`initialize=true`、`finalize=true`；
- `active_keys=4`，Q/K/V 均为单位矩阵；
- `causal`、`query_base`、`key_base` 使用零初始化默认值；
- 仅断言 `request_done=true`、`protocol_error=false`、`normalized=true`。

因此 C 仿真仍只证明最小控制路径能够结束，**没有检查 L/O 数值，也没有独立金
标准**。未覆盖多 KV tile 在线状态、`initialize=false`、`finalize=false`、causal、
非满 `active_keys`、非法请求、reset-only、连续事务、特殊浮点值或 128×4 配置。
C 仿真观察到的最大 `hls::stream` 队列深度为 8；这只是软件仿真观测，不能代替
RTL FIFO 和死锁验证。

## 4. 时序与吞吐

### 4.1 100 MHz 时序检查

| 指标 | 数值 |
|---|---:|
| 目标周期 | 10.000 ns |
| 时钟不确定度 | 2.700 ns |
| 计算得到的有效预算 | 7.300 ns |
| HLS 估算周期 | 7.296 ns |
| 计算得到的时序裕量 | **+0.004 ns** |
| 按估算周期计算的原始 Fmax | 137.06 MHz |

计算关系为 `10.000 - 2.700 - 7.296 = +0.004 ns`。当前构建不再产生时序
约束警告，但正裕量只有 4 ps，只能称为 **HLS 估算边界满足**，不能认为时序稳健。
顶层最慢的已报告子模块是四个 O 更新循环之一，估算周期同为 7.296 ns。
HLS 估算周期不是布局布线后的 WNS，最终仍需 Vivado 实现确认。

### 4.2 顶层与 tile 延迟

| 层级 | 最小延迟 | 平均延迟 | 最大延迟 | 最小间隔 | 最大间隔 | 类型 |
|---|---:|---:|---:|---:|---:|---|
| `fsa_stream_request_top` | 2 | 150 | 782 | 3 | 783 | 非流水顶层 |
| `fsa_stream_request_run` | 1 | 未给出 | 781 | 1 | 781 | 非流水 |
| `stream_fsa_tile` | 658 | 未给出 | 776 | 658 | 776 | 非流水 |
| `runFmaMesh` | 87 | 未给出 | 94 | 20 | 27 | DATAFLOW |

顶层最小值包含 reset、无效请求或协议错误等提前返回路径。合法 tile 的综合静态
范围为 658–776 周期，即按 100 MHz 目标周期计算为 6.58–7.76 μs；顶层最坏
静态值为 782 周期。因为没有协同仿真事务报告，这些不是实际 RTL 事务测量值，
也不能据此确认连续请求吞吐。

### 4.3 关键循环

| 处理阶段 | 模块延迟 | 循环 II | 目标 II | 说明 |
|---|---:|---:|---:|---|
| Q 装入 resident bank | 6 周期 | 1 | 1 | trip count=4，内层完全展开 |
| K/V 本地装载 | 6 周期 | 1 | 1 | trip count=4，内层完全展开 |
| score max 与回写 | 9 周期 | 1 | 1 | trip count=4 |
| `macPeProcess` phase token | 16–23 周期 | **1** | 1 | wave count=1–8 |
| PWL 输出 bank 清零 | 6 周期 | 1 | 1 | trip count=4 |
| 每个 query 的 O 更新 | 17 周期 | 1 | 1 | 四个同类模块，trip count=4 |

单个 `macPeProcess` 包装延迟为 19–26 周期、间隔 19–26；其内部 phase token
循环已经达到目标 II=1。`runFmaMesh` 作为整体的调用间隔为 20–27 周期，因此
“内部 token II=1”仍不等于非流水顶层可每拍接收一个新请求。

## 5. 关键层级、循环与并行性

### 5.1 单组 4×4 FMA mesh

源码、综合层级、RTL 实例和资源加总共同确认当前硬件结构：

- `stream_fsa_tile` 只有 **1 个具名 `runFmaMesh` RTL 实例**；
- `runFmaMesh` 内有 **16 个 `macPeProcess` RTL 实例**，对应 4×4 空间展开；
- 每个 `macPeProcess` 占 12 DSP，内部 `peMacUnit` 被内联；
- `stream_pe_step` 已不在 Tcl 源文件列表和当前综合层级中；
- QK、减 max、缩放、8 段 PWL、rowsum 和 PV 六个 phase 顺序调用同一个
  `runFmaMesh`，由 phase-resident A/B bank 传递结果。

因此可以确认是“一组 16 路等效 PE FMA 资源”而不是六组并行 mesh。顶层 DSP
加总为：

`16×12 + 5 + 4×2 + 4×3 = 217 DSP`

其中 mesh 为 192 DSP，`accExp2PWL` 为 5 DSP，四个 FP32 加/加减单元共 8 DSP，
四个 FP32 乘法器共 12 DSP。资源方程与顶层 217 DSP 完全一致。

### 5.2 DATAFLOW 与 FIFO

`runFmaMesh` 包含左右/顶部 feed、16 个 PE 进程、底部 reduction collector、
lane collector 和右侧 drain。主 mesh FIFO 源码深度为 5，综合报告中的
`runFmaMesh` FIFO 合计占 3,449 FF 和 5,966 LUT。

工具自动增加了 18 个 start FIFO 深度，并建议 6 条 lane FIFO 增至 6–8。
同时仍有 DATAFLOW canonical-form 和 `feedMeshTop` auto-rewind 潜在死锁告警。
当前只有单事务 C 仿真、没有 RTL 协同仿真，因此所有 phase 和连续调用下的
stall/deadlock 安全性仍无法确认。

## 6. 资源与存储映射

### 6.1 顶层资源

| 资源 | 使用量 | 器件总量 | 计算占用率 |
|---|---:|---:|---:|
| BRAM_18K | 0 | 4,032 | 0.00% |
| DSP | 217 | 9,024 | 2.40% |
| FF | 72,669 | 2,607,360 | 2.79% |
| LUT | 134,931 | 1,303,680 | 10.35% |
| URAM | 0 | 960 | 0.00% |

相对单个 SLR，报告估算为 DSP 7%、FF 8%、LUT 31%，原始总量能够放入一个
SLR；实际布局、拥塞和时序仍需 Vivado 实现确认。

### 6.2 层级与存储

| 层级 | BRAM_18K | DSP | FF | LUT |
|---|---:|---:|---:|---:|
| 顶层总计 | 0 | 217 | 72,669 | 134,931 |
| `fsa_stream_request_run` | 0 | 217 | 71,813 | 134,917 |
| `stream_fsa_tile` | 0 | 217 | 70,288 | 134,060 |
| `runFmaMesh` | 0 | 192 | 50,925 | 76,424 |
| 16 个 `macPeProcess` 算术实例 | 0 | 192 | 46,627 | 68,405 |

在线状态的六个小型 RAM 合计 192 FF、198 LUT。resident bank、tile 缓存、完全
分割数组和小 FIFO 仍主要使用 FF/LUT，未映射为 BRAM/URAM；这解释了零
BRAM/URAM 和仍然较高的 13.5 万 LUT。

## 7. 接口

物理顶层接口与上次构建一致，共 8 个端口：

| 端口 | 方向 | 位宽 | 协议 | 含义 |
|---|---|---:|---|---|
| `ap_clk` | 输入 | 1 | `ap_ctrl_hs` | 时钟 |
| `ap_rst` | 输入 | 1 | `ap_ctrl_hs` | 同步高有效复位 |
| `ap_start` | 输入 | 1 | `ap_ctrl_hs` | 启动事务 |
| `ap_done` | 输出 | 1 | `ap_ctrl_hs` | 事务完成/输出有效窗口 |
| `ap_idle` | 输出 | 1 | `ap_ctrl_hs` | 空闲指示 |
| `ap_ready` | 输出 | 1 | `ap_ctrl_hs` | 可接受下一次启动 |
| `input_r` | 输入 | 853 | `ap_none` | 聚合的控制字段与 Q/K/V |
| `output_r` | 输出 | 660 | `ap_none` | 聚合的状态、L 和 O |

853 位输入由 5 个布尔控制位、16 位 `active_keys`、两个 32 位 base，以及
48 个 FP16 Q/K/V 元素组成。660 位输出由 4 个状态位、16 位 `executed_steps`、
4 个 FP32 L 和 16 个 FP32 O 元素组成。

`output_r` 没有独立 data-valid；外部逻辑必须使用 `ap_done` 判断输出有效窗口。
工具对此仍给出 `RTGEN 206-101`。该数据接口是并行宽总线，不是 AXI4、
AXI4-Lite 或 AXI-Stream。

## 8. 警告与风险

Solution 日志共有 **93 条 warning、0 条 error**，且不再有时序约束或 II 违例。

| 告警 | 数量 | 影响 |
|---|---:|---|
| SYN 201-103 | 41 | 匿名命名空间函数名合法化，主要影响可读性 |
| XFORM 203-561 | 21 | 工具把 phase 循环上界由 13 收窄到实际最大 8 |
| HLS 200-1020 | 18 | 自动增加 start FIFO 深度以改善性能/避免死锁 |
| HLS 200-1018 | 6 | 建议部分 lane FIFO 深度由 5 增至 6–8 |
| HLS 214-114、200-471 | 各 1 | `runFmaMesh` DATAFLOW 区域不是完全规范形式 |
| HLS 200-656 | 1 | `feedMeshTop` auto-rewind 在无 start propagation 情况下可能死锁 |
| HLS 200-1995 | 1 | 第一次展开/内联后设计含 114,957 条 IR 指令 |
| XFORM 203-631 | 1 | `runFmaMesh` 的匿名命名空间函数被重命名 |
| RTGEN 206-101 | 2 | `online_active` 依赖上电初始化；`output_r` 无独立 valid |

当前最高优先级风险已从 II 和明确时序失败转为 **仅 4 ps 的 HLS 时序裕量**、
DATAFLOW/FIFO 潜在死锁，以及测试不检查数值且没有 cosim。C 仿真通过不能消除
这些 RTL 级风险。

## 9. 当前合格性结论

| 检查项 | 结论 |
|---|---|
| 4×4 最小 C 仿真 | **通过，但只验证状态标志** |
| C 综合 | **完成** |
| 100 MHz HLS 时序估算 | **边界满足：+0.004 ns** |
| PE 空间并行 | **确认 1 个 mesh、16 路 PE 算术实例** |
| phase token II=1 | **达到：内部循环 II=1** |
| 顶层事务 II=1 | **未达到，也未要求：顶层为非流水 `ap_ctrl_hs`** |
| RTL 协同仿真 | **未执行** |
| IP 导出 | **未执行** |
| Vivado 实现/板级验证 | **未执行** |

新构建已经消除旧报告中的双份 PE 算术硬件和 II=16 问题，资源与静态延迟显著
下降，100 MHz HLS 估算也由失败变为临界通过。但当前验证仍不足以认定设计功能
正确、DATAFLOW 无死锁或最终实现满足 100 MHz。

## 10. 后续工作

| 优先级 | 建议 |
|---|---|
| 高 | 增加独立数值金标准，覆盖 causal、`active_keys=1..4`、多 tile 初始化/延续/结束、错误路径和连续事务 |
| 高 | 处理 DATAFLOW canonical-form、start propagation 和 lane FIFO 深度告警后执行 RTL 协同仿真，核对 stall/deadlock 与事务周期 |
| 高 | 在不改变器件、100 MHz 和 2.7 ns 不确定度的前提下继续增加时序裕量，避免只剩 4 ps |
| 中 | 完成 128×4 参数化编译/综合，核对一组 mesh 是否保持、资源是否可接受及内部循环 II |
| 中 | 仅在功能、时序和 cosim 合格后导出 IP，再用 Vivado 实现确认 WNS、拥塞和实际资源 |

如需在相同工程上复现，应使用 `./run_hls.sh fsa_stream_request`；本次报告没有执行该命令。

## 11. 结果文件

以下路径均相对于 `build/fsa_stream_request_build`：

- 顶层综合：`solution1/syn/report/fsa_stream_request_top_csynth.rpt`
- 顶层 XML：`solution1/syn/report/fsa_stream_request_top_csynth.xml`
- 设计规模：`solution1/syn/report/csynth_design_size.rpt`
- mesh 综合：`solution1/syn/report/p_anonymous_namespace_runFmaMesh_csynth.rpt`
- PE 进程综合：`solution1/syn/report/p_anonymous_namespace_macPeProcess_csynth.rpt`
- 其他子模块与循环：`solution1/syn/report/`
- C 仿真：`solution1/csim/report/fsa_stream_request_top_csim.log`
- Solution 日志：`solution1/solution1.log`
- 生成 RTL：`solution1/syn/verilog/`

本构建没有可引用的 cosim、transaction、`component.xml`、IP 导出 ZIP、Vivado
实现或板级结果文件。
