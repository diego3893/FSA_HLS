# FSA-HLS

FSA-HLS 是把原 [VCA-EPFL/FSA: FSA: Fusing FlashAttention within a Single Systolic Array](https://github.com/VCA-EPFL/FSA)Chisel 项目逐步迁移为 HLS C++ 的工程。

本项目的目标不是在 CPU 上重新实现一次 FlashAttention，而是用适合 HLS 的 C++ 描述 FSA 的计算模块、片上存储和外围接口，最终将它们综合成 FPGA 硬件。

## 1 迁移范围

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

