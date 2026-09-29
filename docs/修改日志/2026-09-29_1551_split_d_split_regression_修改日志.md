# Split-D 拆分回归 修改日志

## 1. 本次调用信息

- 开始时间：2026-09-29 15:51 +08:00
- 当前状态：进行中
- 本地仓库：`C:\Users\30130\Desktop\workstation\FlashAttention\FSA_HLS`
- 远端仓库：`FSA-FPGA-NM37-tailBox:/home/zhangchenxuan/FSA_HLS`
- 分支：`fsa_split_D`
- 起始commit：`8216f04e69a605039e954c062d619da5272f89e2`
- 目标模块：`fsa_stream_split_d`
- 工具链：远端 Vitis HLS 2024.2；每条 SSH 命令使用 Bash 并显式加载 `~/.bashrc`
- 最大迭代次数：未设置（用户要求先完成第一轮）
- 调用授权范围：本地修改与检查、普通commit/push、SSH、fast-forward pull、远端Vitis测试、证据读取、联网检索、本日志与根目录`PROJECT_CONTEXT.md`更新
- 额外授权记录：无

## 2. 期望结果与验收标准

### 用户期望

用户要求开始第一轮迭代，在远端测试 `4×4/16`，把本地修改推送上去测试。

### 可轮次范围

本轮被测对象是**交接前的纯文件拆分**（原约748行单文件拆为顶层、控制器、计算、DMA四个实现文件并新增内部接口头）。本轮只修改文件组织，不修改算法。

### 可验证标准

- [ ] 拆分后源码在 Vitis 中通过 CSim（`[PASS] fsa_stream_split_d: PE=4x4 HEAD_DIM=16`）
- [ ] CSynth 完成且顶层估算周期不超过 7.300ns
- [ ] 仍只有一套 16PE 阵列和一套 4 列 Accumulator，总 DSP 约 40
- [ ] QK/ROW_SUM 达到 II5，PWL/PV 达到 II1
- [ ] Q/K/V 加载的 `#pragma HLS INLINE` 在拆分后仍生效（不出现独立的 `loadElemTile`/`loadValueTile` RTL 模块）

### 本轮不包含

- RTL CoSim、IP 导出、Vivado 实现、板测
- `16×16/dim128` 参数

## 3. 初始状态

- 本地工作树：分支 `fsa_split_D`，HEAD `8216f04` 与 `origin/fsa_split_D` 一致。已跟踪文件有4处未提交修改（`PROJECT_CONTEXT.md`、上一轮修改日志、`hls/fsa_stream_split_d/run_hls.tcl`、`src/stream/split_d/fsa_stream_split_d.cpp`），另有5个未跟踪文件（4个拆分实现/头文件、交接文档）。
- 上一轮闭环：已用满2轮并结束，本轮为新的用户授权。
- 相关源码：拆分本体为 `src/stream/split_d/{fsa_stream_split_d,split_d_controller,split_d_compute,split_d_dma}.cpp` 与 `include/fsa/stream/split_d/split_d_internal.hpp`。
- 初始问题证据：拆分只经过 `git diff --check` 和符号唯一性检查（8个关键函数各只有1处定义），从未在 Vitis 中编译或综合；Q/K/V 加载的 `INLINE` 在其定义移入独立翻译单元后是否仍生效是本轮首要验证对象。
- 对照基线：`83b9b515` 的4×4/16（拆分前单文件）为 DSP40、BRAM8、FF29732、LUT124651、7.300ns、QK/ROW_SUM II5、PWL/PV II1、RTL CoSim 9/9通过。
- 环境限制：本地 Windows 缺 Vitis `ap_int.h`、WSL 启动被拒绝，**本地无法编译**，只能做结构检查；编译验证由远端 CSim 承担。
- 测试范围决定：`hls/fsa_stream_split_d/run_hls.tcl` 写死 `RUN_COSIM 1` 且不读环境变量，因此本轮用一个环境变量守卫的临时 Tcl（`/tmp/run_hls_regression.tcl`）把 `RUN_COSIM` 置0，仓库默认保持不变；被排除的阶段（CoSim）将在结论中标注为“未执行（本轮范围）”。

## 4. 迭代总览

| 轮次 | 被测commit | 修改摘要 | 本地测试 | 远端测试 | 验收状态 |
|---:|---|---|---|---|---|
| 1 | 待填 | 推送交接前的纯文件拆分，不改算法 | 结构检查 | 待测 | 待判定 |

## 5. 逐轮记录

### 第1轮

#### 修改前判断与计划

- 当前问题：拆分后的源码从未经过 Vitis 编译/综合，`INLINE` 是否仍生效未知。
- 证据：修改日志交接前源码整理一节仅记录 `git diff --check` 与符号唯一性检查通过；本地无 Vitis 头文件。
- 原因假设：把 `loadElemTile`/`loadValueTile` 的定义移到独立翻译单元后，即使保留 `#pragma HLS INLINE`，也可能因跨翻译单元而不被内联；若回退，会出现独立的加载子模块，重复此前已被证明会造成 RTL 数据错位的结构。
- 本轮计划：不改算法，只推送拆分本体，在远端以默认 `4×4/16` 跑 CSim + CSynth，逐项对照拆分前基线（DSP40、7.300ns、II5/II1、单阵列、加载内联）。

#### 实际修改

- 本轮无算法或测试代码修改；被测对象是交接前已完成、尚未提交的纯文件拆分。
- 为把测试范围收窄到 CSim + CSynth，使用临时 `/tmp/run_hls_regression.tcl`：内容为仓库 Tcl 把第2行改为 `set RUN_COSIM 0`，因此只会执行 `csim_design` 与 `csynth_design`。仓库默认仍是 `RUN_COSIM 1`，**不进入被测 commit**。
- 与计划的偏差：无。

#### 修改后本地测试

| 命令 | 结果/退出码 | 关键证据 |
|---|---|---|
| `git diff --check` | PASS，code 0 | 无空白错误 |
| 符号唯一性检查 | PASS | `runPeArray`、`runAccumulatorColumns`、`loadElemTile`、`loadValueTile`、`stagePeArrayResult`、`stageAccumulatorResult`、`runController`、`run` 各只有1处定义 |
| Windows C++ 编译检查 | 未执行 | Windows 缺 `ap_int.h`；WSL 启动返回 `E_ACCESSDENIED`，由远端 Vitis CSim 承担编译检查 |

#### 修改后远端测试

- 被测commit：待填
- 结果：待填

#### 本轮结论与下一步

- 待填
