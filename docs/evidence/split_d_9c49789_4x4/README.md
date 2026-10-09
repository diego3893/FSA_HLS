# 9c49789的4×4基线证据

## 1 来源与格式

来自服务器正式`hls/fsa_stream_split_d/fsa_stream_split_d_build/solution1`，被测commit为`9c49789da48d0c17190d3a9ae0cd7606f0da4098`，参数4×4/head16/MAX_SEQUENCE_LENGTH4096。仅只读取回已有文件，未运行新构建。

- [baseline.json](baseline.json)：原24个有效事务的完整有效Q/K/V/O打包字、两类非法事务的全O canary核对结果、配置及原始文件SHA256。含原输入已修改的4个事务，其旧输出仅作为历史证据。
- [transactions.csv](transactions.csv)：原26个事务的名称、长度、causal及latency/interval。
- [原始周期表](performance.result.transaction.txt)：原`.performance.result.transaction.xml`原样保存；该文件实际为文本表，末事务interval为x，不应写成0。
- [RTL实例清单](rtl_arithmetic_instances.txt)：顶层父路径、16个PE乘法、4个Acc乘法、8个FP32减法的例化位置，以及服务器原始文件SHA256。

TV文件是二进制：每事务先有8字节大端uint64字数，然后是对应数量的大端uint64 DMA打包字；共26帧，末尾为`0x5a5aa5a50f0ff0f0`。Q/K/V每帧16384字，O每帧32768字。已核对帧数、每帧长度及终止标记；两类非法长度的32768个O字均为`0x0000000012345678`。JSON只保留有效范围，避免把零填充缓冲重复入库。

官方C输出`sim/tv/cdatafile/c.fsa_stream_split_d.autotvout_o_gmem.dat`与RTL输出`sim/tv/rtldatafile/rtl.fsa_stream_split_d.autotvout_o_gmem.dat`的SHA256均为`b23cfcdb75aff44682af3eafbdf3fbd624ce3ce326f6c886fb184f844ab7276f`，完整文件逐字节一致。

## 2 测试台怎样使用

现有testbench内嵌20个未修改输入事务的1548个Q/K/V字及1032个O字：原8例＋10个边界例＋2个零Q/K基向量例。按名称/长度/causal查找，恢复官方FP16输入，再核对输入FNV-1a64和每个有效O字。只适用于4×4/head16；其他参数不套用这一基线。

恢复输入是必要的：本地与官方宿主产生的随机FP16输入位模式不同，仅使用相同seed不足以保证相同输入。基线输入不从被测输出反推；独立double点积/exp参考和0.03门槛继续执行。

新的重标定与PWL用例改变了输入，不使用旧输出作期望，仍用独立数学参考；新增位模式期望须来自后续获授权的正式运行。

## 3 当前验证范围

本地MinGW＋math stubs编译成功，数学检查24/24，两类非法长度检查通过；严格位模式检查15/20，整项运行exit=1，未放宽或跳过失败判据。相同打包输入下的5个位差事务为primary-noncausal、full-tile-noncausal、full-tile-causal、boundary-causal(L=8)、boundary-causal(L=9)。这表明本地宿主模型不能替代官方位级验收；差异根因本轮未定位。

完整本地输出见[检查记录](local_math_stubs_check.txt)。self-test-nan/inf两段错误诊断是预期检查器自检；5条baseline bit mismatch是实际未通过的严格比较，两者须分开统计。

原官方C/RTL完整O文件一致，说明归档基线有官方来源。当前修改后的测试台尚未运行新CSim/CoSim，不能将历史26/26写成新增严格检查已通过。新PWL事务长度25，原周期表属于旧L=9版本，跨版本仅比较同输入事务。
