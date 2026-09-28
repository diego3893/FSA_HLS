# Split-D全验收持续迭代修改日志

## 1. 本次调用信息

- 开始时间：2026-09-29 01:29 +08:00
- 当前状态：已结束，达到3轮上限但未完全验收
- 本地仓库：`C:\Users\30130\Desktop\workstation\FlashAttention\FSA_HLS`
- 远端仓库：`FSA-FPGA-NM37-tailBox:~/FSA_HLS`
- 分支：`fsa_split_D`
- 起始commit：`d098e17c2105de3646188badc6e38440410d6532`
- 目标模块：`fsa_stream_split_d`
- 工具链：远端Vitis HLS 2024.2；每条SSH命令使用Bash并显式加载`~/.bashrc`
- 最大迭代次数：3；若提前全部验收则提前结束
- 调用授权范围：本地修改与测试、普通commit/push、SSH、fast-forward pull、远端Vitis测试、证据读取、日志和根目录`PROJECT_CONTEXT.md`更新
- 额外授权记录：无

## 2. 期望结果与验收标准

### 用户期望

持续迭代并交付一版硬件结构、时序、吞吐和RTL数据全部合格的参数化Split-D FSA。

### 可验证标准

- [x] 默认`4×4/dim16`为单一`D×D` PE阵列、每列唯一Accumulator、总DSP约40，无阵列复制。
- [x] QK先执行`dim/D`轮并在PE内FP32寄存器累加S，softmax后PV再执行`dim/D`轮；不使用`PIPELINE off`或虚假依赖换取结果。
- [x] VU37P、10ns时钟、2.7ns uncertainty保持不变，估算周期不超过7.300ns。
- [x] QK和ROW_SUM接受真实反馈II5，PWL和PV达到II1；CoSim无deadlock并在有限时间完成。
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
| 1 | `58eecaad3b1530dfa22c28daded59311927dbfa9` | 增加单key、全1-V和basis-V诊断用例并恢复8路PV交错 | 4×4/16、16×16/128通过 | CSim/CSynth通过，CoSim数据失败 | 未通过 |
| 2 | `6c80d49346fb47139cfc21be93dac63889626a73` | 分离V专用非内联加载器 | 4×4/16、16×16/128通过 | CSim/CSynth通过，CoSim数据失败 | 未通过 |
| 3 | `842ef7787ca4edc24326878779ef67e4b444a98a` | 将V加载内联到控制器，消除跨事务`ap_done`状态 | Windows缺Vitis头文件；由远端CSim替代 | CSim/CSynth通过，CoSim数据失败 | 未通过，达到上限 |

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

- 被测commit：`58eecaad3b1530dfa22c28daded59311927dbfa9`
- `.bashrc`加载：使用`bash -ic`并显式加载成功，Vitis HLS 2024.2正常启动。
- 拉取冲突处理：无。远端tracked工作树干净，从`b0ab080`快进到精确被测commit；既有未跟踪文件未触碰。
- 环境与参数：VU37P、4×4 PE、HEAD_DIM=16、10ns时钟、2.7ns uncertainty
- 命令：`./run_hls.sh fsa_stream_split_d`
- 开始/结束时间：2026-09-29 01:37:46+08:00至01:43:43+08:00；Vitis总elapsed 357.85s。
- 结果与退出码：FAIL，code 1。CSim和CSynth通过；RTL仿真8/8事务完成且无deadlock，C post-check的7个有效长度事务全部数值失败。
- 关键指标：估算周期7.300ns；DSP40、BRAM8、FF29415、LUT123434；QK/ROW_SUM II5、PWL/PV II1；顶层最大延迟439747587 cycles。CoSim在208505ns结束。单keyRTL全0；全1-V用例首值为-0.113159而非1；basis-V用例RTL整行为1，精确等于前一个全1-V事务的期望结果。
- 证据路径：`hls/fsa_stream_split_d/build/solution1/csim/report/`、`syn/report/fsa_stream_split_d_csynth.rpt`、`syn/report/runController_csynth.rpt`、`syn/report/runController_Outline_VITIS_LOOP_309_3_csynth.rpt`、`sim/tv/cdatafile/`、`sim/tv/rtldatafile/`和`sim/verilog/xsim.log`。

#### 本轮结论与下一步

- 已解决的问题：诊断用例把随机误差收敛为跨事务V数据滞后的可复现现象；恢复8路PV交错后资源回到FF29415/LUT123434，DSP、II和时序不变。
- 仍存在的问题：RTL使用的V tile不是当前事务数据，导致全部有效事务失败；尚不能在修复V加载前判断是否还有独立的softmax/PV数值问题。
- 验收标准状态：本地两种参数、远端CSim、CSynth、硬件结构、DSP、II、时序和CoSim控制流程通过；RTL数据失败，整体未验收。
- 失败分析：输入TV逐事务解析确认当前Q/K/V文件正确；输出解析显示事务1使用事务0的稀疏V、事务2输出精确等于事务1全1-V结果。综合层次显示key循环outline中的K/V共用一个`loadElemTile`子模块和内部状态，当前证据指向V加载调用的跨事务/跨调用调度错误，而不是PV反馈距离。
- 下一轮修改：给V建立独立的非内联加载函数，使K和V不再共享同一个有状态HLS子模块；保持数组布局、AXI bundle、计算结构和数值合同不变。
- 本轮闭环状态：已完成。

