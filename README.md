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

### 阶段二：DMA 与外围部分

阶段二负责迁移：

- DMA Request Partitioner；
- LoadQueue 和 StoreQueue；
- 指令 Decoder；
- Semaphores；
- Fence 和系统运行状态；
- AXI4FSA 顶层接口。

阶段二只通过阶段一预留的接口连接核心，不应重新修改 PE、CMP 等模块的内部算法。