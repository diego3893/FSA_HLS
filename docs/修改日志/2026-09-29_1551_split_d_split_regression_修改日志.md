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
| 1 | `2c94ef1` | 推送交接前的纯文件拆分，不改算法 | 结构检查通过；本地无法编译 | 4×4/16 CSim PASS、CSynth PASS（约6分47秒） | 通过（本轮范围，CoSim未执行） |

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

- 被测commit：`2c94ef1289050f3a0116d759200dfe9057e6526a`（远端HEAD经`git rev-parse HEAD`核对一致）
- 推送：`8216f04..2c94ef1` 到 `origin/fsa_split_D`
- `.bashrc`加载：已确认。远端`~/.bashrc`第119-122行加载 `/opt/Xilinx_2024.2/{Vivado,Vitis,Vitis_HLS}/2024.2/settings64.sh`；必须用**交互式** bash（`bash -ic`）才会读取`.bashrc`，非交互`bash -lc`不读，因此本轮统一用交互式方式加载
- 拉取冲突处理：无。远端tracked工作树干净，`git pull --ff-only`从`8216f04`快进到`2c94ef1`；原有未跟踪 `evidence/`、`logs/`、Vivado日志未触碰
- 环境与参数：VU37P `xcvu37p_CIV-fsvh2892-2-e`、4×4 PE、HEAD_DIM=16、10ns时钟、2.7ns uncertainty、Vitis HLS 2024.2
- 命令：`./run_hls.sh fsa_stream_split_d`（仓库入口，由用户指明），范围由临时`/tmp/run_hls_regression.tcl`（`set RUN_COSIM 0`）限定为 CSim + CSynth
- 开始/结束时间：2026-09-29 16:02:39 至 16:09:26 +08:00，约6分47秒
- 结果与退出码：PASS，code 0
- 关键指标（与拆分前基线逐项对照）：
  - CSim：`[PASS] fsa_stream_split_d: PE=4x4 HEAD_DIM=16 DIM_BLOCKS=4`，重复3次
  - 顶层：Target 10.00ns / Estimated **7.300ns** / Uncertainty 2.70ns；BRAM **8**、DSP **40**、FF **29732**、LUT **124651**、URAM 0
  - 顶层最坏延迟 427162627 cycles（与拆分前相同）
  - `runPeArray`：DSP16、latency4、II1、7.054ns；`runAccumulatorColumns`：DSP8、latency7、II1、7.299ns；`stagePeArrayResult`/`stageAccumulatorResult`：II1 latency1、DSP0
  - RTL循环实测：QK(`VITIS_LOOP_168_6_169_7`) achieved 5 / target 5；ROW_SUM(`VITIS_LOOP_364_33`) achieved 5 / target 5；PWL(`VITIS_LOOP_321_26`) achieved 1 / target 1；PV(`VITIS_LOOP_408_38`) achieved 1 / target 1（trip count 80，与“8个feature上下文 × 10个操作”一致）
  - 加载内联：`syn/report/`下**无**`loadElemTile`/`loadValueTile`报告，生成的Verilog中**无**任何load子模块，确认跨翻译单元的强制`INLINE`仍然生效
  - 拆分前遗留的`peArrayTask`/`accumulatorTask`/`KPN`模块名在新报告中均已消失
- 证据路径：`hls/fsa_stream_split_d/fsa_stream_split_d_build/solution1/syn/report/fsa_stream_split_d_csynth.rpt`、`runPeArray_csynth.rpt`、`runAccumulatorColumns_csynth.rpt`、`runController_Pipeline_VITIS_LOOP_{168_6_VITIS_LOOP_169_7,364_33,321_26,408_38}_csynth.rpt`，以及`/tmp/splitd_r1_regression.log`
- 未执行阶段：RTL CoSim（本轮范围之外）、IP导出、Vivado实现、板测

#### 本轮结论与下一步

- 已解决的问题：**拆分未造成任何回退**。Q/K/V加载的强制内联在定义移入独立翻译单元后仍然生效（无独立加载模块、无对应子报告），这消除了拆分前最主要的未知风险。
- 仍存在的问题：拆分后的RTL数值行为尚未用CoSim验证；本轮只证明到CSynth层面。
- 验收标准状态：本轮6项标准全部满足（CSim通过、CSynth完成、7.300ns、单阵列DSP40、QK/ROW_SUM II5与PWL/PV II1、加载内联仍生效）。CoSim明确不在本轮范围，记为未执行。
- 失败分析：本轮无失败。
- 下一轮修改：由用户决定。可选方向：(a) 以同一`2c94ef1`运行一次4×4 RTL CoSim，确认拆分未改变RTL数据；(b) 直接进入16×16可流水PE bank重构（交接文档第8节）。
- 本轮闭环状态：已完成。

### 第2轮（未开始）

- 状态：用户要求每轮结束暂停汇报，本轮结束后等待用户决定下一轮范围，尚未启动。

## 6. 调用结束总结

- 结束时间：2026-09-29 16:20 +08:00
- 结束原因：第1轮验收标准全部满足且用户未设最大轮数；按“每轮结束暂停汇报”的要求停在轮边界，等待用户决定是否继续及下一轮范围
- 已完成闭环迭代：1（未设上限）
- 未完成迭代：无
- 最终被测代码commit：`2c94ef1289050f3a0116d759200dfe9057e6526a`
- 最终日志commit：待本轮日志更新后提交（本文件更新时该commit尚未产生）
- 验收结果：4×4/16拆分回归通过——CSim PASS、CSynth PASS、7.300ns、DSP40/BRAM8/FF29732/LUT124651、QK/ROW_SUM II5、PWL/PV II1、Q/K/V加载内联保持单套16PE阵列
- 仍未解决：拆分后RTL CoSim数据验收；16×16/128的PE入口流水与目标II
- 建议下一步：见“下一轮修改”
- 独立最终报告：未要求

## 7. 本轮过程中的环境问题与更正

- 传输脚本曾带CRLF行尾，导致远端`TOOL`变量尾部含`\r`、`$TOOL/bin`加入PATH无效并使`cp`找不到文件；已改为传输时`tr -d '\r'`，远端脚本现为纯LF。
- 一次脚本使用 `/opt/Xilinx_2024.2/Vitis_HLS/2024.2/include/ap_int.h` 失败：该目录不存在，`ap_int.h`与`ap_fixed.h`位于 `/opt/Xilinx_2024.2/Vitis/2024.2/include/`。
- 更正：本轮**没有**向仓库添加任何`ap_int.h`/`ap_fixed.h`。经核查仓库`include/`下只有`fsa`，而CSim在仅加载`.bashrc`的正常环境下即可找到这些头文件，因此该复制既无必要也未发生；`.gitignore`中只有一条无关的 `/third_party/vitis_hls/include/`。
- 更正：此前“远端工具链不可用（`TOOL=NONE`）”的结论是探针引号错误造成的假象；实际用交互式bash加载`.bashrc`后`vitis-run`/`vitis_hls`均可正常调用。