### 第2轮

#### 修改前判断与计划

- 当前问题：V tile在RTL中滞后一事务，而输入TV数据正确。
- 证据：basis-V事务的RTL输出精确复现前一全1-V事务；`runController_Outline_VITIS_LOOP_309_3`中K/V共用唯一`grp_loadElemTile`。
- 原因假设：同一非内联加载子模块在K、V连续调用和外层outline间产生了错误的状态/调度复用。
- 本轮计划：新增V专用加载函数，确保K和V拥有不同HLS子模块；先跑两种参数本地测试，再提交远端完整HLS。

#### 实际修改

- `src/stream/split_d/fsa_stream_split_d.cpp`：新增非内联`loadValueTile`，仅连接V AXI bundle；K继续使用`loadElemTile`。两者保持相同数据布局和II1加载循环，但综合时成为不同子模块，不再共享调用状态。
- 与计划的偏差：无。

#### 修改后本地测试

| 命令 | 结果/退出码 | 关键证据 |
|---|---|---|
| `g++ ... FSA_SPLIT_D_PE_DIM=4 FSA_SPLIT_D_HEAD_DIM=16`并运行 | PASS，code 0 | `[PASS] fsa_stream_split_d: PE=4x4 HEAD_DIM=16 DIM_BLOCKS=4` |
| `g++ ... FSA_SPLIT_D_PE_DIM=16 FSA_SPLIT_D_HEAD_DIM=128`并运行 | PASS，code 0 | `[PASS] fsa_stream_split_d: PE=16x16 HEAD_DIM=128 DIM_BLOCKS=8` |

#### 修改后远端测试

- 被测commit：`6c80d49346fb47139cfc21be93dac63889626a73`
- `.bashrc`加载：使用`bash -ic`并显式加载成功，Vitis HLS 2024.2正常启动。
- 拉取冲突处理：无。远端tracked工作树干净，从`58eecaa`快进到精确被测commit；既有未跟踪文件未触碰。
- 环境与参数：VU37P、4×4 PE、HEAD_DIM=16、10ns时钟、2.7ns uncertainty
- 命令：`./run_hls.sh fsa_stream_split_d`
- 开始/结束时间：2026-09-29 01:58:10+08:00至02:04:07+08:00；Vitis总elapsed 357.73s。
- 结果与退出码：FAIL，code 1。CSim和CSynth通过；RTL仿真8/8事务完成且无deadlock，C post-check有6个数值用例失败，单key首事务通过。
- 关键指标：估算周期7.300ns，Estimated Fmax 136.99MHz；QK/ROW_SUM II5、PWL/PV II1。CoSim在205005ns结束。全1-V事务首值为0而非1；紧随其后的basis-V事务首值为0.5，符合使用前一事务全1-V的结果。
- 证据路径：`hls/fsa_stream_split_d/build/solution1/csim/report/`、`syn/report/`、`sim/tv/`和`sim/verilog/xsim.log`。

#### 本轮结论与下一步

- 已解决的问题：K和V不再共用同一个加载RTL实例；生成物中出现独立`loadValueTile`模块。单key首事务数据通过，控制流程无deadlock。
- 仍存在的问题：第二笔及以后顶层事务仍使用前一笔事务的V tile，6个多key/后续事务数据失败。
- 验收标准状态：CSim、CSynth、目标II、时序和CoSim控制流程通过；RTL数据失败，整体未验收。
- 失败分析：独立V加载函数每个短事务只调用一次。生成RTL保留带`ap_start/ap_done`状态的`loadValueTile`子模块；首事务通过而后续事务稳定读取前一事务V，说明跨顶层调用时控制器观察到上一调用残留完成状态，在当前V写入tile完成前进入PV。
- 下一轮修改：将V加载函数强制内联到调用控制器，消除独立`ap_done`握手和跨顶层事务状态，同时保持AXI、循环II、数据布局和计算结构不变。
- 本轮闭环状态：已完成。

### 第3轮

#### 修改前判断与计划

- 当前问题：独立V加载RTL实例在首事务正确，但后续事务稳定让V tile滞后一笔。
- 证据：第2轮单key首事务通过；全1-V事务输出0，后一basis-V事务输出约0.5；生成RTL存在独立`loadValueTile`模块。
- 原因假设：每个短事务只调用一次的V子模块在下一次顶层启动时残留完成握手，使调用控制器在新tile写完前继续执行。
- 本轮计划：把V加载循环内联进调用控制器，彻底移除V加载子模块边界；完成两种参数本地测试后提交远端完整HLS。这是本次限定的第3轮。

