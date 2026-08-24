# `fsa_dma_top` 综合报告

## 1. 综合配置

本报告读取 `build/fsa_dma_build/solution1` 的现有产物更新，没有重新运行 Vitis HLS、
Vivado 或板级测试，也没有修改源码、测试平台、时钟、器件或接口。

| 项目 | 当前构建 |
|---|---|
| Vitis HLS | 2024.2，Build 5238294 |
| 综合顶层 | `fsa_dma_top` |
| Solution | `solution1`，Vivado IP Flow Target |
| 目标器件 | `xcvu37p_CIV-fsvh2892-2-e`（Virtex UltraScale+ HBM） |
| 构建内配置 | `SA_ROWS=4`，`SA_COLS=4`，`MAX_SEQUENCE_LENGTH=4096` |
| 数据格式 | FP16 Q/K/V，FP32 O |
| 时钟目标 | 10.00 ns，即 100 MHz |
| 时钟不确定度 | 2.70 ns |
| 有效组合逻辑预算 | 7.30 ns |
| 控制接口 | 32-bit AXI4-Lite |
| 数据接口 | 单个 64-bit AXI4 master `gmem` |
| 构建产物时间范围 | 2026-08-24 17:42:40 至 17:52:27（UTC+8） |

### 1.1 源码对应性

当前本地 DMA 顶层、请求核、`banked_sram.cpp`、`accumulator.cpp`、测试平台和 Tcl 的
修改时间均早于本次构建。C 仿真日志明确重新编译了上述源码，Solution 源文件清单、接口
和 PASS 标记也与本地版本一致，未发现构建过期迹象。产物中仍保存 Linux 服务器路径，
而当前检查位置是 Windows 构建副本，因此无法证明两端文件逐字节相同；以下结论以本次
生成产物为准。

## 2. 流程结果

| 阶段 | 状态 | 证据与限制 |
|---|---|---|
| C 仿真 | 通过 | 一次启动完成 `L=9`、4-token tile 的完整 9×4 O，0 error |
| C 综合 | 完成 | 顶层、请求核、单 core 和关键循环报告均存在 |
| RTL 协同仿真 | **通过** | xsim/Verilog，1 个事务，延迟和总执行周期均为 27,566 cycles |
| IP 导出 | **未执行** | Tcl 中 `EXPORT_IP=0`，当前 Solution 无 `component.xml` 和 `export.zip` |
| Vivado 综合/实现 | 未执行/未提供 | 没有实现后资源、WNS、跨 SLR 布线或实际 Fmax |
| FPGA 板级验证 | 未执行/未提供 | 没有真实 DDR/HBM、AXI-Lite 驱动或板上功能证据 |

本次有效验证范围是当前版本的 C 仿真、C 综合和单事务 RTL Co-sim。
本次没有执行 IP 导出、Vivado 综合/实现或板级验证。

## 3. 相对上一份报告构建的变化

上一份报告记录的是 2026-08-24 16:50:34 至 16:59:43 构建；当前构建采用相同
器件、时钟、4×4 配置、接口和测试。本版移除 `advanceDatapath` 的 latency 下限，
保留其 `II=20`和调度循环 `II=39`，并增加一个保留层级的 `registerDatapathInput`。

| 指标 | 上一份报告 | 当前构建 | 变化 |
|---|---:|---:|---:|
| BRAM_18K | 4 | 4 | 0 |
| DSP | 298 | 303 | +5（+1.68%） |
| FF | 102,179 | 105,648 | +3,469（+3.40%） |
| LUT | 114,126 | 128,957 | +14,831（+13.00%） |
| 等效完整 core 数 | **1** | **1** | 保持单 core 复用 |
| `HLS 214-167` 越界告警 | 1 | **0** | 已从最新日志消失 |
| `SYNCHK 200-23` 变量索引告警 | 1 | **0** | 已从最新日志消失 |
| HLS 估算周期 | 7.935 ns | **7.300 ns** | -0.635 ns，回到有效预算边界 |
| `advanceDatapath` 延迟 / Interval | 37 / 20 | 34 / 20 | 延迟 -3，Interval 不变 |
| 调度循环目标 / 实际 II | 37 / 38 | **39 / 39** | 当前目标已满足 |
| RTL Co-sim 延迟 | 26,922 cycles | 27,566 cycles | +644（+2.39%） |
| IP 导出 | 未执行 | 未执行 | 当前 Tcl 仍禁用导出 |

