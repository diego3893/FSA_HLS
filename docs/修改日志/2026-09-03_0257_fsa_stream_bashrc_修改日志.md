# fsa_stream 加载 bashrc 远端迭代修改日志

## 1. 本次调用信息

- 开始时间：2026-09-03 02:57 +08:00
- 当前状态：达到要求
- 本地仓库：`C:\Users\30130\Desktop\workstation\FlashAttention\FSA_HLS`
- 远端仓库：`FSA-FPGA-NM37-tailBox:/home/zhangchenxuan/FSA_HLS`
- 分支：`perf/fsa-streaming-core-v1`
- 起始commit：`125c747462e92470388b6ae66ef8a2ffa7977757`
- 目标模块：`fsa_stream_request`（当前流式 `fsa_core` 综合主路径）
- 工具链：Vitis HLS 2024.2；所有远端命令必须使用交互式 Bash 加载 `~/.bashrc`
- 最大迭代次数：2

## 2. 期望结果与验收标准

### 用户期望

在每次远端执行中务必加载 `~/.bashrc`，重新完成最多两轮远端迭代。目标仍为 II=1 的流式 FSA 核，包含 4x4 PE、4 个 CMP、Input/Output Delayer 和 4 路 Accumulator；使用完整 9x4 Q/K/V 测试验证功能和结构。

### 可验证标准

- [x] 本地测试和远端 CSim 的完整 9x4 Q/K/V 结果正确。
- [x] 4x4、VU37P、100 MHz 配置下 C synthesis 成功。
- [x] PE 核心 token 处理循环 II=1；顶层事务吞吐若无法由静态报告直接给出，记录 DATAFLOW 子模块 II 与 CoSim 间隔证据。
- [x] 综合层次包含 16 个 PE、4 个 CMP、Input/Output Delayer 和 4 路独立 Accumulator。
- [x] RTL CoSim 通过，无死锁，并记录 latency/interval。
- [x] 记录估算时钟、资源和警告；未改变器件、时钟、位宽或接口。

## 3. 初始状态

- 本地工作树：分支与 origin 同步；无暂存或已跟踪未提交修改。保留无关未跟踪文件 `docs/fsa_stream_request综合报告.md` 和目录 `skills/fsa-hls-remote-iteration/`。
- 远端预检：用 `bash -ic` 加载 `~/.bashrc` 后确认 Vitis 路径为 `/opt/Xilinx_2024.2/Vitis/2024.2/bin/vitis-run`。远端初始 HEAD 为 `276422f`，唯一 tracked 状态是 `D run_hls.sh`；按用户授权删除该阻塞文件、执行 `git pull --ff-only`，再从新 HEAD 恢复脚本，最终精确同步到 `27ba02aa44ac1db71419c0f05f4fb983c52c0a8b` 且 tracked 工作树干净。
- 相关源码和既有测试：设计源码仍对应 `dfb6940`；上一调用对相同源码执行的四项本地回归均通过，本次将至少重跑直接流式顶层测试。
- 初始问题证据：旧综合只形成一条 Accumulator lane，且没有当前代码的 CoSim；当前显式 4-lane 修改尚无新综合证据。上一远端自动执行未加载 `.bashrc`，导致代理环境缺失和 GitHub:443 超时。

## 4. 迭代总览

| 轮次 | 被测commit | 修改摘要 | 本地测试 | 远端测试 | 验收状态 |
|---:|---|---|---|---|---|
| 1 | `27ba02aa44ac1db71419c0f05f4fb983c52c0a8b` | 加载 `.bashrc`，同步并验证现有流式结构 | PASS | CSim/CSynth/CoSim PASS | 通过 |

## 5. 逐轮记录

### 第1轮

#### 修改前判断与计划

- 当前问题：当前源码的 II、4 路 Accumulator 和 CoSim 尚无对应的远端报告；自动 SSH 环境此前缺失 `.bashrc` 配置。
- 证据：用户交互终端中的 `curl` 出现代理 CONNECT 并成功，手动 `git pull` 成功；自动非交互 Shell 直连 GitHub:443 超时。
- 原因假设：使用 `bash -ic` 强制加载 `.bashrc` 后，GitHub 代理、Vitis PATH 和许可证配置会与用户手动终端一致。
- 本轮计划：运行本地流式顶层测试；提交并推送本日志；用加载 `.bashrc` 的远端 Shell 预检并安全处理唯一可能的 `run_hls.sh` 阻塞，拉取精确提交；运行 CSim、CSynth、CoSim；分析层次、循环、时序、资源和 warning。

#### 实际修改

- `docs/修改日志/2026-09-03_0257_fsa_stream_bashrc_修改日志.md`：新建本次调用日志。
- 设计源码：远端测试前无新增修改。
- 远端环境处理：所有命令使用 `bash -ic`；删除阻塞的 `run_hls.sh`，pull 后使用 `git restore --source=HEAD -- run_hls.sh` 恢复版本库内容。
- 与计划的偏差：没有设计源码修改；第1轮直接验证起始设计并达到范围内验收标准。

#### 修改后本地测试

| 命令 | 结果/退出码 | 关键证据 |
|---|---|---|
| `.\run_test.ps1 fsa_stream_request_top` | PASS，0 | reset + 9 requests、完整 9x4 Q/K/V；max L error=0.00654554，max O error=0.00217265；stream 最大深度 8。 |
| 上一调用的 `fsa_stream_vs_legacy`、`accumulator_top`、`fsa_dma_top` | PASS，0（复用相同源码证据） | legacy tile boundary、Accumulator 全操作、non-causal/causal 9x4 DMA 均通过；本次未重复执行。 |

#### 修改后远端测试

