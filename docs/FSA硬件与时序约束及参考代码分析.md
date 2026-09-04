# FSA 硬件与时序约束及参考代码分析

本文基于 `FSA-main/` 当前源码和 `参考代码/` 下的两个 HLS 项目整理。它有两个用途：

1. 作为修改 FSA 参数、数据通路、算术单元或 `ExecutionPlan` 时必须保持的设计约束；
2. 作为把现有 Chisel FSA 迁移、重写或优化为 HLS 实现时的架构参考。

文中的周期均以一条 matrix 指令开始执行的时刻为 `t=0`。区间 `[a,b]` 包含首尾；“总周期数”是按当前静态计划计算的执行时间，不包含指令队列、DMA 未就绪和片外存储器延迟。

符号约定：

| 符号 | 含义 |
|---|---|
| `R` | `saRows`，物理脉动阵列的 PE 行数 |
| `C` | `saCols`，物理脉动阵列的 PE 列数 |
| `P` | `exp2PwlPieces`，exp2 分段线性近似的段数；当前浮点实现默认 `P=8` |
| `D` | `reciprocalLatency`，倒数单元延迟；当前 fp32 accumulator、`divBitsPerCycle=2` 时 `D=14` |
| `E` | `AttentionScore` 中 exp2 最后一拍，`E=2R+P+3` |

---

# 第一部分：FSA 硬件配置与时序约束

## 1. 参数化硬件结构：不是固定 4×4

### 1.1 `R×C` 的物理含义

`FSAParams(saRows, saCols, ...)` 本身接受独立的 `R` 和 `C`。仓库中的 `fsa4x4`、`fsa8x8`、`fsa16x16` 等只是预置配置，不代表微架构只能是方阵。

物理资源随参数展开为：

| 结构 | 数量或宽度 |
|---|---|
| PE 阵列 | `R×C` 个 PE |
| 顶部 CMP | `C` 个，每列一个 |
| 向量 Accumulator 单元 | `C` 路，每列一路 |
| scratchpad 完整行 | `R` 个 `elemType` 元素 |
| accumulator SRAM 完整行 | `C` 个 `accType` 元素 |
| 输入阶梯延迟最大深度 | `R-1` 拍 |
| 输出阶梯延迟最大深度 | `C-1` 拍 |

每个 PE 包含一个 `MacUnit` 和一个元素精度寄存器 `reg`。`reg` 在不同阶段依次保存 Q、score/归一化前中间值和概率 P。数据可左到右传播，累加值可上到下或下到上传播；顶部 CMP 维护每个阵列列对应的 `oldMax/newMax`。

### 1.2 `R`、`C` 与 FlashAttention tile 的对应关系

当前 Python kernel 对矩形阵列的实际映射是：

| 算法维度或 tile | 当前映射 |
|---|---|
| head/feature 维 `d` | `R` |
| query block 高度 `Br` | `C` |
| key/value block 高度 `Bc` | `R` |
| Q tile | `[C,R]` |
| K tile | `[R,R]` |
| 转置后的 V tile `V_t` | `[R,R]` |
| softmax 分母 L | accumulator 中 `[1,C]` |
| 转置输出 `O_t` | accumulator 中 `[R,C]` |
| score/P 的阵列内布局 | 逻辑上与 `[C,R]` 对应，并以转置、反向的方式分布在 `R×C` 个 PE 中 |

因此，“支持非 4×4”与“所有算法维度任意”不是一回事。当前代码允许 `R!=C`，但软件 tile 约定仍把 `d` 和 `Bc` 同时绑定到 `R`，把 `Br` 绑定到 `C`。如果今后要让 `d`、`Br`、`Bc` 三者彼此独立，不能只改 `FSAParams`，还必须同时改 tile 分解、scratchpad 行格式、装载次数、PE 数据布局和全部执行计划。

### 1.3 数据类型与算术结构

