# `fsa_core_execute_top` 综合报告

## 1. 综合配置

本报告读取 `build/fsa_core_execute_build/solution1` 的现有产物编写，没有重新运行
Vitis HLS，也没有修改源码、测试、Tcl、时钟或接口。

| 项目 | 当前构建 |
|---|---|
| Vitis HLS | 2024.2，Build 5238294 |
| 顶层函数 | `fsa_core_execute_top` |
| Solution | `solution1`，Vivado IP Flow Target |
| 目标器件 | `xcvu37p_CIV-fsvh2892-2-e`（Virtex UltraScale+ HBM） |
| 时钟目标 | 10.00 ns，即 100 MHz |
| 时钟不确定度 | 2.70 ns（综合报告记录值；Tcl 未显式设置） |
| RTL / 协同仿真 | Verilog / XSIM |
| 顶层控制协议 | `ap_ctrl_hs` |
| 构建产物时间范围 | 2026-08-20 14:52:46 至 15:01:38（UTC+8） |

`run_hls.tcl` 加入了 10 个实现源文件和
`tests/hls/test_fsa_core_execute_top.cpp`，并依次启用 C 仿真、C 综合、Verilog
协同仿真和 IP 导出。当前本地源文件、测试和 Tcl 的修改时间均早于本次构建产物，
未发现“源码晚于构建”的情况；但报告内嵌的是 Linux 服务器路径
`/home/zhangchenxuan/FSA_HLS/...`，当前检查位置是 Windows 上的构建副本，因此无法仅靠
路径和时间戳证明两边内容逐字一致。以下结论以所给构建为准，源码版本对应性仍应视为
**无法完全确认，构建结果可能过期**。

## 2. 流程结果

| 阶段 | 结果 | 证据与边界 |
|---|---|---|
| C 仿真 | 通过 | `CSim done with 0 errors`，测试返回 `[PASS]` |
| C 综合 | 完成 | 生成顶层及 10 个子模块的 `csynth` 报告，日志无 `ERROR` |
| C/RTL 协同仿真 | 通过 | Verilog / XSIM，`C/RTL co-simulation finished: PASS` |
| IP 导出 | 完成 | 生成 `impl/export.zip`、IP ZIP 和 `component.xml` |
| Vivado 综合与实现 | 未执行 | IP 打包不等于 Vivado synthesis、place/route 或时序收敛 |
| 比特流与 FPGA 上板 | 未执行 | 未发现 bitstream、Hardware Manager、VIO 或 ILA 验证证据 |

## 3. 功能验证范围

测试平台通过综合顶层接口执行固定的 4×4 FlashAttention 用例：

1. 发送一次语义复位事务，检查 `instruction_done` 未错误拉高；
2. 用 12 次维护事务预装 Q、K 和转置后的 V，并检查 Scratchpad 写入 `ready`；
3. 连续执行 `LOAD_STATIONARY`、`ATTENTION_SCORE_COMPUTE`、
   `ATTENTION_VALUE_COMPUTE`、`ATTENTION_LSE_NORM_SCALE` 和
   `ATTENTION_LSE_NORM` 五条指令；
4. 检查每条指令的 `instruction_done`、`busy` 和 `executed_steps`；
5. 通过两拍请求/响应方式读回 L 和 O，检查 accRAM 的 `ready`、`response_valid`；
6. 使用测试平台独立计算的 `exp`、点积和归一化结果作 golden model，以 0.08 的容差
   比较全部 L/O 元素。

C 仿真与协同仿真都使用了这一份测试平台。它覆盖了一次无中断、合法地址、固定 4×4
数据下的完整数据路径，但没有覆盖非法/边界地址与 sub-bank 编号、主动背压、多组随机
矩阵、外部 `ap_rst` 的独立行为、非零旧 L/O 的累积路径或所有控制组合。因此“通过”只
说明当前向量与当前协议序列通过，不能消除综合器给出的越界和接口告警。

## 4. 时序与吞吐

### 4.1 100 MHz 时序估算