最新构建仍保持唯一一套计算数据通路，越界与变量索引告警仍未出现。
HLS 时序告警和 II 违例已消失，但 Co-sim 增加 644 cycles，且新增输入模块实际是
latency=0、0 FF 的纯连线层级，不是物理寄存器。

## 4. 功能验证范围

本次 Tcl 选择的唯一测试平台是 `tests/hls/test_fsa_dma_top.cpp`。C 仿真实际检查：

- `L=2×SA_COLS+1=9`，覆盖 3 个 query tile、3 个 KV tile 和最后一个非整 tile；
- Q、K、V 使用 `[sequence][head_dim]` row-major FP16 布局；
- 一次 `ap_start` 完成整个 `L×head_dim` attention 并写回 row-major FP32 O；
- `causal=false`，检查返回状态为 `OK(0)`；
- 使用独立循环计算完整 softmax 金标准，逐元素检查结果有限且绝对误差不超过 0.18；
- 在有效 O 区域后放置两个 canary，检查该用例没有越界写出。

RTL Co-sim 使用同一测试平台和 Verilog RTL，完成 1 个事务并通过；事务延迟为 27,566
cycles，即 275.66 us @ 100 MHz。由于只有一个事务，Co-sim 汇总中的 transaction
interval 为 `NA`，不能据此推导连续启动吞吐。

当前构建没有验证：

- `causal=true`；
- `sequence_length=0`、超过 4096 或其他非法输入；
- AXI 错误响应、长时间 backpressure、突发传输边界和真实 DDR/HBM 行为；
- 多次连续 start、auto-restart、软件寄存器/中断流程；
- 随机、极端、NaN/Inf 或更长序列输入；
- Vivado 综合/实现和 FPGA 板级运行。

## 5. 时序与吞吐

### 5.1 100 MHz HLS 时序估算

| 指标 | 当前值 |
|---|---:|
| 目标周期 | 10.000 ns |
| 时钟不确定度 | 2.700 ns |
| 有效预算（计算值） | 7.300 ns |
| 顶层 HLS 估算周期 | **7.300 ns** |
| 有效预算裕量（计算值） | **0.000 ns** |
| 由估算周期换算的频率（计算值） | 约 136.99 MHz |

`7.300 + 2.700 = 10.000 ns`，当前在 HLS 估算中恰好满足 100 MHz 时序约束，
日志也不再出现 `HLS 200-871`。但计算裕量为 **0 ns**，136.99 MHz 只是
`1000/7.300` 的原始估算换算值，不是布局布线后的 Fmax。

### 5.2 顶层理论延迟

| 层次 | 延迟最小 | 延迟最大 | Interval | 说明 |
|---|---:|---:|---:|---|
| `fsa_dma_top` | 2 | 13,088,292,106,249 | 3 至 13,088,292,106,250 | 非流水化 `ap_ctrl_hs` |
| `fsa_core_request_run` | 1 | 780,025 | 1 至 780,025 | 包含无效/空闲快速返回分支 |
| 请求调度循环 | 1,972 | 780,022 | 1,950 至 780,000 | trip count 50 至 20,000 |

顶层最大值约为 130,882.92 秒，即 36.36 小时 @ 100 MHz。这不是 `L=9` 的实测延迟，
而是 HLS 把两层最多 4096 次的 tile 循环、请求核 20,000 次调度上界和子模块延迟组合后
得到的极保守静态上界，不代表当前 `L=9` 用例。Co-sim 已测得该单事务的实际 RTL 延迟
为 **27,566 cycles（275.66 us @ 100 MHz）**；由于只执行一次，尚无连续事务 interval。

### 5.3 请求核局部调度