默认 `WithFpFSA` 使用 fp16 乘法输入与 fp32 累加：

- scratchpad、PE 横向数据和 `PE.reg` 使用 `elemType`；
- PE 竖向部分和、CMP、Accumulator、accumulator SRAM 使用 `accType`；
- `FPMacUnit` 在普通模式做 `a*b+c`，在 exp2 模式做分段线性近似；
- 每列 Accumulator 另有一个 MAC/exp2/reciprocal 单元和一个 `scale` 寄存器；
- `attentionScale=log2(e)/sqrt(R)`，所以修改 `R` 会同时改变硬件 tile 形状和缩放常量。

代码还提供 bf16/fp32 等算术配置。修改精度时必须重新检查 SRAM 行字节数、DMA beat 整除性、PWL 常量位宽、资源量和时序，而不能只替换类型名。

## 2. 片上存储与 DMA 配置

### 2.1 默认容量公式

`Configs.defaultFSAParams(R,C,M)` 分配：

```text
spadRows = 2C + 4R
accRows  = 1 + R
nMemPorts = M
```

其来源是：

- scratchpad：两个 Q buffer，各占 `C` 行；K、V 各双缓冲，共四个 `R` 行 tile；
- accumulator SRAM：L 占 1 行，`O_t` 占 `R` 行。

相应容量为：

```text
scratchpad bits = (2C + 4R) × R × elemWidth
accumulator bits = (1 + R) × C × accWidth
```

以上是当前 Python kernel 的最低布局需求。若增加并行 tile 数、三缓冲、额外中间量或改变 Q/K/V/O 的布局，必须先重新推导容量，而不是继续沿用这两个公式。

### 2.2 SRAM 访问粒度

两块 SRAM 都是按“逻辑行”寻址：

- matrix engine 对 scratchpad 每拍完整读取 `R` 个元素；DMA 以窄写端口按 memory beat 填入；
- matrix engine 对 accumulator SRAM 每拍完整读写 `C` 个元素；DMA 以窄读端口把结果取走；
- SRAM 读有 1 拍延迟；`ExecutionPlan`、常量选择和 read-modify-write 写回均已围绕这 1 拍延迟排程。

每个逻辑行被拆成 `nSubBanks` 个 memory-beat 宽的子 bank：

```text
scratchpad: nSubBanks = R × elemWidth / (8 × beatBytes)
accumulator: nSubBanks = C × accWidth / (8 × beatBytes)
```

两种行宽都必须能被 `beatBytes` 整除，否则 `BankedSRAM` elaboration 会失败。改变 `R`、`C`、精度或 AXI beat 宽度时，这是首先要检查的硬约束。

### 2.3 bank、端口和并发约束

- `spadBanks`、`accBanks` 默认均为 2；地址低位直接用作 bank 编号，因此后续配置应保持为 2 的幂。当前实现没有为非 2 的幂提供完整映射保护。
- `nMemPorts` 在 DMA 中被明确要求为 2 的幂；现有预置为 4、8 或 16。
- 每个物理 sub-bank 是单读单写 SRAM。多个同类端口同拍命中同一 bank/sub-bank 时按端口顺序仲裁。
- FSA core 对关键内部 SRAM 请求有“不允许反压”的断言。因此修改地址分配或访问时序后，必须证明 matrix full access 与 DMA narrow access 不会产生未计划的冲突。
- 默认队列参数为：原始 32-bit 指令队列 256 项、matrix inflight 队列 8 项、DMA load/store inflight 分别 16/8。
- DMA 可有多个 AXI master port，并用 request partitioner 拆分请求。

## 3. 控制系统与周期解释

### 3.1 ExecutionPlan 是硬件微程序

`ExecutionPlan.scala` 不实例化计算单元，而是描述每类 matrix 指令在第几拍产生以下控制：

1. scratchpad 读或常量注入；
2. PE 的 9 个控制位；
3. CMP 命令；
4. accumulator SRAM 读；
5. Accumulator 命令。

