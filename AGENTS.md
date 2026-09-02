# FSA-HLS 项目协作说明

## 适用范围和当前架构

- 本文件适用于 `FSA-HLS` 仓库中的全部文件；用户在当前对话中的明确要求优先。
- `FSA-main` 的 Chisel 源码和 FSA 论文用于核对功能、数据移动和架构意图；除非用户明确要求，否则不要修改参考项目。
- 当前综合主路径是 `fsa_dma_top -> fsa_stream_request_run -> stream_array/runFmaMesh`：DMA 使用 load/compute/store `DATAFLOW`；计算核用一组定长-token FMA mesh 顺序执行 QK、减 max、缩放、PWL、rowsum 和 PV。
- `stream_pe_step` 保留为独立功能参考和单元测试对象，但不在 `fsa_stream_request`/`fsa_dma` 的当前综合主路径中。
- 原 `fsa_core_request_run`、`fsa_core_datapath_step` 及其 `current/next` 模型继续作为功能参考，不应为了优化路径而删除或无意改变其语义。
- 当前版本保持在线 softmax、causal mask、`active_keys`、非整 tile、最终归一化和现有 DMA 外部接口。Split-D、controller 指令重叠、AXI bundle 拆分不属于默认修改范围，除非用户明确要求。
- `accumulator_pipeline` 是独立实验实现，当前综合主路径不使用；未经单独验证不要接入主路径。

## 与用户沟通

- 默认使用中文，表达简明、通俗，按只学过本科基础课程的读者水平解释。
- 用户未要求修改文件时，只检查并给出结论；用户要求实现或修复时，可直接修改并进行与风险相称的验证。
- 不擅自改变阵列规模、开发板型号、时钟目标、顶层协议或数据位宽。
- 必须区分 C++ 测试、C 仿真、C 综合、RTL 协同仿真、IP 导出、Vivado 实现和上板测试；只完成其中一部分时准确说明范围。

## 目录职责

- `include/fsa/`：公共配置、类型、控制信号和核心接口。
- `include/fsa/hls/`：可综合顶层函数接口。
- `src/core/`：算术、旧逐拍参考模型和新 stream 核心实现。
- `src/hls/`：Vitis HLS 顶层包装、外部接口和顶层 `DATAFLOW`。
- `tests/`：核心 C++ 测试；`tests/hls/`：必须调用对应 HLS 顶层的 testbench。
- `hls/<module>/run_hls.tcl`：单模块 C 仿真、综合、协同仿真和导出流程。
- `run_test.ps1`：本地 GCC 测试入口；`run_hls.sh`：服务器/Vitis HLS 模块入口。
- `docs/`：架构方案、执行说明和基于实际构建产物的综合报告。
- `build/`、`hls/*/build/`、`hls/*/*_build/`：工具生成物，通常不提交 Git。

## C++、数值和注释约定

- 公共名称尽量与 Chisel/FSA 概念对应；核心类型和函数放在 `namespace fsa` 中。
- 头文件保护宏只使用文件名，例如 `PE_TOP_HPP`；不使用 `#pragma once`。
- 保持现有排版，不对无关代码做大范围格式化。新增公共结构体、字段和函数要说明硬件含义、数据来源和去向。
- `cvtAtoE` 是 FP32 到 FP16 的数值转换；`viewEasA`、`viewAasE` 是位模式装入/取出，不能互换。
- FP16 位模式装在 `acc_t` 载体中时，测试必须先用 `viewAasE` 解包，不能把载体本身直接与 FP32 数值比较。
- 默认使用 C++ testbench，不额外引入 Python，除非 Python 明显更合适或用户明确要求。

## 旧逐拍模型与新 stream 模型

- 旧 `step` 模型必须只读 `current`、只写 `next`，并在 step 结束后统一提交；不要在 step 中途写回 `current`。
- 上述 `current/next` 约束只适用于旧逐拍参考模型。新高吞吐路径使用完全分割的 phase-resident bank；单个 phase 内 PE 只读 resident、结果经 stream 提交到下一 bank，不能重新引入逐拍整阵列状态复制。
- stream token 必须携带所需的 `valid/op/tag/last/masked` 信息。所有 DATAFLOW 生产者和消费者必须具有可证明的固定 token 数或明确终止 token；bubble 也要传递，不能因分支少写 FIFO。
- QK 产生的 S、减 max 后的 N、PWL 后的 P、rowsum 和 PV 应沿 FSA 阵列路径移动；不要把完整 S/P tile 写入通用 SRAM 后交给独立 softmax/PV 核。
- 每个 PE 的普通 MAC、减 max、缩放和 PWL 以复用一条 FMA 流水线为目标。空间展开仍应保留每个 PE 的独立实例，不能把整个阵列错误限制成一条共享 FMA。
- CMP 的逐 score 最大值更新应使用 compare/mux；`oldMax-newMax` 只在 tile 边界计算。
- 在线状态必须保持 `m/l/O` 语义：跨 KV tile rescale，只有 `finalize` 后才计算最终 `O/l`。

