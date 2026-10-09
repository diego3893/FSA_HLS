# Split-D补强测试官方复核修改日志

## 1 本次调用信息

- 开始时间：2026-10-10T00:45:24+08:00。
- 状态：第1轮准备中；用户已授权一轮完整流程。
- 本地仓库：`C:/Users/30130/Desktop/workstation/FlashAttention/FSA_HLS`。
- 远端仓库：`FSA-FPGA-NM37-tailBox:/home/zhangchenxuan/FSA_HLS`。
- 分支：`fsa_split_D`；起始本地commit：`cc09f8d146f5bdd09aadb4bad1fb87be0e1db5a0`。
- 目标：`fsa_stream_split_d`，4×4/head16/MAX_SEQUENCE_LENGTH4096。
- 工具链：Vitis2024.2；每条SSH显式运行Bash并加载`~/.bashrc`，需交互式`bash -ic`进入工具环境。
- 授权：同步本次测试补强，运行一轮CSim＋CSynth＋CoSim；不导出IP，不进行16×16、Vivado或板测，不追加第二轮。
- 既有Git授权保留；仅提交任务文件，用户的AGENTS.md修改及无关.tmprun/保留在本地。

## 2 验收标准

- 官方CSim及CoSim中的数学检查24/24、严格位模式20/20，非法长度检查及非有限检查器自检正确。
- 重标定错误模型灵敏度超过4×0.03；48条PWL段界探针通过；CoSim完成26/26且C post-check成功。
- 综合维持7.300ns、DSP40/BRAM8/FF31923/LUT125123、QK/ROW_SUM循环II5、PWL/PV循环II1、runPeArray latency3/II1。
- 唯一PE阵列及唯一列Accumulator不变；不放宽判据、时钟或不实声明验证范围。
- 保留新日志、报告、逐事务周期及版本对应证据。

## 3 初始状态

- 本地未暂存；前轮任务改动为测试台、local_split_d_check.cmd、PROJECT_CONTEXT、两份阶段报告、补强报告及基线证据目录。AGENTS.md是用户原有修改，.tmprun/是无关未跟踪目录。
- 本地桩库已编译：数学24/24、位模式15/20、exit1；5例位差仍待官方环境复核。源码自该次检查未改，不重复运行相同本地检查。
- 服务器预检：仓库路径及origin一致，分支fsa_split_D，HEAD=`9c49789da48d0c17190d3a9ae0cd7606f0da4098`，已跟踪工作树干净。既有未跟踪工具生成物不改动。
- 工具路径`/opt/Xilinx_2024.2/Vitis/2024.2/bin/vitis-run`，磁盘705G可用；没有发现运行中的Vitis任务。
- 非交互bash加载.bashrc后未得到vitis-run，改用交互bash重新预检后成功；未启动重复构建。
- 原9c49789基线输入/O、逐事务周期和RTL实例已归档；旧官方C/RTL完整O一致。新流程会替换标准生成目录，但历史基线已保留。

## 4 第1轮

### 计划与实际改动

假设：相同固定输入下，本地5例位差属于宿主实现差异；官方CSim应保持20事务逐字一致。本轮仅发布前轮补强，不改变综合源码、Tcl、算术合同或接口。

标准命令：`./run_hls.sh fsa_stream_split_d`；默认4×4/head16，RUN_CSIM=1、RUN_COSIM=1、EXPORT_IP=0。保留既有入口全部阶段；精确commit核对后执行一次。

### 结果

待发布精确commit，运行并读取新报告。当前不引用旧生成物作为新结果。