普通描述符的有效区间是 `[cycle, cycle+repeat-1]`。PE 控制还要沿阵列行方向传播：

```text
parallel(start, repeat):  总窗口 [start, start+repeat-1]
flow_up/down(start,repeat): 总窗口 [start, start+repeat+R-2]
```

`repeat=1` 的 flow 不是只作用一个 PE；它表示发出一条控制波，仍需 `R-1` 拍走完整个阵列高度。

### 3.2 compute 与 accumulate 两类时序

ExecutionPlan 把控制描述分成两类：

- `computeTimer`：驱动 scratchpad、PE 和 CMP；
- `accumTimer`：驱动 accumulator SRAM 和 Accumulator。

`accStartCycle` 等于“最早 accumulator 描述符周期”和 `computeMaxCycle` 中的较小者。Accumulator 时序可以在阵列 compute 尚未结束时开始，也可以在 compute 结束后继续。因此一条指令的完整静态周期应取两类时序结束边界的最大值。

## 4. 五类 ExecutionPlan 的参数化时序

### 4.1 汇总表

| 指令 | compute 有效周期 | 最晚结束周期 | 总周期数 |
|---|---:|---:|---:|
| `LOAD_STATIONARY` | `[0,C]` | `C` | `C+1` |
| `ATTENTION_SCORE` | `[0,3R+P+3]` | `3R+C+P+3` | `3R+C+P+4` |
| `ATTENTION_VALUE` | `[0,2R-1]` | `2R+C-1` | `2R+C` |
| `ATTENTION_LSE_NORM_SCALE` | 无阵列 compute | `D+1` | `D+2` |
| `ATTENTION_LSE_NORM` | CMP reset 在 `t=0` | `R` | `R+1` |

其中“总周期数”取 compute/accumulator 两类时序的最大结束边界，适合做静态回归断言和单指令时间估算。端到端 kernel 时间还会受 DMA 和指令供给影响。

当前默认 `P=8`、`D=14`。以 4×4 仅作公式校验示例，各指令独立占用时间分别为 5、28、12、16、5 拍；这些数值不是硬编码规格。

### 4.2 `LOAD_STATIONARY`：装载 Q

| 周期 | 动作 |
|---:|---|
| `[0,C-1]` | 连续读 Q 的 `C` 个 scratchpad 行 |
| `[1,C]` | `load_reg_li.parallel`，Q 从左向右传播并装入各 PE 的 `reg` |
| `C` | 最后一拍 PE 装载完成 |

约束：该计划的长度由列数 `C` 决定，不是由 `R` 决定。修改 Q tile 高度或 PE 横向传播结构时，必须同步修改 scratchpad 读取次数、装载窗口和冲突释放点。

### 4.3 `ATTENTION_SCORE`：QK、在线 softmax 前半段与 L 更新

该计划完成当前 K block 的：

```text
S = QK
newMax = max(oldMax, rowmax(S))
P = exp((S-newMax)/sqrt(R))
Lnew = exp(oldMax-newMax) × Lold + rowsum(P)
```

详细阶段：