顶层 HLS 估算周期为 7.123 ns。按报告的时钟不确定度计算：

```text
有效时序预算 = 10.00 - 2.70 = 7.30 ns
HLS 估算裕量 = 7.30 - 7.123 = +0.177 ns
估算 Fmax     = 1000 / 7.123 = 140.39 MHz
```

因此当前 HLS 估算在 100 MHz 目标下只有 0.177 ns 正裕量。该数值是 HLS 调度估算，
不是布局布线后的 WNS；较小的估算裕量不能证明 Vivado 实现后仍能满足时序。

### 4.2 顶层与内部循环

| 对象 | Latency（cycles） | Interval / II | Pipeline |
|---|---:|---:|---|
| 顶层 `fsa_core_execute_top` | min 2，avg 77，max 589 | min 3，max 590 | no |
| 指令循环模块 | min 124，avg 344，max 584 | min 120，max 580 | auto-rewind loop |
| 循环 `VITIS_LOOP_121_1` | min 122，max 582；单迭代延迟 43 | **achieved II=20**，target II=1 | yes |

指令循环的 trip count 为 5～28。综合日志连续报告 5 次 `HLS 200-880`，指出
`acc_ram_io_i` 的读与 `accumulator_step` 调用之间存在 distance=1 的循环携带依赖，
最终循环只能达到 II=20。这个 II 是内部 logical step 的启动间隔，并不表示
`ap_ctrl_hs` 顶层可以每 20 拍接收一条新指令；顶层仍需等待整条事务完成并重新握手。

### 4.3 协同仿真事务

协同仿真共执行 28 次顶层调用，整体 latency 为 4 / 93 / 599 拍（min/avg/max），
interval 为 5 / 96 / 600 拍，总执行时间 2654 拍。平均值混合了复位、12 次预装、
五条指令和 10 次 accRAM 读请求/响应，不代表一条典型计算指令。

按 testbench 的确定调用顺序，transaction 13～17 对应五条指令：

| 指令 | logical steps | RTL latency | RTL interval | 100 MHz latency |
|---|---:|---:|---:|---:|
| `LOAD_STATIONARY` | 5 | 127 | 128 | 1.27 us |
| `ATTENTION_SCORE_COMPUTE` | 28 | 599 | 600 | 5.99 us |
| `ATTENTION_VALUE_COMPUTE` | 12 | 279 | 280 | 2.79 us |
| `ATTENTION_LSE_NORM_SCALE` | 17 | 379 | 380 | 3.79 us |
| `ATTENTION_LSE_NORM` | 5 | 139 | 140 | 1.39 us |

## 5. 关键子模块、循环与并行性

| 子模块 | Latency | II | DSP | FF | LUT |
|---|---:|---:|---:|---:|---:|
| SA 阶段 | 16 | 1 | 280 | 64,349 | 79,827 |
| `accumulator_step` | 15 | 1 | 60 | 38,428 | 23,950 |
| `make_execution_plan_step` | 3 | 1 | 5 | 2,242 | 6,024 |
| 单个 accumulator lane | 15 | 1 | 15 | 9,603 | 5,987 |
| `peExp2PWL` | 13 | 1 | 7 | 1,260 | 2,641 |

SA 子模块的 `II=1` 只表示它能每拍接收一组新的局部输入；顶层内部循环仍受状态和
accRAM 依赖限制为 II=20。

4×4 SA 的空间并行性有以下互相独立的证据：

- 源码对 4 行×4 列 PE 循环使用完整 `UNROLL`，并对 mesh、控制和数据数组完整分区；
- SA 综合层次和生成 Verilog 中存在 16 个 `peExp2PWL`、16 个浮点加法器和 32 个
  浮点乘法器实例；`pe_step` 本身已内联，因此不把它们误称为 16 个具名 `pe_step`
  RTL 实例；
- SA 阶段独占 280 DSP、64,349 FF 和 79,827 LUT，与单一共享 PE 的资源形态不符。

