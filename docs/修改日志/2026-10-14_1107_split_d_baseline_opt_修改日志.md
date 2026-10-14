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
| 1 | 待定 | RawFMA整数位宽与对齐电路精简 | 待执行 | 待执行 | 进行中 |

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

### 本轮结论与下一步

待远端数据后填写；当前未闭环。
