# Split-D基线性能与资源优化修改日志

## 1 本次调用信息

- 开始时间：2026-10-14 11:07 +08:00。
- 当前状态：进行中；第1轮已开始。
- 本地仓库：`C:/Users/30130/Desktop/workstation/FlashAttention/FSA_HLS_baseline_opt_20261014`。
- 远端仓库：`FSA-FPGA-NM37-tailBox:/home/zhangchenxuan/FSA_HLS`；使用现有SSH配置和Vitis HLS2024.2。
- 分支：`opt/split-d-baseline-20261014`，从`ebe529be6f4d03f855add5f3fe745af9874fa9d4`开始。
- 原工作树已在`backup/split-d-pre-baseline-20261014`的`68638e3f6d316ffbbb93b67f7512e15020ed5695`备份；原`fsa_split_D`提交仍为`52613ef81300d2d02569125e1a5eabf718d6e73f`。
- 目标模块：`fsa_stream_split_d`，16×16/head128；默认VU37P、10ns时钟、2.7ns uncertainty和现有AXI接口。
- 最大迭代次数：未设置。沿用本对话的Git推送和NM37上CSim/CSynth授权；不运行CoSim、IP导出、Vivado或板测。

## 2 期望结果与验收标准

用户要求从已测基线重新优化，同时提高性能和减少资源；不再把II=1作为硬约束，并与16×16的Split-D相关论文公平比较。本轮先按相同器件、参数、测试覆盖和时钟约束测量：

- [ ] 保持现有逐位数值合同、阵列256PE和16列Accumulator结构，官方CSim通过。
- [ ] 官方CSynth完成，无超长停滞；记录LUT、FF、DSP、BRAM、估算时钟、主要循环latency/II及RTL实例数。
- [ ] 相对`ebe529b`，LUT低于1,213,113，且至少一项可核对的计算路径周期低于基线；不把局部II改善称作端到端加速。
- [ ] 若顶层无确定transaction latency，则单独列明实际性能证据不足，后续再安排CoSim或板级计数器。

## 3 初始状态与论文边界

- 优化工作树从`ebe529b`干净创建，初始暂存、未暂存和未跟踪文件均为空；原工作树的现状保存在上面的备份分支。
- 基线官方CSim/CSynth：数学22/22；LUT1,213,113、FF231,565、DSP352、BRAM_18K8、估算时钟7.300ns。旧报告`docs/综合报告/Split-D_16x16_head128_CSim与综合测量_20261011.md`和`docs/evidence/split_d_71025ff_16x16/summary.json`提供近似同形证据；`ebe529b`自身详细报告需再核对。
- 主要面积热点为两块128PE的RawFMA；旧报告每块PE约459,128LUT、两个约占顶层75%。PV一次发射仅16个有用MAC位置，旧II5；这是后续性能优化方向。
- 对比论文：ASICON2025的16×16方案采用INT8/INT16混合精度、Virtex-7/200MHz和外置非线性单元；本设计为FP16乘、FP32累加、VU37P/100MHz目标和阵列内softmax。不同器件与精度不能据裸LUT或CPU加速比评判优劣。

## 4 迭代总览

| 轮次 | 被测提交 | 修改摘要 | 本地测试 | 远端测试 | 状态 |
|---:|---|---|---|---|---|
| 1 | `0349e7f` | RawFMA整数位宽与对齐电路精简 | PE差分及16×16数学通过 | CSim/CSynth通过 | 已闭环：面积下降，性能未变 |
| 2 | 待定 | PV跨feature wavefront调度 | 待执行 | 待执行 | 进行中 |

## 5 第1轮

### 修改前判断与计划

基线PE将FP16/FP32指数以及对齐距离作为32位`int`传递，且先对两个幅值各做一次带sticky右移，再比较、加减。每个PE都复制这部分逻辑。计划先缩小实际指数位宽，将两个幅值先排序，只让较小者做一次右移，保持所有特殊值和舍入位级行为。先做独立旧/新PE差分与整机C++测试，再以精确提交进行官方CSim/CSynth。若面积不降或关键路径变差，就撤销此候选并改测其他热点。

### 实际修改与测试

- `src/stream/pe_raw_fma.cpp`：从备份分支移植旧轮次本地已验证的PE精简。FP16/FP32指数采用定宽整数；C最高位改用`countLeadingZeros`；乘积和C先比较排序，只对较小者做一次带sticky的右移。保留原RawFMA特殊值、PWL、舍入规则。已删除备份源码中的实验性DSP绑定宏，本轮不改变综合参数。
- `PROJECT_CONTEXT.md`：删除旧阶段已失效的当前状态，改为本轮基线、边界和下一步。
- 新工作树缺少Git忽略的Vitis头文件，已从原工作树复制5.2MB依赖到同样的忽略路径；首次本地编译因缺头文件失败，补齐后复测。

