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

### 阶段二：DMA 与外围部分

阶段二负责迁移：

- DMA Request Partitioner；
- LoadQueue 和 StoreQueue；
- 指令 Decoder；
- Semaphores；
- Fence 和系统运行状态；
- AXI4FSA 顶层接口。

阶段二只通过阶段一预留的接口连接核心，不应重新修改 PE、CMP 等模块的内部算法。