| 模块/循环 | 延迟 | Interval / II | 结论 |
|---|---:|---:|---|
| `registerDatapathInput` | **0** | II=1 | 保留独立层级，但是 0 FF/0 LUT 纯连线，不是物理寄存器 |
| 唯一 `advanceDatapath` | 34 | 20 | 流水模块，已满足目标 II=20 |
| SA stage | 16 | II=1 | 4×4 局部空间并行 |
| `accumulator_step` | 15 | Interval=16 | 4 个独立 lane 实例，每个 5 DSP、Interval=5 |
| `make_execution_plan_step` | 23 | Interval=24 | 请求控制计划生成 |
| 请求调度循环 | 60/iteration | **实际 II=39，目标 II=39** | 当前 II 约束已满足 |

单 core 复用解决了面积问题，但调度循环不能每周期推进一次 logical step。当前主要吞吐
限制已经从“11 套重复硬件”转为“共享数据通路的状态依赖和 II=39”。顶层仍是一次启动
完成整个序列的非流水化事务，不能每周期接收新的完整请求。

### 5.4 DMA 搬运

| 模块/循环 | 延迟 | Interval / II | 说明 |
|---|---:|---:|---|
| Q tile 加载包装 | 16 | Interval=5，循环 II=1 | trip count=4 |
| K/V tile 加载包装 | 26 | Interval=5，循环 II=1 | trip count=4，含两次行读取 |
| `dma_load_elem_row` | 9 | II=1 | 单行 FP16 解包 |
| `dma_store_acc_row` | 10 | Interval=3，内部 II=1 | 两个 64-bit FP32 word/行 |

综合层级中 Q 加载包装包含 1 个、K/V 加载包装包含 2 个轻量 `dma_load_elem_row` 实例，
但它们均为 0 DSP，每个约 71 FF、95 LUT，不是完整计算 core 的复制。四个外部数组仍共享
一个 `gmem` 主口；HLS 局部循环 II=1 不代表真实 DDR/HBM 在任意 backpressure 下都能
维持相同吞吐。

## 6. 单 core 实例与局部并行性

### 6.1 单 core 复用确认

请求调度循环的综合层级中只有：

- 1 个 `p_anonymous_namespace_advanceDatapath`，使用 300 DSP；
- 1 个 `make_execution_plan_step`，使用 3 DSP；
- 1 个 `registerDatapathInput`，使用 0 FF、0 LUT，只有组合连线；
- 没有 `advanceDatapath_1` 或第二套同类模块；
- Solution 日志中不再出现 `HLS 214-300` 或 `HLS 214-209` 克隆/ALLOCATION 告警。

资源可精确对账为：

```text
1 × 300 DSP（唯一完整数据通路） + 3 DSP（执行计划） = 303 DSP
```

综合层级、生成 RTL 中唯一一次 `advanceDatapath` 实例化以及 DSP 对账共同确认：当前版本
只有一套完整 core，预装载、指令执行、归一化和读回阶段均复用它。

### 6.2 4×4 core 内部并行性

唯一 `advanceDatapath` 内部资源为：

```text
280 DSP（4×4 SA stage） + 4 × 5 DSP（四个 Accumulator lane） = 300 DSP
```

这里的“单 core”指一套完整 4×4 数据通路，不是把 16 个 PE 串行化。SA stage 仍为
II=1；Accumulator 综合层级和 RTL 均有 4 个 `accumulator_lane_step` 实例，
每个使用 5 DSP，因此当前 4 列运算资源是空间独立的。PE 主体可能被内联，
因此不把它表述为 16 个命名 PE RTL 模块。

## 7. 资源与存储映射

### 7.1 顶层资源

| 资源 | 使用 | 器件可用 | 计算利用率 | 单 SLR 利用率（报告） |
|---|---:|---:|---:|---:|
| BRAM_18K | 4 | 4,032 | 0.10% | <1% |
| DSP | 303 | 9,024 | 3.36% | 10% |
| FF | 105,648 | 2,607,360 | 4.05% | 12% |
| LUT | 128,957 | 1,303,680 | 9.89% | 29% |
| URAM | 0 | 960 | 0.00% | 0% |

HLS 估算显示当前设计可以放入一个 SLR 的资源容量范围，不再需要仅因面积而强制跨 SLR。
但这仍是 HLS 综合估算，不包含 Vivado 实现后的互连、时钟和 AXI 基础设施误差。