据此可确认当前实现形成了 **16 套 PE 等效运算资源**。Accumulator 没有内联为一个
共享实例：综合层次和 Verilog 均显示 4 个独立 `accumulator_lane_step`，每 lane 使用
15 DSP，`4 × 15 = 60 DSP` 与模块总 DSP 完全一致。顶层实例 DSP 也可核对为
`SA 280 + Accumulator 60 + 指令循环局部逻辑 5 = 345`。

## 6. 资源与存储映射

| 资源 | 使用量 | 器件可用量 | 计算占用率 |
|---|---:|---:|---:|
| BRAM_18K | 0 | 4,032 | 0.000% |
| DSP | 345 | 9,024 | 3.823% |
| FF | 117,561 | 2,607,360 | 4.509% |
| LUT | 124,473 | 1,303,680 | 9.548% |
| URAM | 0 | 960 | 0.000% |

HLS 还按单个 SLR 容量给出约 11% DSP、13% FF 和 28% LUT。当前小型存储合计 8 个
bank、5,760 bit，全部映射为 FF/LUT，没有使用 BRAM 或 URAM。资源总量低于整片器件
容量不代表布局一定容易；SA 的 79,827 LUT 和大量浮点运算集中在一个层次中，仍需
Vivado 实现检查 SLR 放置、布线和真实时序。

## 7. 接口

| RTL 端口 | 方向 | 位宽 | 协议 | 说明 |
|---|---|---:|---|---|
| `ap_clk` | in | 1 | `ap_ctrl_hs` | 时钟 |
| `ap_rst` | in | 1 | `ap_ctrl_hs` | 块级复位 |
| `ap_start` | in | 1 | `ap_ctrl_hs` | 启动事务 |
| `ap_done` | out | 1 | `ap_ctrl_hs` | 事务完成 |
| `ap_idle` | out | 1 | `ap_ctrl_hs` | 空闲状态 |
| `ap_ready` | out | 1 | `ap_ctrl_hs` | 可接受下一事务 |
| `input_r` | in | 382 | `ap_none` | `FsaCoreExecuteInput` 按 bit 紧凑聚合 |
| `output_r` | out | 292 | `ap_none` | `FsaCoreExecuteOutput` 按 bit 紧凑聚合 |

源级 `output` 参数是 C++ 引用，在元数据中标为 `inout`，但函数不读取旧输出值，最终
物理接口只有 292-bit 输出端口。`output_r` 没有独立的数据有效信号，日志对此给出
`RTGEN 206-101`；系统集成时应以 `ap_done` 作为整笔事务的采样边界，并继续按聚合输出
中的 `acc_dma_response_valid`、`acc_write_valid` 等字段解释维护响应。若下游不能遵守这
一契约，应重新评估接口协议，不能仅因本次自动协同仿真通过就忽略该风险。

## 8. 警告与风险

Solution 日志中共有 0 个错误、298 条 warning 记录。重点如下：

| 优先级 | 告警 | 次数 | 影响 |
|---|---|---:|---|
| 高 | `HLS 214-167` 可能越界访问 `acc_ram_io.i254` | 1 | 当前合法地址测试通过不能证明所有索引安全；签核前应定位生成变量对应的源级索引并补充边界测试 |
| 高 | `HLS 200-880` 指令循环 II 违例 | 5 | 真实 `acc_ram_io_i` / `accumulator_step` 依赖使 target II=1 未达成，最终 II=20 |
| 中 | `HLS 214-250/253` 分区或 reshape 指令被忽略 | 3 / 6 | `sp_ram.full_read_data`、`acc_ram.full_read_data` 和 `acc_ram.narrow_read_data` 的部分 pragma 未生效，可能限制端口并行性和 QoR |
| 中 | `RTGEN 206-101`：`output_r` 为无 valid 的 `ap_none` | 1 | 必须由块级完成握手和聚合字段确定输出采样时刻 |
| 中 | `RTGEN 206-101`：寄存器使用 power-on initialization | 268 | 设计依赖初始化语义；外部复位和目标平台上的启动行为仍需实现/板级验证 |
| 低 | `HLS 214-366` 标准库函数因签名差异被复制 | 6 | 可能增加资源，当前资源表已包含其影响 |
| 低 | `SYNCHK 200-23` 变量索引位段选择 | 1 | 可能降低 Accumulator QoR，但不是本次综合失败 |
| 低 | 设计规模、名称合法化/冲突改名 | 7 | 主要影响编译规模和 RTL 可读性，不改变当前功能结论 |