## HLS 编写和顶层维护约定

- 顶层包装函数负责接口、复位、输入映射、核心调用、输出映射和必要的跨事务状态。
- **新增 HLS 顶层时，必须同时新增或修改以下内容：**

  1. `include/fsa/hls/<top>.hpp`；
  2. `src/hls/<top>.cpp`；
  3. `tests/hls/test_<top>.cpp`；
  4. `hls/<module>/run_hls.tcl`；
  5. 根目录 `run_hls.sh` 的模块白名单、交互提示和用法提示，使 `./run_hls.sh <module>` 能运行新顶层。
- 新建或修改 `run_hls.tcl` 时，默认运行 C 仿真和 C 综合；`RUN_COSIM=0`、`EXPORT_IP=0`。只有用户明确要求时才启用协同仿真或 IP 导出。
- 保持目标器件 `xcvu37p_CIV-fsvh2892-2-e` 和 100 MHz 时钟；未经允许不能靠放宽时钟、更换器件或减小时钟不确定度掩盖时序问题。
- 顶层控制协议默认保持 `ap_ctrl_hs`；需要改为 free-running/chain 等协议时，先说明接口影响。
- PE/CMP 空间阵列优先用 `UNROLL`，独立生产者/消费者用 `DATAFLOW` 和 `hls::stream`；不要用空的包装函数冒充寄存器边界。
- 并行访问数组必须按实际访问维度 `ARRAY_PARTITION/RESHAPE`。避免把未使用的 `SA_ROWS x SA_ROWS` K/V 数据完全展开；优化路径只缓冲实际 tile 行。
- FIFO 深度必须结合 token 生产/消费距离和综合/cosim stall 决定；C++ 仿真的最大队列长度不等于 RTL FIFO 的最终合理深度。
- 修改 pragma 或算术结构后，必须在综合报告中核对 FMA/DSP 实例数、循环 II、延迟、FIFO stall、LUT/FF/BRAM/URAM 和估算时钟。源码中只有一个调用点不等于综合后一定只有一个硬件实例。

## 测试约定

- HLS testbench 必须调用对应顶层，不能绕过顶层只测内部函数。
- 金标准尽量独立计算；浮点比较使用与 FP16/PWL 误差相符的容差，位模式功能直接比较位模式。
- 修改 stream 路径时至少覆盖：单/多 KV tile、initialize/finalize、causal、跨 tile causal、`active_keys` 边界、非整 tile、reset、连续 query block 和新旧核 tile 边界对照。
- 修改公共算术、token 或 PE/CMP 后，先跑直接测试，再跑 `test_fsa_stream_request_top`、`test_fsa_stream_vs_legacy` 和 `test_fsa_dma_top` 等上层回归。
- 默认 4x4 配置用于快速回归；影响参数化代码时还要至少编译或运行一次目标 128x4 配置。
- 本地 GCC 使用 `.vscode/c_cpp_properties.json` 指向的 `third_party/vitis_hls/include`。若机器没有 `vitis-run`/`vitis_hls`，只能报告 C++ 测试结果，不能声称完成 HLS 综合。
- 服务器/Vitis 环境统一使用 `./run_hls.sh <module>`；新增模块必须按上一节同步更新该脚本。

## 综合报告约定

- 报告必须来自当前源码对应的最新构建；无法确认版本一致时注明“构建结果可能过期”。
- 至少记录 C 仿真、C 综合、协同仿真、IP 导出是否执行，以及目标/估算时钟、延迟/II、接口、层次和资源。
- 判断 PE 并行度和 FMA 复用时必须同时检查 RTL 层次、operator/instance 报告和资源，不能只看顶层 II 或 C++ 写法。
- `docs/` 中的报告保持简洁，记录结论、证据、未验证项和下一步。

## 修改和 Git 安全

- 默认不创建/切换分支、不提交、不推送，也不覆盖用户已有修改；只有用户明确要求时才执行相应 Git 操作。
- 可以使用 `git status`、`git diff` 等只读命令检查工作区，并提醒用户存在未跟踪或未提交文件。
