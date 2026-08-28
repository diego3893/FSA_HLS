# 阶段 1：重新导出 U280 HLS IP

先运行`vitis_hls -version`并确认是2020.2。本阶段必须重新导出2020.2 IP，不能把2024.2
导出的IP直接放入最终2020.2工程。

执行：

```bash
set -o pipefail
vitis_hls -f 01_hls_ip/run_hls_u280.tcl \
  2>&1 | tee reports/01_hls_u280_console.log
python3 01_hls_ip/unpack_ip.py
```

`set -o pipefail`很重要：没有它时，流水线的退出码通常来自`tee`，HLS失败也可能让终端看起来
像成功。第一条流水线必须返回0后才能执行解压命令。

第一条命令依次运行 C simulation、C synthesis、RTL co-simulation 和 IP export。
第二条命令把导出内容放入 `ip_repo/fsa_dma_top/`。必须确认：

- 日志先打印`HLS_PROJECT_DIR=.../build/hls_fsa_dma_u280`；脚本会先进入`build/`，再以
  纯项目名调用`open_project`，以兼容2020.2不接受绝对项目路径的限制；

- C simulation日志出现 `[PASS] test_fsa_dma_top`；
- `solution1/syn/report/fsa_dma_top_csynth.rpt` 中目标时钟为 10 ns；
- `solution1/sim/report/fsa_dma_top_cosim.rpt` 的 Verilog结果为 PASS；
- `python3`命令最后打印 `IP_UNPACK_PASS=...component.xml`；
- 新IP的 ReleaseNotes目标器件是 `xcu280`，不是`xcvu37p_CIV`。

不要用`solution1/`下面暂时只有`csim/`来判断综合失败。`csynth_design`开始后，HLS会先做
源码分析、展开和调度，`solution1/syn/report/fsa_dma_top_csynth.rpt`只会在C综合完成后出现。
运行中可用下面的命令判断当前阶段：

```bash
grep -nE 'Running: (csim_design|csynth_design|cosim_design|export_design)|Finished Command (csim_design|csynth_design|cosim_design|export_design)|ERROR:' \
  reports/01_hls_u280_console.log | tail -n 80
```

验收C综合完成时，必须同时满足：日志出现`Finished Command csynth_design`、不存在`ERROR:`、
并且`solution1/syn/report/fsa_dma_top_csynth.rpt`存在。若日志最后仍停留在
`Running: csynth_design`或源码分析信息，只表示该步骤尚未结束，应继续等待进程返回。

Vitis HLS 2020.2的`ARRAY_PARTITION`和`ARRAY_RESHAPE` pragma把分区类型写成位置参数：
`variable=... complete dim=...`。如果日志出现`unexpected pragma parameter 'type'`，说明服务器
仍在使用含`type=complete`的旧源码；该警告可能导致分区/重排未生效，不能把这一轮综合作为
有效基线。同步新版源码后重跑，并执行：

```bash
grep -n "unexpected pragma parameter 'type'" reports/01_hls_u280_console.log
```

正确结果是没有任何输出。来自Xilinx头文件`hls_hotbm_apfixed.h`的
`resource pragma is deprecated`与本项目数组分区无关，可单独记录但不作为失败条件。

若CSIM通过，但CSYNTH在`/usr/include/features-time64.h`报告
`'bits/wordsize.h' file not found`，这是Vitis HLS 2020.2综合Clang与新版Ubuntu multiarch
头文件布局的兼容问题。脚本会通过`gcc -print-multiarch`探测目录，并在日志打印例如：

```text
HOST_MULTIARCH_INCLUDE=/usr/include/x86_64-linux-gnu
```

重新运行前先确认该文件实际存在：

```bash
test -f /usr/include/x86_64-linux-gnu/bits/wordsize.h \
  && echo MULTIARCH_HEADER_PRESENT \
  || echo MULTIARCH_HEADER_MISSING
```

若打印`MISSING`，不要从其他Vivado版本复制glibc头文件；应让管理员补齐当前Ubuntu架构的
C开发头文件，或在受支持的Ubuntu 20.04环境中运行2020.2。

若随后报告`Cannot reshape array 'state.acc_ram.SubBankSize'`、
`Cannot reshape array 'state.acc_ram.banks'`或
`Cannot partition array 'state.acc_ram.banks'`，这是2020.2处理嵌套结构体四维数组的兼容问题。
当前实现采取两项规避措施：`SubBankSize`等纯编译期值改为等价枚举常量；banks优化指令只在
实际访问存储的`bankedSRAMStep`中声明。第一、第二维继续完整分割；第四维不再显式声明
`ARRAY_RESHAPE`或`ARRAY_PARTITION`，而由已完全展开的`element`循环提供编译期固定下标。
真实数组形状、地址边界和读写数值不变。重新综合后必须以CSYNTH资源、存储映射和II报告
验收工具最终选择的物理实现。

若CSYNTH不再报告数组指令错误，但在LLVM `Module Verifier`阶段打印
`[4 x [2 x float]]* %output.6`、`Broken module found`并core dump，问题对象是内部
`FsaCoreStepOutput::acc_dma_read_data[4][2]`。2020.2在把这个二维float数组作为引用参数
传过`advanceDatapath`边界时可能生成非法IR。当前实现把它等价展平为8个float，并通过
`accDmaReadDataIndex(port, element)`访问；同时改用逐字段清零，避免对含数组输出结构体执行
聚合赋值。该信号只存在于Core内部，不改变`fsa_dma_top`的AXI端口或O矩阵布局。

若展平后仍在`advanceDatapath193`一类函数名上发生同样的Module Verifier崩溃，说明错误来自
`advanceDatapath`独立LLVM函数边界本身。当前实现已删除`advanceDatapath`和返回整个输入结构体
的`registerDatapathInput`，把数据通路调用、步数累加及协议检查原样展开到调度循环中的唯一
调用点。调度循环已有`PIPELINE II=39`，该约束继续保留；重新综合后仍需用报告检查实际II和
Estimated Clock，因为原先用于切分组合路径的独立RTL层次不再存在。

若2020.2报告`FSA_DMA_AXI_QKV_DEPTH`或`FSA_DMA_AXI_O_DEPTH`为未声明标识符，说明服务器
仍在使用旧版`src/hls/fsa_dma_top.cpp`。新版使用两层`_Pragma`宏，确保2020.2在解析
`m_axi depth=`之前把编译参数展开为Q/K/V深度4096和O深度8192。