| 命令或检查 | 结果 | 证据 |
|---|---|---|
| 基线/候选PE差分 | PASS；1,393,216组FP32、FP16、exp2命中位逐位相同 | 忽略目录`build/baseline_opt_logs/pe_diff.cpp`和可执行文件 |
| PE顶层独立测试 | PASS；30,000随机向量、对齐/规格化边界及exp2 | 本地输出；首次未链接数学替身失败，补齐后通过 |
| `tools/local_split_d_check.cmd 16 128` | PASS；数学22/22，退出0 | `build/baseline_opt_logs/local_16x16.log` |
| `tools/local_split_d_check.cmd 4 16` | 数学24/24；严格位模式19/24，退出1 | `build/baseline_opt_logs/local_4x4.log`；MinGW替身的已知差异，不能判断官方Vitis结果 |
| `git diff --check` | PASS | 当前工作树 |

SSH沙箱内预检只得到别名无法解析；用户SSH配置路径被沙箱拒绝读取。需在宿主审批机制下重试，不能把这个错误判作NM37离线。

宿主权限下已成功预检：原远端仓库`fsa_split_D`位于`d53df16`，tracked文件干净，但有历史未跟踪日志/证据；无`vitis_hls`或`xsim`进程。为避免覆盖，已另建`/home/zhangchenxuan/FSA_HLS_baseline_opt_20261014`分离HEAD工作树，并从GitHub核对为`0349e7f220858f26da6926c6d6ab62b51cba0a48`。备份分支`backup/split-d-pre-baseline-20261014`及优化分支都已正常推送，不改原`fsa_split_D`。

远端首次启动前发现非交互`.bashrc`未把Vitis放入PATH；再加载`/opt/Xilinx_2024.2/Vitis/2024.2/settings64.sh`。该脚本在`set -u`下因未定义`PYTHONPATH`失败；已改为先加载工具链再启用严格Shell选项。均未运行HLS，也未生成测试日志。正式命令于2026-10-14 11:23:45 +08:00开始，测试提交`0349e7f`：`RUN_CSIM=1 RUN_COSIM=0 EXPORT_IP=0 FSA_SPLIT_D_PE_DIM=16 FSA_SPLIT_D_HEAD_DIM=128 ./run_hls.sh fsa_stream_split_d`，在独立工作树以`timeout -s TERM -k 30s 150m`限时；原始输出`logs/round1_16x16_csim_csynth.log`。启动时`vitis-run`位于`/opt/Xilinx_2024.2/Vitis/2024.2/bin/vitis-run`。

11:33只读核对：官方CSim数学22/22、0错误，`csim_design`耗时8分30秒，已进入CSynth源码分析。此前日志中的NaN/Inf self-test故意触发失败路径，不计真实用例失败。CSynth资源和周期尚未报告。

11:53阶段性调度结果：`runPeArray`的Target II1、Final II5、Depth10，与已测基线PE服务间隔相同；单个模块调度耗时350.91秒。此时顶层综合仍在执行，不能据此计算完整PV循环周期或资源收益。

12:00生成的`runPeArray_csynth.rpt`中，单个128PE模块估算LUT为378,796、FF61,365、DSP128、延迟9周期、服务II5、估算时钟7.095ns。与`ebe529b`同模块LUT459,128相比减少80,332（17.5%）；两个模块的顶层节省仍须等待完整CSynth报告，不能直接把单模块差值当作顶层结果。

下一轮候选的独立本地原型已验证：8个feature上下文按斜向wave映射到不同行，保留每个feature的key行累加顺序；100组随机输入、每组128个feature×16行×8列，RawFMA输出逐位一致。原型在忽略目录`build/baseline_opt_logs/pv_wave_prototype.cpp`，尚未修改任务源码。AMD UG1399提示虚假的`DEPENDENCE inter false`可能产生错误硬件，正式修改时要为新的真实反馈距离重新检查依赖声明；见`https://docs.amd.com/r/2024.2-English/ug1399-vitis-hls/Managing-Pipeline-Dependencies`。

### 本轮结论与下一步