- 被测commit：`27ba02aa44ac1db71419c0f05f4fb983c52c0a8b`；其中设计源码对应 `dfb694052b632f90492d9a1464efc29fe44cb25a`，后续提交仅更新远端迭代日志。
- 环境与参数：Vitis HLS 2024.2 build 5238294；`FSA_SA_ROWS=4`、`FSA_SA_COLS=4`、`xcvu37p_CIV-fsvh2892-2-e`、目标周期 10 ns、不确定度 2.7 ns；`RUN_CSIM=1 RUN_COSIM=1 EXPORT_IP=0`。
- 命令：`bash -ic '... RUN_CSIM=1 RUN_COSIM=1 EXPORT_IP=0 FSA_SA_ROWS=4 FSA_SA_COLS=4 bash ./run_hls.sh fsa_stream_request ...'`。
- 开始/结束时间：2026-09-03 03:09:29 至 03:16:07 +08:00；Vitis 报告总 elapsed 6 分 27 秒，远端命令退出码 0。
- 结果与退出码：CSim PASS；CSynth PASS；Verilog/XSim CoSim PASS，10/10 transactions，无 deadlock；IP export 未执行。
- 关键指标：
  - CSim/CoSim 数值：max L error=0.00701189，max O error=0.00193387。
  - DATAFLOW：`runFmaMesh` 成功抽取 19 个进程，即 entry + Input Delayer + 16 PE + Output Delayer；没有 DATAFLOW warning。
  - II：16 个 `stream_pe_process` 的 `VITIS_LOOP_87_1` 均为 Final II=1、depth=15；Accumulator 的 `VITIS_LOOP_34_1` 为 II=1、depth=8。
  - 结构：16 PE，每个 12 DSP、合计 192 DSP；4 CMP，每个 2 DSP、合计 8 DSP；4 个独立 Accumulator lane，每个 8 DSP、合计 32 DSP；总计 232 DSP。
  - Accumulator：4 个 `grp_stream_accumulator_lane_*` 实例，单 lane latency/interval 22–57 cycles；update 总 latency/interval 25–60 cycles。
  - `runFmaMesh`：latency 87–94 cycles，function interval 20–27 cycles；该数字是一次 phase 调用间隔，不等同于 PE 内部 token II。
  - 顶层：CSynth latency 2–682 cycles、interval 3–683 cycles；CoSim latency min/avg/max=3/559/638，interval=4/551/639，总执行 5601 cycles。当前范围不实现 controller/request overlap，因此不宣称顶层请求 II=1。
  - 时序：target 10.000 ns，estimated 7.281 ns，估算 Fmax 137.34 MHz；考虑 2.7 ns uncertainty 后计算余量仅 0.019 ns。
  - 总资源：BRAM_18K=0、DSP=232、FF=71748、LUT=124903、URAM=0；单 SLR 占用约 DSP 7%、FF 8%、LUT 28%。
  - warning：`solution1.log` 共 47 条，即 36 条函数名 legalize、9 条 power-on initialization、2 条 design-size；另有一条 XSim `LIBRARY_PATH` 环境提示，均未阻断综合或 CoSim。error 数为 0。
- 证据路径：`/tmp/fsa_stream_round1_27ba02a.log`；`/home/zhangchenxuan/FSA_HLS/hls/fsa_stream_request/fsa_stream_request_build/solution1/{csim,syn,sim}/report/`；构建 ZIP 为 `/home/zhangchenxuan/FSA_HLS/hls/fsa_stream_request/fsa_stream_request_build.zip`。

#### 本轮结论与下一步

- 已解决的问题：`.bashrc` 环境已正确加载，GitHub/Vitis 环境一致；当前代码的 CSim、CSynth、CoSim 闭环完成；16 PE、4 CMP、Delayer 和 4 路 Accumulator 均由综合报告证实；内部 PE/Accumulator 循环 II=1。
- 仍存在的问题：顶层 request 仍按完整 tile/在线状态顺序执行，CSynth interval 3–683、CoSim平均 551 cycles；这是未实现 controller/request overlap 的已知边界。有效时序余量仅约 0.019 ns，后续布局布线仍有风险。
- 验收标准状态：在“不考虑 controller 指令重叠”的既定范围内全部达到；明确不把 PE token II=1 等同于顶层每周期接受一个新请求。
- 失败分析：本轮没有功能、综合或 CoSim 失败。此前网络失败由未加载 `.bashrc` 导致代理环境缺失，本轮已修正。
- 下一轮修改：无需第2轮；若未来要求顶层 request II=1，必须将请求/phase 调度与在线状态依赖纳入新的多事务重叠架构，不属于本次范围。
- 本轮闭环状态：已完成。

## 6. 调用结束总结

- 结束时间：2026-09-03 03:22:48 +08:00
- 结束原因：全部达到（限定为不包含 controller/request overlap 的内部流式计算核）
- 已完成闭环迭代：1/2
- 未完成迭代：无
- 最终被测代码commit：`27ba02aa44ac1db71419c0f05f4fb983c52c0a8b`
- 最终日志commit：待提交；提交后以分支 HEAD 为准，该文档提交不声称经过 HLS 测试。
- 验收结果：本地 9x4 测试 PASS；远端 CSim/CSynth/Verilog CoSim PASS；16 PE、4 CMP、Input/Output Delayer、4 Accumulator 均实例化；PE 和 Accumulator 内部循环 II=1。
- 仍未解决：顶层 request interval 非 1；有效综合时序余量只有约 0.019 ns。二者均已明确记录，前者超出本次排除 controller overlap 的范围，后者需要后续 Vivado implementation 验证。
- 建议下一步：若继续性能优化，优先做 Vivado implementation 时序验证；若目标升级为顶层 request II=1，则另开任务设计跨请求/跨 phase 重叠和多 bank 在线状态。
- 独立最终报告：未要求
