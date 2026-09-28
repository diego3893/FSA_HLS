# Split-D RTL数值修复修改日志

## 1. 本次调用信息

- 开始时间：2026-09-29 00:52 +08:00
- 当前状态：进行中
- 本地仓库：`C:\Users\30130\Desktop\workstation\FlashAttention\FSA_HLS`
- 远端仓库：`FSA-FPGA-NM37-tailBox:~/FSA_HLS`
- 分支：`fsa_split_D`
- 起始commit：`ce6cb678952112990f1bade26f7f2a39a379a218`
- 目标模块：`fsa_stream_split_d`
- 工具链：远端Vitis HLS 2024.2；每条SSH命令显式加载`~/.bashrc`
- 最大迭代次数：1（用户要求每完成一轮迭代即暂停汇报）
- 调用授权范围：本地修改/测试、普通commit/push、SSH、pull、远端测试、证据读取、日志更新
- 额外授权记录：无

## 2. 期望结果与验收标准

### 用户期望

根据当前4×4/16 Split-D RTL CoSim数值错误开始下一轮迭代，定位根因并最小修复；完成一次远端闭环后暂停汇报。

### 可验证标准

- [ ] 4×4/16 CSim通过。
- [ ] CSynth保持单4×4 PE阵列、单Accumulator通路和DSP40。
- [ ] 目标时钟10ns下估算周期不高于7.300ns。
- [ ] QK/ROW_SUM保持II5，PWL/PV保持II1。
- [ ] RTL CoSim完成6/6事务，无deadlock、无`Bad TV file`，C post-check数值通过。
- [ ] 不改变器件、时钟、接口、误差阈值或既定算法结构。

## 3. 初始状态

- 本地工作树：分支`fsa_split_D`，HEAD为`ce6cb678952112990f1bade26f7f2a39a379a218`；无暂存、未暂存或未跟踪文件。
- 远端预检：显式加载`~/.bashrc`后连接成功；仓库根目录`/home/zhangchenxuan/FSA_HLS`，分支`fsa_split_D`，HEAD为上一轮被测代码`ac44a2b86bd13810953dd62a7892376123e3f6ea`。tracked工作树干净；仅有既知未跟踪`evidence/`、`logs/`和Vivado日志，均不触碰。远端尚未拉取本地文档commit`ce6cb67`。
- 相关源码和既有测试：`src/stream/split_d/fsa_stream_split_d.cpp`、`tests/stream/test_fsa_stream_split_d.cpp`、`hls/fsa_stream_split_d/run_hls.tcl`。
- 初始问题证据：上一轮4×4/16 CSim/CSynth通过，DSP40、7.300ns、QK/ROW_SUM II5、PWL/PV II1；CoSim完成6/6，但首个`L=7, causal=0`用例`max_error=0.199228`，C post-check失败。

## 4. 迭代总览

| 轮次 | 被测commit | 修改摘要 | 本地测试 | 远端测试 | 验收状态 |
|---:|---|---|---|---|---|
| 1 | 待定 | 待定位后填写 | 待执行 | 待执行 | 进行中 |

## 5. 逐轮记录

### 第1轮

#### 修改前判断与计划

- 当前问题：RTL已能结束全部事务，但输出数值与C参考不一致。
- 证据：首个`L=7, causal=0`事务`max_error=0.199228`；CSim通过，说明C级算法和地址映射一致，错误仅在RTL调度或接口行为中暴露。
- 原因假设：远端TV文件按正确lane顺序解码后，首个事务不是token/feature置换；长度1事务的C输出等于唯一V向量，而RTL输出全0，说明错误位于PV反馈而非AXI写回。`csynth.rpt`进一步显示PV循环迭代延迟为9拍，而`pv_sum`上下文按`distance=8`反馈，8路交错不足以覆盖真实流水延迟。
- 本轮计划：把PV独立feature上下文由8路增至16路，并同步真实RAW距离为16；默认16维和目标128维的PV总操作数不变，只增加局部`pv_sum`寄存器，不复制PE或Accumulator。完成本地回归、提交推送和远端完整HLS测试。

#### 实际修改

- `src/stream/split_d/fsa_stream_split_d.cpp`：`PV_INTERLEAVE`由8改为16，`pv_sum`真实RAW距离由8改为16，使同一feature上下文的反馈间隔大于PV循环9拍迭代延迟。
- 与计划的偏差：无。

#### 修改后本地测试

| 命令 | 结果/退出码 | 关键证据 |
|---|---|---|
| `.\run_test.ps1 test_fsa_stream_split_d` | FAIL，code 1 | 通用脚本仅扫描`tests/`根目录，未登记`tests/stream/`中的Split-D测试；不是源码编译失败。 |
| `g++ ... tests/stream/test_fsa_stream_split_d.cpp -o build_local/test_fsa_stream_split_d_4x4.exe`并运行 | PASS，code 0 | `[PASS] fsa_stream_split_d: PE=4x4 HEAD_DIM=16 DIM_BLOCKS=4`。 |
| 上述命令增加`-DFSA_SPLIT_D_PE_DIM=16 -DFSA_SPLIT_D_HEAD_DIM=128`并运行 | PASS，code 0 | `[PASS] fsa_stream_split_d: PE=16x16 HEAD_DIM=128 DIM_BLOCKS=8`。 |

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
- 已完成闭环迭代：0/1
- 未完成迭代：第1轮进行中
- 最终被测代码commit：待定
- 最终日志commit：待定
- 验收结果：待填写
- 仍未解决：待填写
- 建议下一步：待填写
- 独立最终报告：未要求
