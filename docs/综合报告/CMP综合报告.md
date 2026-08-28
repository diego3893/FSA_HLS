# CMP综合报告

## 1. 综合配置

| 项目 | 配置 |
|---|---|
| HLS工具 | Vitis HLS 2024.2 |
| 顶层函数 | `cmp_top` |
| 目标器件 | `xcvu37p_CIV-fsvh2892-2-e` |
| 目标时钟周期 | 10 ns（100 MHz） |
| 顶层控制协议 | `ap_ctrl_hs` |
| 输入接口 | `input_r`，51 bit，`ap_none` |
| 输出接口 | `output_r`，50 bit，`ap_none` |

参与综合的设计文件：

```text
src/hls/cmp_top.cpp
src/core/cmp.cpp
src/core/arithmetic.cpp
```

使用的testbench：

```text
tests/hls/test_cmp_top.cpp
```

## 2. 流程结果

| 阶段 | 结果 |
|---|---|
| C仿真 | 通过 |
| C综合 | 通过 |
| C/RTL协同仿真 | 通过 |
| Vivado IP导出 | 通过 |

协同仿真完成62次顶层事务，日志结果为：

```text
[PASS] test_cmp_top: complete CMP functional test
C/RTL co-simulation finished: PASS
```

## 3. 时序与吞吐

### 3.1 HLS综合估计

| 项目 | 结果 |
|---|---:|
| Target Clock Period | 10.00 ns |
| Clock Uncertainty | 2.70 ns |
| 有效时序预算 | 7.30 ns |
| Estimated Clock Period | 7.150 ns |
| 估计时序余量 | 约0.15 ns |
| Latency | 1～3拍 |
| Initiation Interval | 2～4拍 |
| Pipeline | no |

HLS估计周期小于扣除Uncertainty后的有效预算，因此10 ns目标在HLS估计阶段通过。不过余量只有约0.15 ns，最终是否满足时序仍应以Vivado布局布线结果为准。

当前事务顶层没有流水化，不能每拍接受一次新事务。该结果适合评价独立CMP IP，但不等同于CMP放入脉动阵列后的最终吞吐率。

### 3.2 C/RTL协同仿真

| 项目 | 最小值 | 平均值 | 最大值 |
|---|---:|---:|---:|
| Latency | 1拍 | 2拍 | 3拍 |
| Interval | 2拍 | 3拍 | 4拍 |

协同仿真总执行时间为223拍，Verilog状态为`Pass`。

## 4. 资源使用

| 资源 | 单个CMP使用量 | 器件可用量 | 占比 |
|---|---:|---:|---:|
| BRAM_18K | 0 | 4032 | 0% |
| DSP | 2 | 9024 | 约0.02% |
| FF | 376 | 2607360 | 约0.01% |
| LUT | 695 | 1303680 | 约0.05% |
| URAM | 0 | 960 | 0% |

2个DSP主要用于FP32减法单元。8项PWL截距常量表被实现为32个FF和33个LUT，没有使用BRAM。

按当前`SA_COLS = 4`简单估算4个CMP：

| 资源 | 4个CMP估算 | 器件占比 |
|---|---:|---:|
| DSP | 8 | 约0.09% |
| FF | 1504 | 约0.06% |
| LUT | 2780 | 约0.21% |

该估算不包含PE、Delayer、Accumulator、片上存储和控制器，也不考虑整块综合时的共享与优化。

## 5. 已验证功能

当前testbench和C/RTL协同仿真已经验证：

- 顶层复位、无效控制和跨事务状态保持；
- `UPDATE`输出FP16位模式并更新`newMax`；
- `PROP_MAX`输出`0 - newMax`；
- `PROP_MAX_DIFF`输出`oldMax - newMax`并推进`oldMax`；
- `PROP_ZERO`输出0且不修改最大值状态；
- `RESET`清空`oldMax`和`newMax`并使本拍数据无效；
- `causalCounter`屏蔽输入、逐级递减并在0处保持；
- FP32数值1.1转换为FP16位模式`0x3c66`；
- `PROP_EXP2_INTERCEPTS`依次输出8个编码截距；
- 连续输出两轮截距时，`exp2_counter`正确回绕；
- `RESET`命令不清`exp2_counter`，而顶层reset会将其清零；
- 传播0和PWL截距时不破坏最大值状态。

## 6. 尚未验证范围

本报告仍不能证明以下内容：

- CMP截距与PE斜率在完整SA中的逐拍对齐；
- 多个CMP之间控制信号和`causalCounter`的阵列级传递；
- 与原Chisel仿真进行独立的完整位级交叉对比；
- NaN等非FSA正常工作范围输入的行为；
- CMP与PE、Delayer连接后的整体II、资源和时序；
- Vivado布局布线后的最终时序。

## 7. 需要关注的工具警告

协同仿真通过，但日志中仍有一条FP32转FP16单元端口位宽警告：

```text
actual bit length 17 differs from formal bit length 16
```

完整testbench已经检查普通数值、舍入值和无穷值的转换结果，并通过RTL协同仿真。不过后续仍应检查生成RTL中多出的1位是否只是浮点IP的内部状态位。

工具还提示`output_r`使用`ap_none`时没有独立的端口级有效信号。当前结构体内部包含`d_output.valid`，顶层事务还由`ap_done`结束；集成IP时仍必须按照`ap_ctrl_hs`握手读取输出。

## 8. 结论

CMP已经完成C仿真、C综合、62次C/RTL协同仿真事务和Vivado IP导出。全部6种命令、最大值状态、causal mask和PWL截距计数器均通过测试。

当前CMP可以判定为：

> 单CMP功能验证合格，可以进入SA级联合验证。

该结论只针对独立CMP顶层。由于当前顶层未流水化、时序余量较小，且尚未验证完整SA中的逐拍连接，因此不能据此认定整个CMP阵列已经满足最终吞吐和时序要求。

## 9. 后续工作

1. 联合PE和InputDelayer验证截距、斜率与控制信号对齐；
2. 在SA顶层验证多个CMP之间的控制和causal mask传递；
3. 在SA顶层重新评估II、资源和时序；
4. 检查FP32转FP16单元的端口位宽警告；
5. 使用Vivado布局布线结果确认最终时序。

## 10. 相关文件

```text
build/cmp_build/solution1/syn/report/cmp_top_csynth.rpt
build/cmp_build/solution1/sim/report/cmp_top_cosim.rpt
build/cmp_build/solution1/sim/report/verilog/cmp_top.log
build/cmp_build/solution1/syn/verilog/cmp_top.v
build/cmp_build/solution1/impl/ip/component.xml
build/cmp_build/solution1/impl/ip/xilinx_com_hls_cmp_top_1_0.zip
```

服务器重新执行：

```bash
./run_hls.sh cmp
```