| 周期 | 阶段与主要动作 |
|---:|---|
| `[0,R-1]` | 连续读取 K 的 `R` 个 scratchpad 行 |
| PE 控制源从 `1` 开始，整体至 `2R-1` | QK MAC 控制自底向上形成波前，K 同步向右流动 |
| `[R+1,2R]` | 顶部 CMP 逐拍 `UPDATE`：更新 `newMax`，同时把 score 向下回流 |
| 回流控制整体至 `3R-1` | score 继续沿列下行到各 PE |
| 控制源从 `R+4` 开始，整体至 `3R+2` | 自底向上送 0，为后续向下累加通路准备边界值 |
| `2R+1` | 所有 PE 从上方保存回流的 S；CMP 下发 `-newMax`；左侧注入 1 |
| 控制源 `2R+2`，整体至 `3R+1` | PE 计算并保存 `S-newMax`；CMP 同时产生 `oldMax-newMax` |
| 控制源 `2R+3`，整体至 `3R+2` | 左侧注入 `attentionScale`，PE 保存 `(S-newMax)×log2(e)/sqrt(R)` |
| `[2R+3,2R+P+2]` | CMP 和左侧常量通路分别发出 PWL intercept、slope |
| exp2 控制源 `[2R+4,2R+P+3]`，整体至 `3R+P+2` | 各行依次完成 exp2，结果 P 留在 `PE.reg` |
| `E=2R+P+3` | CMP 发 0、左侧发 1；PE.reg 中的 P 已经形成，开始准备 row sum |
| row-sum 控制源 `E+1`，整体至 `3R+P+3` | PE 自顶向下计算 `P×1+partial_sum` |
| `3R+C+2` | Accumulator `EXP_S1`：把 `oldMax-newMax` 乘 attention scale |
| `3R+C+3` | Accumulator `EXP_S2`：得到 `exp(oldMax-newMax)` 缩放因子 |
| `E+R+C-1 = 3R+C+P+2` | 读旧 L |
| `E+R+C = 3R+C+P+3` | `ACC_SA` 合并旧 L 与当前 block row sum，并写回 |

关键约束：

- `P` 会改变 exp2 常量发送长度、score 的 conflict-free 点、compute 尾部和 L 写回时刻；修改 PWL 段数时必须改的是参数和公式，不能只换常量表。
- CMP 的 `PROP_MAX` 当前实际通过 `out_diff` 送出 `0-newMax`；重命名或改 CMP 数据选择时要保持这一数值语义。
- causal mask 由 CMP 控制波中的 `causalCounter` 逐列递减实现；修改列传播延迟会改变 mask 对齐。

### 4.4 `ATTENTION_VALUE`：PV 与 O 更新

该计划复用 `PE.reg` 中的 P，读取转置后的 V，计算并在线更新输出：

```text
Onew = exp(oldMax-newMax) × Oold + P×V
```

| 周期 | 动作 |
|---:|---|
| `[0,R-1]` | 连续读取 `V_t` 的 `R` 个 scratchpad 行 |
| MAC 控制源从 `1` 开始，整体至 `2R-1` | `acc_ui=true`，P×V 的部分和自顶向下传播 |
| `[R+C-1,2R+C-2]` | 连续 `R` 拍读旧 `O_t` 行 |
| `[R+C,2R+C-1]` | 连续 `R` 拍执行 `ACC_SA` 并写回新 O |

约束：V 与 O 的行数都绑定到 `R`，O 的行宽绑定到 `C`；输出对齐还包含 `C-1` 拍的阶梯延迟。任何一处 tile 形状变化都必须重新推导 `R+C-1` 这一 accumulator 读起点。

### 4.5 `ATTENTION_LSE_NORM_SCALE`：计算 `1/L`

| 周期 | 动作 |
|---:|---|
| `0` | 读 L，`rmw=false` |
| `1` | `SET_SCALE`，把 L 保存到每列 Accumulator 的 scale |
| `[2,D+1]` | 发起并等待多周期 `RECIPROCAL`，最后一拍得到 `1/L` |

当前 EasyFloat 除法延迟公式为：

```text
D = ceil((accMantissaWidth + 3) / divBitsPerCycle) + 2
```

默认 fp32 accumulator 为 `accMantissaWidth=23`、`divBitsPerCycle=2`，所以 `D=14`，本指令共 16 拍。改变除法器每拍处理位数或 accumulator 尾数宽度时，ExecutionPlan 会通过 `reciprocalLatency` 自动变化，但仍需验证实际 `out_valid` 与最后控制拍对齐。

### 4.6 `ATTENTION_LSE_NORM`：最终 `O/L`

