# Split-D补强测试官方复核修改日志

## 1 本次调用信息

- 开始时间：2026-10-10T00:45:24+08:00。
- 状态：第1轮已闭环，达到一轮上限，完整验收未通过。
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

- 已提交并推送`46cbbd0b5fdb090ce547a762a10cb8d2308d359e`；服务器fast-forward成功，HEAD逐字核对一致，tracked工作树干净。
- 入口SHA256=`3d2b870c4b49547b99cc61d9e7198efa8972328eb499207ec14907716a22c213`；TclSHA256=`dbe4dc804c59e986e229bb5f3ab7dcfd23a694138fe63f9aaa34a86553e135e3`，与同步前一致；测试台SHA256=`9c2a2e4f3686d678a7a08f5a0b5d1eb48f32170712dcb9b5859ad6a5f8f558d4`。
- 已于2026-10-10T00:47:43+08:00启动标准流程，日志`/tmp/codex_split_d_46cbbd0_20261010.log`。本轮正在运行，报告未验收。
- 启动命令的printf元数据在PowerShell→SSH引号传递中格式异常，未影响标准入口启动；版本证据使用独立SSH预检及运行中HEAD复核。最终退出状态与结束时刻需结合会话和正式日志确认，不从异常printf臆造。

### 本轮结论与下一步

- 官方CSim：位模式20/20、数学22/24；重标定两个用例的灵敏度均0.434121，20个原基线及非法长度检查执行成功；两条PWL构造于7/8精确点失败，main返回1。CSim耗时36秒，工具总耗时55.93秒，失败日志最后更新00:48:43+08:00。未进入CSynth/CoSim，未出现可供本轮验收的新资源或RTL证据。
- 已解决：官方环境不重现本地5例位差，固定输入及旧O期望一致。正式日志已下载，SHA256与服务器一致。
- 仍存在：测试构造把所有段界视为精确可达，FP16 RNE在固定SCALE下跳过7/8；尚无新增PWL顶层/RTL证明。5例本地位差逐运算归因未完成。
- 诊断：本地无FPO库half转换截断尾数；官方使用FPO转换。0x40da/0x40db乘0x35c5的RNE结果0x3aff/0x3b01跳过0x3b00。既有CSynth/CoSim成功目录仍为旧9c49789，不引用作新结果。
- 验收状态：20项位模式通过；完整数学、CSim、CSynth及CoSim未通过/未执行，不记作全PASS。
- 下一次修改：只修测试台探针的明确RNE量化及可达性判定；所有段界保留两侧检查，可达边界才要求精确命中，不可达必须显式记录。修改未实施，下一轮需新授权。
- 闭环：已完成1/1轮，取得并分析真实失败数据，按一轮上限停止；未新开迭代或修改硬件。

## 5 调用结束总结

- 分析结束时间：2026-10-10T00:56:41+08:00。最终验收记录随本次收尾commit发布，commit号见Git记录；不是新的被测代码commit。
- 最终被测代码commit：`46cbbd0b5fdb090ce547a762a10cb8d2308d359e`。
- 完成1/1闭环轮次；完整验收失败，未追加运行。
- 证据：`docs/evidence/split_d_46cbbd0_4x4_recheck/`；独立报告：`docs/阶段1官方复核_20261010.md`。
- 本轮不执行IP导出、Vivado、板测或16×16；用户AGENTS.md与无关.tmprun/保留。结束记录以文档提交发布，不声称文档提交经过HLS测试。
- 新官方日志已逐字核对服务器SHA256；Git属性仅为这两份原始日志禁用文本转换/空白检查，以保留工具生成的行尾空格，不清洗原始证据。本轮没有修改测试台失败条件或综合源码。