#### 实际修改

- `src/stream/split_d/fsa_stream_split_d.cpp`：把V专用`loadValueTile`由`INLINE off`改为强制`INLINE`，使加载循环进入key-tile控制层级，不再生成独立的跨顶层事务握手模块。
- 保持V的AXI bundle、地址计算、tile布局、II1加载循环和全部算术路径不变。
- 与计划的偏差：无。

#### 修改后本地测试

- `git diff --check`通过。
- Windows本地`g++`缺少Vitis的`ap_int.h`，两种参数编译均在头文件解析阶段退出，未进入本次修改代码；本轮以服务器Vitis CSim作为编译和功能检查。

#### 修改后远端测试

- 被测commit：`842ef7787ca4edc24326878779ef67e4b444a98a`
- `.bashrc`加载：使用`bash -ic`并显式加载成功，Vitis HLS 2024.2正常启动。
- 拉取冲突处理：无。远端tracked工作树干净，从`6c80d49`快进到精确被测commit；既有未跟踪文件未触碰。
- 环境与参数：VU37P、4×4 PE、HEAD_DIM=16、10ns时钟、2.7ns uncertainty
- 命令：`./run_hls.sh fsa_stream_split_d`
- 开始/结束时间：2026-09-29 02:08:27+08:00至02:14:23+08:00；Vitis总elapsed 356.42s。
- 结果与退出码：FAIL，code 1。CSim和CSynth通过；RTL仿真8/8事务完成且无deadlock。单key和全1-V通过，basis-V及4个随机用例超过0.03容差。
- 时序与资源：估算周期7.300ns，Estimated Fmax 136.99MHz；BRAM8、DSP40、FF29745、LUT124445。QK/ROW_SUM II5、PWL/PV II1。
- 结构：控制器层次只有一套8-DSP`runAccumulatorColumns`和一套32-DSP key-tile outline；后者由16个PE与16个减法器组成。独立`loadValueTile` RTL文件消失，V加载已内联。
- 延迟：顶层最大估算492176387 cycles；CoSim 8/8于225795ns结束。
- 数据：basis-V首输出实际0.5、期望0.562177，最大误差0.096066；其余失败用例最大误差分别为0.0641532、0.0718321、0.0542624、0.0752103。
- 证据路径：`hls/fsa_stream_split_d/build/solution1/csim/report/`、`syn/report/fsa_stream_split_d_csynth.rpt`、`syn/report/runController_csynth.rpt`、`syn/verilog/`、`sim/tv/`和`sim/verilog/xsim.log`。

#### 本轮结论与下一步

- 已解决的问题：V跨顶层事务滞后已消除；首个单key和第二个全1-V事务均通过，独立V加载RTL模块也已消失。
- 仍存在的问题：basis-V得到均匀0.5/0.5而非0.562177/0.437823，说明当前RTL的QK分数差没有正确进入softmax；随机用例仍有0.054至0.075最大误差。16×16/128未做Vitis验证。
- 验收标准状态：4×4/16硬件结构、DSP、II、时序、CSim和CoSim控制流程通过；RTL数据失败。由于基础参数数据未通过，不启动成本更高且无法形成最终验收的16×16/128 HLS运行。
- 失败分析：本轮已经把V路径从“整笔事务错位”修到正确。basis-V使用确定的Q/K分数和正交V，实际输出精确为0.5，直接把剩余故障定位到QK分数、缩放或进入PWL前的数据时序，而不是V加载或PV分子。
- 下一轮建议：若允许新一轮，给basis-V增加RTL可判别的score/PWL中间检查，优先检查QK最后一轮结果写入`score`以及scale进入PWL的调度边界；保持当前已通过的V内联修复。
- 本轮闭环状态：已完成；达到用户限定的3轮上限，停止继续修改。

## 6. 调用结束总结

- 结束时间：2026-09-29 02:14:23+08:00
- 结束原因：完成用户限定的3轮；第3轮仍未满足RTL数据验收，因此按上限停止。
- 已完成闭环迭代：3/3
- 未完成迭代：无
- 最终被测代码commit：`842ef7787ca4edc24326878779ef67e4b444a98a`
- 最终日志commit：由本节所在提交承载，以Git历史为准。
- 验收结果：未通过。硬件结构、40 DSP、II和7.300ns时序合格；4×4/16 RTL数据仍超过0.03容差；16×16/128未做远端Vitis验收。
- 仍未解决：QK分数差或其缩放/PWL输入在RTL中的时序错误；basis-V被错误计算为均匀权重。
- 建议下一步：新一轮从basis-V的score与PWL输入做定点诊断，不再改V加载和PV交错结构。
- 独立最终报告：未要求