### 7.2 顶层层级

| 顶层实例 | BRAM | DSP | FF | LUT |
|---|---:|---:|---:|---:|
| `fsa_core_request_run` | 0 | 303 | 101,430 | 124,404 |
| Q tile 加载包装 | 0 | 0 | 647 | 572 |
| K/V tile 加载包装 | 0 | 0 | 1,125 | 1,113 |
| `dma_store_acc_row` | 0 | 0 | 143 | 232 |
| AXI master 适配器 | 4 | 0 | 869 | 956 |
| AXI-Lite 控制 | 0 | 0 | 375 | 634 |
| 顶层总计 | 4 | 303 | 105,648 | 128,957 |

4 个 BRAM 仍来自 AXI master 适配器缓冲。唯一 core 内部的小型状态存储主要映射为 FF/LUT；
`advanceDatapath` 报告中的 Memory 项为 32 FF、33 LUT，BRAM/URAM 均为 0。相对上一份报告，
Accumulator 从 1 个 15-DSP 共享 lane 变为 4 个 5-DSP 独立 lane，其 LUT 从 6,920 增到
22,033，是顶层 LUT 增长的主要来源。

## 8. 接口

综合 RTL 共有 65 个物理端口：

| 接口 | 物理规模 | 关键参数 |
|---|---:|---|
| `s_axi_control` | 17 个信号 | 32-bit data，7-bit address，读写 |
| 时钟/复位/中断 | 3 个信号 | `ap_clk`、同步低有效 `ap_rst_n`、`interrupt` |
| `m_axi_gmem` | 45 个信号 | 64-bit address、64-bit data、1-bit ID，读写 |

Q、K、V、O 四个 64-bit 基地址共享同一个 `m_axi_gmem`。由当前生成的
`fsa_dma_top_control_s_axi.v` 可确认寄存器映射：

| 寄存器 | 地址 |
|---|---:|
| CTRL / GIER / IP_IER / IP_ISR | 0x00 / 0x04 / 0x08 / 0x0C |
| Q 基地址低/高 32 bit | 0x10 / 0x14 |
| K 基地址低/高 32 bit | 0x1C / 0x20 |
| V 基地址低/高 32 bit | 0x28 / 0x2C |
| O 基地址低/高 32 bit | 0x34 / 0x38 |
| `sequence_length` | 0x40 |
| `causal` | 0x48 |
| `status` / `status_ap_vld` | 0x50 / 0x54 |

软件应在设置 Q/K/V/O 基地址、`sequence_length` 和 `causal` 后启动 IP，并通过
`ap_done` 或中断判断事务结束；读取 `status` 时应同时遵守其 `ap_vld` 控制语义。

## 9. 警告与风险

Solution 日志共有 **349 条 warning、0 条 error**。

| 告警代码 | 数量 | 风险说明 |
|---|---:|---|
| HLS 214-366 | 5 | `std::array` 辅助函数因签名差异被复制，可能增加少量资源 |
| RTGEN 206-101 | 337 | 主要为寄存器上电初始化、RTL 重命名及 AXI 同步低有效复位提示 |
| SYN 201-103 | 7 | RTL 函数名称合法化，包括新增 `registerDatapathInput`，主要影响命名 |

最新日志中不再出现 `HLS 200-871`、`HLS 200-1016`、`HLS 200-875`、`HLS 200-880`、
`HLS 214-167`、`SYNCHK 200-23`、`HLS 214-300` 或 `HLS 214-209`。
源码现在用编译期静态 `row` 下标选择物理 SRAM 行，因此本次可以确认原越界告警已从综合
结果中消失。当前主要风险是 HLS 时序裕量为 0 ns，且 `registerDatapathInput`
实际是纯连线模块；Vivado 可能将该层级展平后重新暴露跨层组合路径。

## 10. 当前合格性结论

