# Split-D全验收持续迭代修改日志

## 1. 本次调用信息

- 开始时间：2026-09-29 01:29 +08:00
- 当前状态：进行中
- 本地仓库：`C:\Users\30130\Desktop\workstation\FlashAttention\FSA_HLS`
- 远端仓库：`FSA-FPGA-NM37-tailBox:~/FSA_HLS`
- 分支：`fsa_split_D`
- 起始commit：`d098e17c2105de3646188badc6e38440410d6532`
- 目标模块：`fsa_stream_split_d`
- 工具链：远端Vitis HLS 2024.2；每条SSH命令使用Bash并显式加载`~/.bashrc`
- 最大迭代次数：未设置；持续到全部验收条件通过或出现技能定义的阻塞
- 调用授权范围：本地修改与测试、普通commit/push、SSH、fast-forward pull、远端Vitis测试、证据读取、日志和根目录`PROJECT_CONTEXT.md`更新
- 额外授权记录：无

## 2. 期望结果与验收标准

### 用户期望

持续迭代并交付一版硬件结构、时序、吞吐和RTL数据全部合格的参数化Split-D FSA。

### 可验证标准

- [ ] 默认`4×4/dim16`为单一`D×D` PE阵列、每列唯一Accumulator、总DSP约40，无阵列复制。
- [ ] QK先执行`dim/D`轮并在PE内FP32寄存器累加S，softmax后PV再执行`dim/D`轮；不使用`PIPELINE off`或虚假依赖换取结果。
- [ ] VU37P、10ns时钟、2.7ns uncertainty保持不变，估算周期不超过7.300ns。
- [ ] QK和ROW_SUM接受真实反馈II5，PWL和PV达到II1；CoSim无deadlock并在有限时间完成。
- [ ] CSim和RTL CoSim全部数据用例通过现有0.03容差，不放宽精度合同；覆盖因果/非因果、单key、多key、短序列和完整tile。
- [ ] 仅改参数得到`16×16/dim128`，并完成对应本地测试、Vitis CSim、CSynth、资源、II、时序和RTL CoSim验证。

## 3. 初始状态

- 本地工作树：起始时分支`fsa_split_D`，HEAD为`d098e17`，无暂存、未暂存或未跟踪文件。
- 远端预检：使用已授权的非沙箱SSH重试成功。远端根目录为`/home/zhangchenxuan/FSA_HLS`，分支`fsa_split_D`，HEAD为`b0ab080`，remote为`git@github.com:diego3893/FSA_HLS.git`，tracked工作树干净。原有`evidence/`、`logs/`和Vivado日志为未跟踪文件，本次不触碰。
- 相关源码和既有测试：`src/stream/split_d/fsa_stream_split_d.cpp`、`tests/stream/test_fsa_stream_split_d.cpp`、`hls/fsa_stream_split_d/run_hls.tcl`。
- 初始问题证据：被测commit `b0ab080`的4×4/16 CSim和CSynth通过，DSP40、7.300ns、QK/ROW_SUM II5、PWL/PV II1；RTL CoSim完成6/6但首事务`max_error=0.199228`。独立`L=1`事务RTL输出全0；16路PV交错与8路结果完全相同，已排除反馈距离不足假设。

## 4. 迭代总览

| 轮次 | 被测commit | 修改摘要 | 本地测试 | 远端测试 | 验收状态 |
|---:|---|---|---|---|---|
| 1 | 待定 | 增加单key、全1-V和basis-V诊断用例并恢复8路PV交错 | 4×4/16、16×16/128通过 | 待执行 | 进行中 |

## 5. 逐轮记录

### 第1轮

#### 修改前判断与计划

- 当前问题：4×4/16硬件、II和时序满足要求，但RTL数据错误；当前16路PV交错无收益并额外消耗FF/LUT。
- 证据：上一轮8路和16路build均在192845ns完成6/6，首事务均为`max_error=0.199228`；`L=1`独立事务RTL输出全0，而`L=7`因果query0单key路径精确。
- 原因假设：错误可能位于短事务写回、softmax分母或PV分子，现有随机测试不能直接区分。
- 本轮计划：恢复8路PV交错；增强顶层testbench，先运行单key，再运行双key全1-V和basis-V用例，并打印首个错误位置和实际/期望值。使用新的CoSim输出判断具体错误阶段。

#### 实际修改

- `src/stream/split_d/fsa_stream_split_d.cpp`：把无功能收益的PV交错从16路恢复为8路，并同步真实RAW distance 8，收回额外FF/LUT。
- `tests/stream/test_fsa_stream_split_d.cpp`：把单key放到首事务；新增双key全1-V和basis-V；失败时报告用例名、首个错误位置、最大错误位置及实际/期望值；主程序不再在首个失败后跳过其他诊断事务。
- 与计划的偏差：无。

#### 修改后本地测试

| 命令 | 结果/退出码 | 关键证据 |
|---|---|---|
| `g++ ... FSA_SPLIT_D_PE_DIM=4 FSA_SPLIT_D_HEAD_DIM=16`并运行 | PASS，code 0 | `[PASS] fsa_stream_split_d: PE=4x4 HEAD_DIM=16 DIM_BLOCKS=4` |
| `g++ ... FSA_SPLIT_D_PE_DIM=16 FSA_SPLIT_D_HEAD_DIM=128`并运行 | PASS，code 0 | `[PASS] fsa_stream_split_d: PE=16x16 HEAD_DIM=128 DIM_BLOCKS=8` |

首次本地编译因testbench中未限定命名空间的`elemZero()`失败；改为显式`(elem_t)0.0F`后两种参数均通过。该问题仅在新增诊断测试代码中，不涉及硬件实现。

#### 修改后远端测试

- 被测commit：待定
- `.bashrc`加载：待验证
- 拉取冲突处理：待验证
- 环境与参数：VU37P、4×4 PE、HEAD_DIM=16、10ns时钟、2.7ns uncertainty
- 命令：`./run_hls.sh fsa_stream_split_d`
- 开始/结束时间：待填写
- 结果与退出码：待执行
- 关键指标：待填写
- 证据路径：待填写

#### 本轮结论与下一步

- 已解决的问题：待填写。
- 仍存在的问题：待填写。
- 验收标准状态：待填写。
- 失败分析：待填写。
- 下一轮修改：待填写。
- 本轮闭环状态：进行中。

## 6. 调用结束总结

- 结束时间：待填写
- 结束原因：待填写
- 已完成闭环迭代：0/未设置
- 未完成迭代：第1轮进行中
- 最终被测代码commit：待定
- 最终日志commit：待定
- 验收结果：待填写
- 仍未解决：待填写
- 建议下一步：待填写
- 独立最终报告：未要求
