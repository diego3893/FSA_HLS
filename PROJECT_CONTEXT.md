# FSA HLS项目上下文

## Mission

在VU37P上优化独立顶层`fsa_stream_split_d`的16×16/head128实现。用户2026-10-14要求备份原状态、从已测基线重新修改，同时提高性能、降低资源；II=1不再是硬约束，1024长测试暂不做。不得改阵列规模、FP16乘/FP32累加合同、AXI协议、器件、10ns时钟或2.7ns uncertainty来制造通过。本文只维护当前任务的高信号状态；历史细节可查Git和`docs/综合报告/`。

## Current State

- **Confirmed:**原工作树`C:/Users/30130/Desktop/workstation/FlashAttention/FSA_HLS`的所有已跟踪修改和未跟踪文件已提交到`backup/split-d-pre-baseline-20261014`，提交`68638e3f6d316ffbbb93b67f7512e15020ed5695`；`fsa_split_D`仍指向`52613ef81300d2d02569125e1a5eabf718d6e73f`。额外文件夹备份已按用户要求删除。
- **Confirmed:**本工作树`C:/Users/30130/Desktop/workstation/FlashAttention/FSA_HLS_baseline_opt_20261014`从已测`ebe529be6f4d03f855add5f3fe745af9874fa9d4`创建，分支`opt/split-d-baseline-20261014`。没有重置、丢弃原工作树改动。
- **Confirmed:**`ebe529b`的16×16/head128官方CSim/CSynth通过；LUT1,213,113、FF231,565、DSP352、BRAM_18K8、估算时钟7.300ns，两个16×8 worker，总256PE/16列Accumulator。顶层transaction latency为undef；未做16×16 CoSim、IP、Vivado或板测。原始摘要在备份分支`docs/evidence/split_d_ebe529b_16x16_timing/summary.json`。
- **Confirmed:**先前II1阶段4的`d53df16`为LUT1,531,724，超VU37P的1,303,680；该版本不能作为容量合格实现。其共享PE局部II1不能证明端到端性能。
- **Current assessment:**基线两个128PE RawFMA块是面积主因；旧相近报告每块约459,128LUT。基线PV每轮只利用16/256个MAC位置，性能改进应优先考虑跨feature调度，但要保存逐key行浮点顺序，且不能让额外状态把LUT推过容量。
- **Confirmed:**第1轮`0349e7f`官方16×16/head128 CSim/CSynth于2026-10-14 12:09 +08:00完成，退出0，约45分20秒。顶层LUT1,052,453（基线−160,660，−13.24%）、FF234,051、DSP352、BRAM_18K8、估算时钟7.300ns；PE服务II5、PV循环10,888周期均未改善。当前尚未同时满足性能和资源目标。
- **Plan:**第2轮将PV改为8个feature上下文斜向wave调度，利用同一次PE调用的不同行；数值顺序由独立原型逐位验证，本地16×16数学22/22通过。下一步以官方CSim/CSynth核对真实PV周期和额外LUT。

## Current Working Set

- `src/stream/pe_raw_fma.cpp`：第1轮已测PE精简，新旧`ebe529b` PE独立对照1,393,216组逐位全等。
- `src/stream/split_d/split_d_controller.cpp`：第2轮PV跨feature wavefront，待官方验证。
- `docs/修改日志/2026-10-14_1107_split_d_baseline_opt_修改日志.md`：本轮研究记录。
- `hls/fsa_stream_split_d/run_hls.tcl`：本轮不改；官方入口`./run_hls.sh fsa_stream_split_d`，设置`RUN_COSIM=0 EXPORT_IP=0 FSA_SPLIT_D_PE_DIM=16 FSA_SPLIT_D_HEAD_DIM=128`。
- `build/baseline_opt_logs/`：忽略的本地编译与差分日志；`third_party/vitis_hls/include/`在本工作树是从原工作树复制的忽略依赖。

## Decisions and Constraints

- 用户明确接受从基线开始，并用Git分支备份当前状态；不要把原工作树的II1/M8大面积版本当作新主线。
- 旧4×4版本有真实HBM上板26普通＋26背压事务通过，不能外推到16×16。
- 本轮授权沿用此前Git推送及NM37上的CSim/CSynth；CoSim/IP/实现/板测未纳入本轮。远端需通过SSH别名`FSA-FPGA-NM37-tailBox`，每条命令显式以Bash加载`~/.bashrc`；路径`/home/zhangchenxuan/FSA_HLS`。不得覆盖远端未提交改动。
- 用户指定的论文比较至少包含ASICON2025《A 16×16 High-Utilization Systolic Array Hardware Accelerator for Long-Sequence Flash-Attention Computation in Transformer》；论文INT8/INT16、Virtex-7/200MHz与本设计FP16/FP32、VU37P/100MHz及不同验证阶段不直接同口径。历史报告记载论文LUT91,633、FF168,507、DSP544、BRAM236、N1024时间9.83ms；须标注来源与比较限制。FSA原论文采用128×128 ASIC评估，更不能直接比较FPGA LUT。

## Validation Status

- **Confirmed local:**PE旧/新源码差分1,393,216向量，FP32/FP16/exp2命中逐位全等；独立PE顶层30,000随机及边界测试通过。
- **Confirmed local:**16×16/head128整机数学22/22通过。本地4×4/head16数学24/24通过，但严格位模式19/24；这是已知MinGW数学替身与官方Vitis差异，本轮不能据此判官方位模式结果。
- **Confirmed remote:**`0349e7f`官方16×16/head128 CSim数学22/22、CSynth完成；顶层LUT1,052,453，PV循环10,888周期。远端原始日志`logs/round1_16x16_csim_csynth.log`。
- **In progress local:**第2轮PV改动已完成；16×16/head128数学22/22，退出0；4×4/head16数学24/24、严格位模式19/24，属于已有MinGW替身差异，待官方CSim判定。

## Next Actions

1. 提交第2轮PV改动和研究日志；在NM37隔离工作树核对精确提交后运行同配置CSim/CSynth，不重复启动。
2. 比较LUT、时钟和PV循环周期；若超器件容量或性能不降，回退/缩小PV wavefront。
3. 完成独立报告，清楚区分本地数学测试、HLS估算、RTL/板测与论文指标，提交/推送最终日志。

## Context Handoff Summary

原状态已在Git备份分支`68638e3`保存并推送。优化分支从`ebe529b`开始，第1轮`0349e7f`通过官方CSim/CSynth，LUT降低13.24%但PV周期不变；第2轮wavefront源码及本地16×16数学已通过，待远端核对。用户不再要求II1，也不希望运行N1024长测试。
