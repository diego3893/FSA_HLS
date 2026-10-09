# 25d6dda的阶段1/2验收证据

被测commit：`25d6dda678104d8a856292b83c1c48607a1148fd`。标准入口、4×4/head16；2026-10-10 01:12:31—01:20:01。源目录为服务器`hls/fsa_stream_split_d/fsa_stream_split_d_build/solution1`，取证时远端HEAD仍为被测commit。

- `official_run.txt`：完整标准流程日志及`RUN_EXIT=0`。CSim→CSynth→CoSim，不导出IP。
- `csim.txt`：官方CSim，数学24/24、严格20/20。
- `*_csynth.rpt/xml`：顶层、PE/Acc、QK/ROW_SUM/PWL/PV及父控制层原始报告。
- `fsa_stream_split_d_cosim.rpt`、`result.transaction.rpt`：CoSim结果和26事务周期。
- `rtl_arithmetic_instances.txt`：最终Verilog例化位置；fsub包装器内部IP实例行另列，不把它额外算作第9条减法通路。
- `rtl_storage_audit.txt`：QK、PWL、PV及父层的状态声明、写事件、RAM参数；附各文件SHA256。
- `source_sha256.json`：全部生成Verilog及完整TV输入/O文件哈希。原RTL本地只读归档在ignored的`build/stage12_25d6dda/`，不整套提交生成物。
- `baseline.json`：24个有效事务的有效范围Q/K/V/O、指纹；非法事务全32768个O字canary核对。20个未改输入事务已与旧基线逐word比较一致。新增4例输出作为官方快照，尚未内嵌为额外严格比较项。

TV二进制格式同旧基线：大端uint64字数＋打包字，共26帧，终止标记`0x5a5aa5a50f0ff0f0`；Q/K/V每帧16384字、O每帧32768字。完整C与RTL的O文件逐字节一致，SHA256=`73dd80b27e9c69e27660b01194bdad25a5c4bcefdd4b9b11fa804edc655e7bd0`。

综合硬件资源/II/周期均保持原基线；20项同输入事务周期逐项保持。PWL改为L25，不能把全套94158 cycles与旧L9的66426直接解释成硬件退化。

存储审计区分源码字段和物理阶段寄存器：算法上没有额外S/P备份，HLS仍生成多组阶段版本，详见[验收报告](../../综合报告/Split-D阶段1与2验收_20261010.md)。