| 周期 | 动作 |
|---:|---|
| `0` | CMP `RESET`，为下一 query block 清空 `oldMax/newMax`；同时开始读 O |
| `[0,R-1]` | 连续读取 `R` 行 O |
| `[1,R]` | 用已保存的 `1/L` 执行 `ACC`：`O <- scale×O` 并写回 |

## 5. 今后修改 FSA 时的强制检查清单

### 5.1 改 `R`、`C` 或 tile 形状

- 确认软件仍满足 `d=R`、`Br=C`、`Bc=R`，或把所有依赖这一映射的代码一起改掉；
- 重新计算 `spadRows=2C+4R`、`accRows=R+1` 是否仍覆盖实际 buffer；
- 检查 scratchpad 行 `R×elemWidth` 和 accumulator 行 `C×accWidth` 均为整数个 DMA beat；
- 检查输入/输出阶梯延迟、反转方向和 Q/K/V/O 地址 stride；
- 用本节公式重新生成 5 类计划的阶段起止和完成周期；
- 特别验证矩形配置，而不只跑方阵和 4×4 波形。

### 5.2 改算术延迟、PWL 或流水寄存器

- 更新 `HasArithmeticParams` 暴露的 `P`、`D` 等延迟，而不是在控制器中散落常数；
- 重新对齐 SRAM 1 拍读延迟、CMP 横向每列 1 拍、PE 纵横传播和 OutputDelayer；
- 重新验证 exp2 只写一次 `PE.reg`，PWL slope/intercept 段号一致；
- 重新验证 `PROP_MAX_DIFF -> EXP_S1 -> EXP_S2 -> L/O ACC_SA` 的数据到达关系；

### 5.3 改存储、bank 或 DMA

- 保持 `nMemPorts` 为 2 的幂，并优先保持 bank 数为 2 的幂；
- 检查 full/narrow 同拍访问不会触发 core 中的 ready 断言；
- 检查多端口 DMA 的所有 load queue 或 store queue 同步入队、同步完成假设；
- 更新生成给 Python 的 `FSAConfig.json` 中 SRAM 容量、精度和对齐信息；

### 5.4 最低验证集合

- 至少选择一个小矩形配置和一个目标规模配置；
- 对每类 matrix 指令检查各阶段首拍、末拍和最终完成周期；
- 覆盖第一个 K/V block（L/O 从 0 开始）与后续 block（read-modify-write）；
- 覆盖 causal 对角 block；
- 比较硬件、PyEasyFloat 和 PyTorch，除整体误差外检查 row max、L、P、O 中间值；

---

# 第二部分：两个参考项目的架构与高性能方法

## 7. `gemm_hls`：通信规避的可扩展矩阵乘

### 7.1 总体架构

该项目是面向 Vitis/OpenCL 平台的纯 HLS GEMM。顶层有 A、B、C 三个独立 `m_axi` bundle，并用顶层 `DATAFLOW` 把整个 kernel 拆为并行进程：

```text
A AXI -> ReadA / TransposeA / width convert -> A stream --+
                                                        |
B AXI -> ReadB / width convert / FeedB -> B stream -----+-> 1-D PE chain
                                                              |
C AXI <- WriteC <- width convert <- C stream ------------------+
```

这里不是 FSA 的 `R×C` 二维 PE 网格，而是沿 N 方向的一维 PE 链。当前实现 `kComputeTileSizeN=1`，因此 PE 数基本等于 `MM_PARALLELISM_N`；每个 PE 内又对 `MM_PARALLELISM_M` 个 M 方向元素完全展开，形成“PE 数 × 每 PE SIMD 宽度”的二维算术并行度。

### 7.2 分块层次

项目区分三类尺度：

| 层次 | 作用 |
|---|---|
| outer/memory tile `TN×TM` | 决定片上 C buffer 大小和片外通信复用 |
| inner N tile | 与 PE 链的分工和 A buffer 对应 |
| compute tile `PN×PM` | 决定实际乘加并行度；当前 `PN` 粒度固定为 1，`PM` 为每 PE SIMD 宽度 |

