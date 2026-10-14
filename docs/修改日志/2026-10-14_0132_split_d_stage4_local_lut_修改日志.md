# Split-D阶段4本地LUT优化候选修改日志

## 1 范围与实测基线

- 用户要求检索网络资料并立即在本地修改。此前第5轮完成后暂停的远端CSim/CSynth未重启；本次仅改本地源码并运行本地C++检查，不推送或同步NM37。
- 最新官方被测提交`d53df16ef10588bbc356e43616a711e8990e9bd1`：数学/冻结逐位32/32、共享/PE/Accumulator服务II1、顶层7.300ns、256PE/16Accumulator/32个FP32减法，但LUT1531724/1303680，超228044。来源为[第5轮官方摘要](../evidence/split_d_stage4_20261013/stage4_resume_d53df16_summary.json)及远端完整归档`build/split_d_stage4_resume_d53df16_M8_phase_20261013/`。
- 第5轮`runPeArray_csynth.rpt`每个128PE的worker有256个54位逻辑右移，每个估算161LUT，共41216LUT；两路worker合计512个。每个PE的产品与累加值原本各有一条`shiftRightJam27`对齐路径。即便无代价地消去一半右移，理论上也仅减少约41216LUT，不足以单独填平228044LUT缺口；实际新增选择器、比较器及映射变化须以CSynth为准。
- 同一PE阵列报告的表达式行按运算符汇总，每worker约有`icmp`113280LUT、`lshr`73216LUT、`select`41856LUT、`shl`35712LUT。它们提示后续还需压缩比较/选择与其余移位网络；表达式逐项估算不等于独立可相加的顶层节省。

## 2 资料与取舍