第1轮于2026-10-14 12:09:05 +08:00结束，远端命令退出0，距11:23:45启动约45分20秒，未触及150分钟上限。Vitis HLS2024.2 Build5238294；`[PASS] fsa_stream_split_d: PE=16x16 HEAD_DIM=128 DIM_BLOCKS=8`，数学22/22。准确被测源码提交仍是`0349e7f220858f26da6926c6d6ab62b51cba0a48`，远端原始构建在`/home/zhangchenxuan/FSA_HLS_baseline_opt_20261014/hls/fsa_stream_split_d/fsa_stream_split_d_build/solution1/`，日志在同工作树`logs/round1_16x16_csim_csynth.log`。

| 指标 | `ebe529b`基线 | 第1轮`0349e7f` | 变化 |
|---|---:|---:|---:|
| 顶层LUT | 1,213,113 | 1,052,453 | −160,660（−13.24%） |
| 顶层FF | 231,565 | 234,051 | +2,486（+1.07%） |
| DSP | 352 | 352 | 0 |
| BRAM_18K | 8 | 8 | 0 |
| 顶层估算时钟 | 7.300ns | 7.300ns | 0 |
| 单128PE模块LUT | 459,128 | 378,796 | −80,332（−17.50%） |
| PE延迟/服务II | 9/5 | 9/5 | 0 |
| PV满key tile循环延迟 | 10,888周期 | 10,888周期 | 0 |
| `runQueryTile`最小延迟 | 15,325周期 | 15,324周期 | −1周期，不能视作实质加速 |

**已解决：**逐位算术合同由独立PE差分支撑；官方CSim和CSynth通过；总LUT下降且相对VU37P总量1,303,680剩余251,227估算余量。**未解决：**PV循环与PE服务II未改善，尚未同时实现性能和资源双目标；16×16无CoSim、Vivado实现或板级时间，HLS顶层动态latency不能代替实际吞吐；单个128PE模块378,796LUT已占单SLR的87%，物理布局仍待验证。综合日志有既存`RTGEN 206-101`悬空AXI输出默认值及复位警告，本轮没有据此扩大接口范围。

下一轮计划：利用这次面积余量改PV调度，让同一次`runPeArray`调用的不同行分别处理不同feature，保持每个feature按key行0..15累加的数值顺序。独立原型已逐位通过；正式修改要记录新反馈距离并核验HLS II、顶层LUT和PV循环周期。若增加资源超过容量或性能未改善，则回退本轮结构候选，再评估更少上下文的wavefront或DSP绑定。

## 6 第2轮

### 继承问题、假设和计划

第1轮官方16×16测量保留了251,227LUT总量估算余量，但PV循环仍为10,888周期，PE阵列每次只激活一个key行。当前`PV_INTERLEAVE=8`，每个feature仍顺序经过16个key行；计划改为按`wave=row+context`发射，每个wave让最多8个不同行各处理一个feature上下文，从而让一次PE调用完成最多8个有效行的MAC。每组8个feature原有128次PE调用变为23次，保留8次output accumulator更新。真实反馈距离由旧循环的8次变为1次，必须调整`DEPENDENCE`，不能靠伪造依赖降低II。预期即使最终II高于5，也有减少PV周期的空间；实际以官方CSim/CSynth为准。

下一步先改`src/stream/split_d/split_d_controller.cpp`的PV段，不改QK、PWL、AXI和PE数量。先运行本地4×4及16×16数学回归，再提交/推送精确源码与本日志，使用隔离NM37工作树执行同一参数CSim/CSynth。验收要求：官方CSim通过，顶层LUT<1,213,113且≤器件容量，PV循环周期<10,888，时钟仍符合原合同；未做RTL或板测时不声称端到端时间改善。

### 源码修改和本地检查

只改`src/stream/split_d/split_d_controller.cpp`的PV循环：每组8个上下文沿`wave=row+context`发射，最多8个key行同时使用共享PE；一个上下文每轮只更新一次，key行仍严格0..15。原`pv_sum`跨迭代距离8改为真实距离1；output accumulator更新保持原顺序。`git diff --check`通过。

- 独立RawFMA原型：100组随机输入，128 feature×16行×8列，wavefront和逐feature逐行基准输出逐位相同；忽略目录`build/baseline_opt_logs/pv_wave_prototype.cpp`。
- `tools/local_split_d_check.cmd 16 128`：数学22/22、`[PASS]`、退出0；日志`build/baseline_opt_logs/round2_local_16x16.log`。
- `tools/local_split_d_check.cmd 4 16`：数学24/24、严格位模式19/24、退出1；日志`build/baseline_opt_logs/round2_local_4x4.log`。与第1轮本地替身相同，不把此作为新PV差异的证据；官方Vitis CSim仍是功能验收。