每个 PE 内的 `cBuffer` 保存 outer tile 中属于该 PE 的 C 子块，并贯穿完整 K 维累加。因此 C 不会在每个 K block 后写回片外存储器，这是其“communication avoiding”的核心。

### 7.3 主要高性能手段

1. **粗粒度 dataflow**：A 读取/转置、B 读取/复用、PE 计算和 C 写回通过 stream 并行运行。
2. **空间展开**：顶层完全展开 PE 链，PE 内完全展开 M 向量 lane；理想吞吐为：

   ```text
   2 × MM_PARALLELISM_N × MM_PARALLELISM_M × frequency
   ```

3. **A 双缓冲**：`aBuffer[2*kInnerTilesN]` 在计算当前 outer product 时预取下一组 A，目标是在 K 维保持无间断流水。
4. **B 片上重放**：`FeedB` 只在第一个 N inner tile 从上游读取 B，之后从本地 buffer 为其余 N tile 重放，减少片外读取。
5. **C 驻留片上**：完整 K reduction 在 `cBuffer` 中完成，结果仅在 outer tile 结束时写回。
6. **宽总线与独立宽度适配**：外存宽度、A/K/M 三个方向和 kernel SIMD 宽度分别配置，必要时插入 width converter。
7. **A 在线转置**：当主存未预转置 A 时，用多路 column stream 将 burst 读入的数据重排为计算需要的顺序；也支持直接输入转置 A。
8. **II=1、循环展开和 flatten**：关键循环显式 `PIPELINE II=1`、lane `UNROLL`，C 写回采用手工扁平循环以减少 pipeline drain。
9. **编译期专用化**：数据类型、tile、并行度、总线宽度、目标频率和算术资源映射均在 CMake/HLS 编译时确定。
10. **广义 map/reduce**：乘法和加法可替换为其他算子，例如 min-plus，在保持数据流骨架的同时复用计算架构。

### 7.4 设计约束与可迁移经验

关键限制包括：

- memory tile 必须能被 inner/compute tile 和总线包宽整除；
- `kInnerTilesM >= kInnerTilesN`，否则当前 A 双缓冲调度不成立；
- README 建议每 PE 的 M 向量宽度不超过 64 bytes，以避免布线和频率恶化；
- outer tile 越大，通信复用越好，但 `cBuffer` 以 tile 面积增长，且宽并行访问增加 BRAM/URAM banking 与布线压力；
- README 报告大规模设计进一步扩展时的主要瓶颈是跨 SLR 布线，而非算术数量。

对 FSA/HLS 重写最有价值的经验是：把“外存传输宽度”“片上 tile 大小”“计算阵列并行度”作为三个独立旋钮；让 L/O 或 C 一类 reduction 状态尽量驻留片上；用显式双缓冲和 stream dataflow 隐藏数据搬运；同时为路由而限制单个模块的扇出和超宽向量。

不能直接照搬的是一维 PE 链和纯 GEMM 的固定数据流。FSA 同一批 PE 还承担 QK、max/diff、exp2、row sum 和 PV，并具有双向竖直传播及顶部 CMP 状态，因此需要保留阶段化控制或把阶段拆成严格对齐的 HLS dataflow actor。

## 8. `hls-fpga-accelerators`：LLM 基础算子集合

### 8.1 项目组织

该项目不是一个融合加速器，而是一组彼此独立的 HLS kernel：

| kernel | 功能 | 数据遍历方式 |
|---|---|---|
| `matmul` | A×B，B 假设已转置 | 三 AXI 端口，流式读 A/B、计算、写 C |
| `elementwise` | add/multiply | 单遍 packet 流 |
| `unary` | passthrough/ReLU/SiLU | 单遍 packet 流，SiLU 可选 LUT exp |
| `rmsnorm` | 均方、倒平方根、缩放 | 两遍输入 |
| `softmax` | exp 求和、倒数、归一化 | 两遍输入 |