不能因 C 仿真或协同仿真通过而把越界、未生效 pragma 或初始化依赖判定为无害；当前
固定测试向量没有覆盖这些告警对应的全部路径。

## 9. 当前合格性结论

| 检查项 | 当前判断 |
|---|---|
| 当前固定 4×4 FA 用例功能 | 合格：C 仿真与 Verilog 协同仿真均通过 |
| 100 MHz HLS 估算 | 暂时满足：计算裕量 +0.177 ns，但尚无实现后 WNS |
| SA / Accumulator 空间并行性 | 合格：16 套 PE 等效资源、4 个独立 accumulator lane 有层次和 RTL 证据 |
| 顶层吞吐 | 未达 target II=1：内部 logical-step loop 实际 II=20 |
| 接口可集成性 | 有条件：`output_r` 需严格使用完成/有效契约 |
| 告警关闭 | 不合格：越界、忽略 pragma 和初始化依赖仍未关闭 |
| Vivado 实现与上板 | 未执行 |

综上，当前构建可认定为“**HLS 流程完整通过、固定功能用例通过、已成功导出 IP**”，
但还不能认定为“顶层 II=1”“Vivado 时序收敛”或“已上板通过”。由于时序估算裕量很小
且仍有一条潜在越界告警，建议在进入系统签核前完成告警定位和 Vivado 实现验证。

## 10. 后续工作

1. 优先定位 `acc_ram_io.i254` 的源级索引，增加地址、sub-bank 边界与非法输入测试；
2. 修正或删除未生效的分区/reshape pragma，重新综合后核对存储端口、II 和资源；
3. 分析 `acc_ram_io_i` 与 `accumulator_step` 的真实 RAW 依赖，若要提高吞吐，应以
   当前 achieved II=20 为基线，而不是以子模块 II=1 为基线；
4. 明确 `output_r` 的系统级采样协议，并验证外部 `ap_rst` 和上电初始化行为；
5. 运行 Vivado synthesis、place/route 和 timing analysis，记录 WNS/TNS、SLR 分布；
6. 修改后可在服务器项目根目录使用 `./run_hls.sh fsa_core_execute` 重新生成报告产物。

## 11. 结果文件

- 顶层综合：`build/fsa_core_execute_build/solution1/syn/report/fsa_core_execute_top_csynth.rpt`
- 指令循环：`build/fsa_core_execute_build/solution1/syn/report/fsa_core_execute_top_Pipeline_VITIS_LOOP_121_1_csynth.rpt`
- SA 层次：`build/fsa_core_execute_build/solution1/syn/report/p_anonymous_namespace_fsa_core_datapath_sa_stage_csynth.rpt`
- Accumulator 层次：`build/fsa_core_execute_build/solution1/syn/report/accumulator_step_csynth.rpt`
- C 仿真：`build/fsa_core_execute_build/solution1/csim/report/fsa_core_execute_top_csim.log`
- 协同仿真：`build/fsa_core_execute_build/solution1/sim/report/fsa_core_execute_top_cosim.rpt`
- 事务明细：`build/fsa_core_execute_build/solution1/sim/report/verilog/result.transaction.rpt`
- 完整日志：`build/fsa_core_execute_build/solution1/solution1.log`
- 导出 IP 元数据：`build/fsa_core_execute_build/solution1/impl/ip/component.xml`
- 导出包：`build/fsa_core_execute_build/solution1/impl/export.zip`
- HLS 流程：`hls/fsa_core_execute/run_hls.tcl`
- 测试平台：`tests/hls/test_fsa_core_execute_top.cpp`