| 检查项 | 结论 |
|---|---|
| 当前版本 C 仿真 | 通过：`L=9`、非整 tile、一次启动完成 9×4 O |
| C 综合 | 完成 |
| 100 MHz HLS 时序估算 | **满足边界：估算周期 7.300 ns，有效预算裕量 0 ns** |
| 输入寄存器切割 | **未形成物理寄存器：该模块 latency=0、FF=0** |
| 单 core 复用 | **达到：1 套 300-DSP 数据通路，顶层共 303 DSP** |
| 4×4 SA 局部空间并行 | 保留：SA stage 280 DSP；Accumulator 为 4 个独立 5-DSP lane |
| 请求调度目标 II=39 | **达到：实际 II=39** |
| 潜在越界告警 | **已消失：无 HLS 214-167** |
| 资源容量 | HLS 估算：DSP 3.36%，FF 4.05%，LUT 9.89% |
| RTL Co-sim | **通过：Verilog/xsim，单事务 27,566 cycles** |
| IP 导出 | **未执行：`EXPORT_IP=0`，当前无导出产物** |
| Vivado实现 / 板级验证 | 未执行/未提供 |

当前构建确认单 core 复用仍生效，越界告警已经消失，C 仿真和单事务 RTL Co-sim 均通过，
且 HLS 时序与 II 约束不再报警。但时序裕量为 0 ns，新增层级不包含物理寄存器，
本次也未导出 IP，且缺少 Vivado 实现后的 WNS、真实资源与板级证据，
因此仍不能认定为可直接上板。

## 11. 后续工作

| 优先级 | 建议 |
|---|---|
| 高 | 若要可靠切断物理路径，需将 `registerDatapathInput` 改为有实际 FF 且 latency≥1 的时钟寄存器级 |
| 高 | 增加非法地址、sub-bank 边界和更长序列用例，补强越界修复的功能覆盖 |
| 高 | 执行 Vivado 综合和布局布线，检查 0 ns HLS 裕量下的 WNS、跨层路径、真实资源与单 SLR 放置 |
| 中 | 收紧请求调度循环的静态上界，使顶层 HLS 延迟估算更接近实际配置 |
| 暂缓 | 按当前安排暂不优化其他 II/latency；记录调度循环实际 II=39、Co-sim 27,566 cycles |
| 中 | 增加 causal、非法长度、AXI backpressure/error、多 start 和软件寄存器/中断测试 |

如需重新生成当前 Tcl 启用的 C 仿真、C 综合和 Co-sim，可使用
`./run_hls.sh fsa_dma`；当前 `EXPORT_IP=0`，该流程不导出 IP。该命令仅作为后续操作说明，
本次报告更新没有执行它。

## 12. 结果文件

以下路径均相对于 `build/fsa_dma_build`：

- 顶层综合：`solution1/syn/report/fsa_dma_top_csynth.rpt`
- 请求核层级：`solution1/syn/report/fsa_core_request_run_csynth.rpt`
- 单一调度循环：`solution1/syn/report/fsa_core_request_run_Pipeline_VITIS_LOOP_255_1_csynth.rpt`
- 输入连线层级：`solution1/syn/report/p_anonymous_namespace_registerDatapathInput_csynth.rpt`
- 唯一 core：`solution1/syn/report/p_anonymous_namespace_advanceDatapath_csynth.rpt`
- SA stage：`solution1/syn/report/p_anonymous_namespace_fsa_core_datapath_sa_stage_csynth.rpt`
- Accumulator：`solution1/syn/report/accumulator_step_csynth.rpt`
- C 仿真：`solution1/csim/report/fsa_dma_top_csim.log`
- Solution 日志：`solution1/solution1.log`
- AXI-Lite 寄存器 RTL：`solution1/syn/verilog/fsa_dma_top_control_s_axi.v`
- 单 core 调度 RTL：`solution1/syn/verilog/fsa_dma_top_fsa_core_request_run_Pipeline_VITIS_LOOP_255_1.v`
- 输入连线 RTL：`solution1/syn/verilog/fsa_dma_top_p_anonymous_namespace_registerDatapathInput.v`
- RTL Co-sim：`solution1/sim/report/fsa_dma_top_cosim.rpt`
- Co-sim 事务：`solution1/sim/report/verilog/result.transaction.rpt`

当前 Solution 未生成 `impl/ip/component.xml` 或 `impl/export.zip`。