公共配置把 AXI word 定义为 `RawDataT=ap_uint<BUS>`，每个 word 打包：

```text
kPackets = BUS / kDataWidth
```

个元素。典型默认总线为 512 bit。每个 kernel 基本都采用：

```text
m_axi load -> FIFO -> compute -> FIFO -> m_axi store
```

并用 `DATAFLOW` 让 load/compute/store 并发，packet 外循环流水化，packet 内 lane 完全展开。

### 8.2 `matmul` 的方法

- A、B、C 分别使用 `gmem0/1/2`，可由平台映射到不同 memory bank；
- B 以转置形式存储，使一个 dot product 的 K 维数据可连续 burst 读取；
- 每拍宽读一个 packet，packet 内 `kPackets` 个乘法被完全展开；
- A stream 按输出元素所需次数重复，B stream 按 A 的行数重复，从而让 compute 函数只处理顺序流；
- load、compute、store 通过深度 16 的 FIFO 解耦。

这种实现代码短、接口清晰，适合作为 HLS 宽接口、stream 化和简单 SIMD 的入门骨架。但它不像 `gemm_hls` 那样在片上缓存并复用大块 A/B/C：A 和 B 会按输出需求反复从全局内存读取，算术利用率很容易受带宽限制，扩展到大 GEMM 时不应把它当作最终高性能方案。

### 8.3 elementwise、unary、RMSNorm、softmax 的方法

- elementwise/unary 每个 AXI packet 对应 `kPackets` 路并行算子，适合带宽受限的一遍式算子；
- SiLU 的指数可选标准 `hls::exp`，或使用 32 点、区间 `[-6,6]` 的线性插值 LUT，并用定点中间格式降低非线性函数成本；
- RMSNorm 和 softmax 的 load 函数各从片外内存读两遍，避免缓存整个输入；第一遍做 reduction，第二遍施加最终 scale；
- reduction 先在 packet 内完全展开形成 `local_cum`，再累加到全局标量，兼顾总线 SIMD 与跨 packet 累加；
- HLS Tcl 统一配置目标器件、300 MHz 时钟、AXI 64-bit 地址和 dataflow deadlock 检查。

### 8.4 使用前必须注意的正确性与可扩展性边界

该项目适合作为实现手法参考，但当前源码不是可直接替换 FSA 的 production-grade FlashAttention 组件：

1. `softmax(size)` 对整个一维 `size` 只计算一个总和，不按矩阵行分组，也没有先减 row max，既不等价于 attention 的 row-wise softmax，也缺少常用的数值稳定处理。
2. `rmsnorm(size)` 同样对整个扁平输入求一个尺度，且没有常见的可学习权重；用于多行矩阵时必须先明确 normalization domain。
3. softmax/RMSNorm 为两遍片外读取，减少片上容量但增加带宽；若 tile 能放入 BRAM/URAM，应比较“片上缓存一次读取”方案。
4. 所有循环默认 `size`、行列和总线 packet 整除；尾包没有通用 byte mask 或元素 mask。
5. `matmul.h` 当前把 `kARows` 固定为 2，Tcl 虽传入 `A_ROWS` 宏但源码未使用；头文件默认 B/C 列数为 32768，而 Tcl 默认传 4096。修改配置前应统一这几处来源。
6. FLOAT4/FLOAT8 路径仍复用了 half/16-bit union 作为数值容器，低位打包、解包和实际低精度编码语义需要单独验证。
7. packet 内串行 reduction 的浮点关键路径和资源映射可能随 `kPackets` 急剧增长；“总线更宽”不自动等于“频率更高或吞吐更高”。

## 9. 两个参考项目对后续 FSA 修改的综合建议