- AMD Vitis HLS2024.2的[AP运算符语义](https://docs.amd.com/r/2024.2-English/ug1399-vitis-hls/Class-Methods-and-Operators)明确移位结果宽度沿用左操作数，算术表达式可能扩位；因此本候选保留原54位jam移位实现与27位舍入信息，仅改变对齐前的操作数选择，不采用截断精度的近似FMA。资料描述语义，不保证综合面积收益。
- AMD的[优化指令说明](https://docs.amd.com/r/2024.2-English/ug1399-vitis-hls/Optimizing-Techniques-and-Troubleshooting-Tips)指出`ALLOCATION`强制资源共享可能增加延迟；[BIND_OP说明](https://docs.amd.com/r/2024.1-English/ug1399-vitis-hls/set_directive_bind_op)允许加减法映射DSP，但实现与延迟须由Vitis检查。当前256PE与II1合同不能靠减少实例数满足；本次未使用这两类未经测量的指令。
- AMD的[数组分区说明](https://docs.amd.com/r/2024.2-English/ug1399-vitis-hls/Array-Partitioning)指出完全分区会把数组拆成寄存器并消耗面积；现有Q/K/V/O已证实落在BRAM，且共享循环II1，故本次不改存储端口/分区。报告中PE比较器、其余右移和移位网络仍是后续可测的面积来源；不从运算符行数直接推断最终顶层LUT。

## 3 本地源码改动

- `src/stream/pe_raw_fma.cpp`的`addFiniteProduct`先按最高位指数、相同时按27位有效数确定较大幅值；对较小幅值只调用一次`shiftRightJam27`，再执行同号加法或异号减法。旧结构对product和c各做一次右移，并在移位后比较、选择减法方向。零、NaN、Inf、FP32次正规数、guard/round/sticky、舍入和exp2入口均未改。
- 数学等价依据：两个非零规格化幅值若最高位指数不同，指数较大者幅值较大；指数相同时27位有效数比较给出顺序。较小者右移带sticky后不可能超过较大者，因此异号差值方向可在移位前确定。相等且相消时仍沿原`magnitude==0`路径输出正零。
- 面积与时序风险：27位有效数比较现在位于选择器和移位器之前，可能拉长PE关键路径或使深度5变为6，进而令共享循环II1退化。局部C++等价不证明Vitis会实际合并54位移位器，也不证明顶层LUT降低。

## 4 本地验证与停点

- 从已提交的`d53df16`源码独立构建旧PE，与本地候选逐位比较：6组固定`b/c/exp2`条件遍历全部65536个FP16`a`位模式，共393216组全等；另以确定性随机种子比较1000000组`a/b/c/exp2`，FP32输出、FP16输出及exp2命中位均全等。
- 16×16/head128/M8/L_MAX1024本地整机数学32/32；capture模式stage4汇总32/32、214561行输入/输出/汇总与第5轮旧源码本地捕获逐行全等。普通本地严格为21/32、exit1，是既有MinGW数学stub与官方Vitis库的11项位差，未改golden、容差或测试台；不能据此宣称官方逐位失败或通过。
- 4×4/head16本地数学52/52、stage12新增严格28/28、旧严格19/24，保持已知stub差异。`git diff --check`通过。未运行Vitis CSim/CSynth、CoSim、IP导出、Vivado或板测；当前本地改动保留待审查，不推送。下一次远端验证须先核对精确提交，再检查官方逐位32/32、PE深度、共享II、7.300ns、完整RTL实例与LUT≤1303680；本次未启动该流程。

## 5 上下文容量与资源本地诊断

- 新增可复算脚本[`tools/split_d_local_lut_estimate.py`](../../tools/split_d_local_lut_estimate.py)及[诊断JSON](../evidence/split_d_stage4_20261013/local_lut_context_sweep.json)。实测锚点是旧M1提交`6f0b40d`的1418632 LUT、初版M8提交`0bfd6d2`的1680612 LUT、最新M8提交`d53df16`的1531724 LUT。前两个差261980 LUT，线性每context约37426 LUT；从最新M8向下外推得到M1约1269744 LUT、M2约1307170 LUT、M4约1382021 LUT。容量为1303680 LUT，M1诊断余量33936 LUT（仅2.6%），M2不计新PE候选仍超3490 LUT。
- **该差额不是纯context成本**：M1锚点L_MAX4096、BRAM_18K=8；M8锚点L_MAX1024、BRAM_18K=360，且中间加入BRAM绑定和状态初始化修改。模型还假设中间M的面积线性，忽略bank阈值、逻辑合并和物理实现。故上述LUT值仅用于筛选，不是当前M1/M2或新PE源码的CSynth估值置信区间。最新M8即使把41216个运算符估算LUT全作为净收益，仍超容量186828 LUT；此数也不是已验证净收益或严格上界。
- 按最新报告`runSharedEngine`调用固定开销49拍，沿用阶段4既有八phase公式估算后续key的共享计算预算：M8每query tile约355拍、M4约568拍、M2约994拍、M1约1846拍。M2/M1分别为M8的2.8/5.2倍；旧`ebe529b`相应计算预算约12427拍。这里只比较共享计算调用，不包含AXI、控制器、首key、DATAFLOW等待或完整Attention事务latency；不能将比例直接称作端到端加速。
- 当前未改默认M8或最终验收合同。对本地PE候选分别以编译选项运行M2及M1，短用例数学均32/32；普通本地严格均21/32，仍为已知MinGW stub差异。M1、M2捕获均32/32，全部214560条`stage4-input/word`与当前M8本地候选SHA256同为`16ac2dce47f5bbf92d6347b7bd4019a19684d5742178d8f3b9921b9b0a991869`。M1/M2都未进行官方综合，资源、II和时序仍待测。

## 6 DSP与BRAM替代LUT的本地实验

- 第5轮官方顶层为DSP356/9024、BRAM_18K360/4032，二者都有余量；顶层LUT缺口仍为228044。已归档`runPeArray_csynth.rpt`的SHA256为`d70aa73c5b4fb08a9970c7dc59dd297c91b0662dce6d212e6cca56a590110d8f`。每个128PE worker的表达式估算有加法45056 LUT、减法28416 LUT、比较113280 LUT、左移35712 LUT、右移73216 LUT；表达式行不等于独立可加的顶层资源。原幅值1加2减共13440 LUT/worker；3处规格化左移共27392 LUT/worker。两路分别约26880及54784个表达式LUT，是下面DSP实验的可见热点，并非预期净节省。
- [AMD的BIND_OP说明](https://docs.amd.com/r/2024.1-English/ug1399-vitis-hls/pragma-HLS-bind_op)允许整数`mul/add/sub`绑定DSP；该版本表格中DSP减法只支持latency0，故实验按此配置。比较器仍在fabric中，不能用BIND_OP转至DSP。PE乘积原已使用DSP，本次不改该乘法。
- `src/stream/pe_raw_fma.cpp`新增默认关闭的`FSA_SPLIT_D_DSP_NORMALIZE=1`：把乘积、C及相消后结果的3处27位规格化左移写成乘以one-hot因子，对乘法结果施加`BIND_OP impl=dsp latency=1`。在27位截断语义下，乘法与原左移逐位相同；工具是否保留DSP实现、需要多少DSP及是否延长PE深度，只能由CSynth判断。另有默认关闭的`FSA_SPLIT_D_DSP_MAGNITUDE=1`：将幅值加/减分别绑定DSP，加法latency1、减法latency0，随后按符号选择。两个开关独立，可做消融。
- `hls/fsa_stream_split_d/run_hls.tcl`只把显式设置的0/1实验开关传给DUT和TB；`tools/local_split_d_check.cmd`可从同名环境变量启用本地候选。默认二者均未定义，现有M8源码/配置合同保持不变。
- Q/K/V/O在第5轮已用BRAM；S/P的`score/reg`虽是LUTRAM，但每个worker仅有8×16×8×(32+16)=49152数据位，并按行/列完全分bank。粗按64bit/LUT折算仅768个原始存储LUT/worker，不能填平228044 LUT缺口；改BRAM还会产生许多浅bank，并可能改变读延迟/端口调度。PE年龄槽完全分区以供128个PE并行读写，简单加`BIND_STORAGE`不能在维持当前并行端口合同的同时替代为少量BRAM。因此本轮不把这些数组盲目强绑BRAM。[AMD的BIND_STORAGE说明](https://docs.amd.com/r/2024.1-English/ug1399-vitis-hls/pragma-HLS-bind_storage)列出了RAM端口类型与BRAM可用组合，但不保证II。
- 本地估算脚本追加极乐观表达式账本：旧报告中单条对齐右移41216 LUT、3处规格化左移54784 LUT、旧幅值1加2减26880 LUT，两路worker合计122880 LUT。即使假设这些表达式行全部免费消失，最新M8顶层仍按直减为1408844 LUT、超容量105164 LUT。该直减仅显示所选热点的规模，绝非实际映射收益、严格下界或综合预测；选择器、比较器、DSP接口及逻辑重组均可能改变结果。
- 两DSP开关同时为1时，本地16×16/head128/M8/L_MAX1024数学32/32，capture模式32/32、214560条`stage4-input/word`与默认M8本地候选SHA256同为`16ac2dce47f5bbf92d6347b7bd4019a19684d5742178d8f3b9921b9b0a991869`；旧`d53df16`与新RawFMA另做1000000组随机逐位对照通过。普通本地严格21/32仍是既有stub差异。未运行新CSynth，因此**没有**新LUT/DSP/BRAM/II/时序数据，也不能把本实验计入阶段4验收。
