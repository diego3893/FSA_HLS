# FSA-HLS

FSA-HLS 是把原 [VCA-EPFL/FSA: FSA: Fusing FlashAttention within a Single Systolic Array](https://github.com/VCA-EPFL/FSA)Chisel 项目逐步迁移为 HLS C++ 的工程。

## 快速运行C++模块测试

### 适用于cpp_version分支

在PowerShell中进入项目根目录后，可以使用通用脚本编译并运行测试：

```powershell
# 运行PE测试，对应tests/test_pe.cpp
.\run_test.cmd pe

# 自动运行tests目录中的全部测试
.\run_test.cmd all
```

### 适用于主分支

- 环境要求：Vitis HLS 2024.2

在linux环境中，进入项目根目录

```shell
./run_hls.sh pe
```

可以启动PE模块的综合和仿真，相应的报告和ip保存于`./hls/pe/build/`中

## 迁移范围

整个迁移分为两个阶段。

### 阶段一：核心计算部分

阶段一负责迁移：

- PE；
- CMP；
- InputDelayer 和 OutputDelayer；
- Systolic Array；
- Accumulator；
- Scratchpad 和 Accumulator RAM；
- ExecutionPlan；
- MatrixEngineController；
- 核心 FSA 顶层连接。

阶段一的目标是让下面的数据通路在 HLS C++ 中完整表达：

```text
Scratchpad
    ↓
InputDelayer
    ↓
Systolic Array（PE + CMP）
    ↓
OutputDelayer
    ↓
Accumulator ↔ Accumulator RAM
```

### 请求级核心顶层

`fsa_core_request_top` 是当前不做指令重叠的请求级入口。外部一次提供一个
4×4 Q/K/V tile，顶层内部使用单 FSM `MatrixEngineController` 顺序执行：

```text
Q/K/V写入Scratchpad
-> LOAD_STATIONARY
-> ATTENTION_SCORE_COMPUTE
-> ATTENTION_VALUE_COMPUTE
-> 可选LSE_NORM_SCALE和LSE_NORM
-> 从Accumulator RAM返回L/O
```

- 第一块设置 `initialize=true, finalize=false`；
- 中间块设置 `initialize=false, finalize=false`，复用非零旧 L/O 和 CMP max；
- 最后一块设置 `initialize=false, finalize=true`，完成 O/L 归一化；
- 只有一个KV block时设置 `initialize=true, finalize=true`。

当前版本有意不实现 Chisel 的双 FSM `conflictFree` 指令重叠。HLS 入口为：

```bash
./run_hls.sh fsa_core_request
```

### 固定tile DMA顶层

`fsa_dma_top` 是当前最小的DDR功能闭环入口。外部通过AXI-Lite设置Q、K、
VT和OL四个64位DDR基地址以及`causal`，写`ap_start`后，IP使用一个64-bit
AXI master串行完成搬入、单个4×4 tile计算和写回。

- Q布局：`[query][feature]`，16个FP16，共4个64-bit beat；
- K布局：`[key][feature]`，16个FP16，共4个64-bit beat；
- VT布局：`[value_feature][key]`，16个FP16，共4个64-bit beat；
- OL布局：先放4个FP32的L，再放query-major的16个FP32 O，共10个beat；
- 每次start都是独立请求，当前不支持多个KV block、outstanding或DMA/计算重叠；
- `status=0`表示成功，`status=1`表示计算核协议错误。

HLS入口为：

```bash
./run_hls.sh fsa_dma
```

### 完整序列streaming v2顶层

`fsa_streaming_v2_top`保留旧顶层不动，新增一次`ap_start`完成整个序列的
高吞吐路径。AXI-Lite参数是`sequence_length`、`causal`以及row-major
Q/K/V/O四个DDR基地址；tile边界和在线softmax状态不再暴露给调用者。

内部采用如下DATAFLOW任务图：

```text
Q/K/V独立AXI DMA
        ↓ stream
双缓冲Scratchpad SRAM（Q复用、K/V ping-pong）
        ↓ stream
SA_ROWS × SA_COLS PE + 每列CMP
        ↓ tile stream
Accumulator SRAM（L/O）+ 最终reciprocal
        ↓ stream
O AXI DMA
```

默认4×4配置下，长度9会由硬件内部完成3个query tile和3个KV tile，软件
只调用一次顶层。四个存储器端口使用独立AXI bundle，并允许512-bit自动
widen和多笔outstanding burst。

```bash
./run_hls.sh fsa_streaming_v2
```

### 阶段二：DMA 与外围部分

阶段二负责迁移：

- DMA Request Partitioner；
- LoadQueue 和 StoreQueue；
- 指令 Decoder；
- Semaphores；
- Fence 和系统运行状态；
- AXI4FSA 顶层接口。

阶段二只通过阶段一预留的接口连接核心，不应重新修改 PE、CMP 等模块的内部算法。