| 目标 | 优先借鉴 `gemm_hls` | 优先借鉴 `hls-fpga-accelerators` | 对 FSA 的落地方式 |
|---|---|---|---|
| 隐藏片外延迟 | 多 actor dataflow、A 双缓冲、B 重放 | load/compute/store FIFO | 保留 Q/K/V 双缓冲，把 DMA、预处理、阵列和写回做成可验证的流式阶段 |
| 提高带宽利用率 | 独立 memory/kernel width、在线转置 | `BUS/kDataWidth` packet SIMD | 将 AXI beat、SRAM sub-bank 和计算 lane 显式解耦，集中做 width conversion |
| 提高数据复用 | C 驻留片上跨完整 K 累加 | 两遍 reduction 的低容量方案 | L/O/P 优先驻留片上；容量不足时才选择重读，并量化带宽代价 |
| 非线性函数 | 无直接方案 | LUT/线性插值 exp | 延续 FSA slope/intercept PWL，但把误差、段数、资源和周期参数绑定成一组配置 |
| 消除流水气泡 | II=1、手工 flatten、预取 | packet pipeline + unroll | 用参数化周期模型和 overlap 测试守住 `conflictFree`，再做循环/阶段扁平化 |
| 控制路由压力 | 限制向量宽度、识别跨 SLR 瓶颈 | 简单局部 SIMD | 大阵列优先层次化、局部广播和寄存器切分，避免全局高扇出控制 |

建议后续 HLS 版本保持三层清晰边界：

1. **算法层**：在线 softmax 状态更新、causal 规则和精度模型；
2. **tile/存储层**：Q/K/V/L/O/P 布局、双缓冲、bank、总线打包；
3. **周期/数据流层**：actor 间 FIFO、II、算术延迟、波前和允许重叠的资源集合。

只有三层都参数化并分别验证，`R×C` 扩展、数据类型替换和性能优化才不会把当前 ExecutionPlan 中隐含的正确性条件打散。

## 10. 主要源码索引

### FSA

- `FSA-main/src/main/scala/fsa/FSA.scala`：参数、SRAM、阵列和 Accumulator 顶层连接；
- `FSA-main/src/main/scala/fsa/Configs.scala`：预置尺寸与默认 SRAM 容量公式；
- `FSA-main/src/main/scala/fsa/ExecutionPlan.scala`：五类逐周期微程序；
- `FSA-main/src/main/scala/fsa/ControlGen.scala`：parallel/up/down 控制波生成；
- `FSA-main/src/main/scala/fsa/MatrixEngineController.scala`：双 FSM、双 timer 与冲突检查；
- `FSA-main/src/main/scala/fsa/sa/PE.scala`、`CMP.scala`、`SystolicArray.scala`：阵列数据通路；
- `FSA-main/src/main/scala/fsa/InputDelayer.scala`：输入和输出阶梯延迟；
- `FSA-main/src/main/scala/fsa/Accumulator.scala`：L/O 在线更新和最终归一化；
- `FSA-main/src/main/scala/fsa/BankedSRAM.scala`：full/narrow 访问、bank/sub-bank；
- `FSA-main/python/main.py`、`python/fsa/kernel.py`：tile 映射、双缓冲和指令序列。

### 参考代码

- `参考代码/gemm_hls/kernel/Top.cpp`：dataflow 顶层和 PE 链；
- `参考代码/gemm_hls/kernel/Compute.cpp`：A 双缓冲、C 驻留和展开计算；
- `参考代码/gemm_hls/kernel/Memory.cpp`：宽度转换、A 转置和 B 重放；
- `参考代码/gemm_hls/include/Config.h.in`：tile、并行度和总线参数；
- `参考代码/hls-fpga-accelerators/common/config.h`：packet 与数据类型配置；
- `参考代码/hls-fpga-accelerators/*/*.cpp`：五类独立 kernel 的 load/compute/store 实现。
