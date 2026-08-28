# 多小型 FSA 与 Split-D 融合：研究定位、文献边界与实验规划

> 文档状态：内部讨论稿  
> 更新日期：2026-08-29  
> 适用范围：`FSA_HLS` 在 NM37 多 SLR FPGA 上的后续架构规划

## 1. 当前结论

我们不能把以下任何单独一点写成“首次提出”：

- 将 FlashAttention 的 Q 按 token 行块切分；
- 将 head dimension `d` 切成多个小块（Split-D）；
- 用 16x16 小型脉动阵列计算 `d=128` 的 Attention；
- 在 FPGA 上复制多个小型脉动阵列；
- 将 Softmax 或完整 FlashAttention 映射到脉动阵列内部。

这些组成部分均已分别出现在公开论文、学位论文或开源实现中。但是，截至 2026-08-29 对公开文献的检索，尚未发现一项工作完整覆盖以下组合：

> 在多 SLR FPGA 上复制多个小型 FSA；每个 FSA 沿 head dimension 分段累加完整的 `QK^T` score；不同 FSA 独立处理不同的 query 行块；并继续在 FSA 数据通路内部完成 online-softmax 与 `PV`，以替代跨 SLR 的 128x128 单体 FSA。

因此，我们现阶段可以保留的是一个**严格限定的组合创新主张**，而不是 Split-D、行切分或小阵列本身的算法创新。

由于文献检索无法证明“绝对不存在”，论文中应使用“据我们所知”或“在我们检索到的公开工作中尚未发现”等有限表述，不能直接写“从未有人做过”。

## 2. 拟研究的具体架构

以单头 Attention 为例：

```text
Q, K, V: [128 tokens, 128 dimensions]
物理 FSA: 16x16
FSA 数量: 8
```

建议的空间分工是：

- 8 个 FSA 分别拥有互不重叠的 16 行 Q；
- 每个 FSA 负责输出 O 中对应的 16 行；
- 每个 FSA 都需要遍历全部 128 行 K/V；
- `d=128` 被分成 8 个 `d_tile=16` 的分块；
- 不同 query 行之间相互独立，因此 8 个 FSA 之间不需要做 Softmax 归约；
- K/V 数据需要广播、复制或从不同存储 bank 并行提供给 8 个 FSA。

对于一个 `16x16` 的 score tile，Split-D 必须满足：

\[
S_{ij}=Q_iK_j^T
=\sum_{p=0}^{7}Q_i^{(p)}\left(K_j^{(p)}\right)^T.
\]

硬件执行顺序应是：

```text
score_acc = 0
for d_tile = 0..7:
    score_acc += Q[d_tile] x K[d_tile]^T

# 只有完整 score 得到后才能进入 Softmax
rowmax -> exp -> rowsum -> online-softmax state update

for v_tile = 0..7:
    O[v_tile] += P x V[v_tile]
```

关键约束是：**前 7 个 D 分块只能累加 score，不能提前进行 rowmax、exp 或 rowsum。** Softmax 的输入是完整点积，而不是任意一个 D 分块的局部点积。

## 3. 与已有工作的关系

### 3.1 FlashAttention 与 FlashAttention-2

FlashAttention 已经将 Q 按 query 行块切分，将 K/V 按序列方向切分，并为每个 query 行维护 online-softmax 的最大值、指数和及输出累加值。因此，“对 Q 的行进行切分”属于 FlashAttention 的标准计算结构，不是新的数学分解。

FlashAttention 和 FlashAttention-2 通常让一个 Q/K/V tile 保留完整 head dimension。我们的区别不是增加 query 行切分，而是让物理 FSA 的高度不再等于 `d`，需要在进入 Softmax 前跨多个 D 分块保存并累加 score。

### 3.2 S2-Attention 与 FFPA：Split-D 已有先例

S2-Attention 在 GPU Triton kernel 中明确给出了 D-Split：把 `d=128` 切成两段，分别执行局部 `QK^T`，将部分 score 相加后再进行 Softmax；`PV` 的输出维度也被分段处理。FFPA 的开源 GPU 实现进一步使用更细粒度的 Split-D，以固定 SRAM 占用并支持更大的 head dimension。

所以 Split-D 不能作为我们的独立算法贡献。我们的研究空间是：如何把这一分解嵌入 FSA 的状态机、Accumulator、片上存储和流水调度，并在 FPGA 上得到可验证的物理收益。

### 3.3 16x16 FPGA Flash-Attention 论文：最接近的直接先例

Li 等人在 2025 年 ASICON 论文中已经在 Xilinx Virtex-7 FPGA 上实现了 16x16 脉动阵列，并设置 Q/K/V 的列维度为 128。论文明确描述了两类计算：

- `16x128(Q) x 128x16(K^T)`：分块结果需要在阵列输出端累加；
- `16x16(P) x 16x128(V)`：输出的不同 16 维分块相互独立。

这实际上已经覆盖了“16x16 阵列处理 `d=128`”和“沿 D 维累加 score”。其 Softmax 和系数更新由阵列外的非线性计算单元完成，因而没有覆盖我们设想的 FSA 内部融合。

这篇论文应作为最主要的直接基线。我们的贡献不能再表述为“小阵列支持大 head dimension”，而应表述为“小型 FSA 如何在保留阵列内 online-softmax 的前提下支持 D 分块”。

### 3.4 SystolicAttention/FSA：阵列内融合已有先例

SystolicAttention 提出的 FSA 可以在单个增强脉动阵列中执行 `QK^T`、rowmax、指数、rowsum、online-softmax 更新和 `PV`，不再依赖外部向量单元。其 kernel 同样将 Q 按行块、K/V 按序列块处理。

但是，其性能评估使用单个 128x128 FSA、`d=128`，没有研究：

- 16x16 FSA 上的 D 分块 score 累加；
- 多个小 FSA 对 query 行块的空间并行；
- 多 SLR FPGA 上的布局、布线和 K/V 供数问题。

论文还指出其 128x128 FSA 无法放入作者可用的最大 FPGA，因此采用 ASIC 综合和 RTL 仿真完成主要评估。这正是我们面向 NM37 研究小 FSA 可扩展实现的工程动机之一。

### 3.5 多小阵列和 FPGA 多核 Attention 也已有先例

Zhao 的 2024 年 UCLA 博士论文在 FPGA 上研究了可重配置脉动阵列：大阵列可以拆成多个独立的小阵列，并让 Attention 的两个矩阵乘阶段占用不同阵列区域并行执行。其多核 FPGA 系统还只在核间传递 Softmax/归一化所需的部分最大值与部分和。

但该工作使用独立的非线性向量模块执行 Softmax，没有把 Softmax 融入 FSA 的 PE 数据通路。因此，“多个小阵列”本身不新，可能的新内容仍然是多小型 **FSA**、Split-D 状态保持和 SLR 感知映射的组合。

### 3.6 StreamAttention、BLADE 和 H-FA

近期的 StreamAttention 也把 online attention 递推和指数近似映射到同一个脉动阵列中；BLADE 与 H-FA 则分别研究 fused attention kernel、Softmax 性质和专用硬件数据通路。这些工作进一步说明，“融合 Attention/Softmax”作为宽泛主张已经非常拥挤。

我们必须把贡献落到 FPGA 上可复现的架构差异和物理结果，而不能只依赖概念上的模块融合。

## 4. 文献覆盖矩阵

| 工作 | Q 行分块 | Split-D | 小阵列处理 `d=128` | 多小阵列/多核 | Softmax 在阵列内 | FPGA 实测 |
|---|---:|---:|---:|---:|---:|---:|
| FlashAttention / FA2 | 是 | 通常否 | 不适用 | GPU 并行 | 单 kernel 融合，非 FSA | 否 |
| S2-Attention | 是 | 是 | GPU kernel | GPU thread blocks | 非 FSA | 否 |
| FFPA | 是 | 是 | GPU MMA tile | GPU 并行 | 非 FSA | 否 |
| 16x16 FPGA FA | 是 | 是 | 是 | 主要为单阵列 | 否，外置非线性单元 | 是 |
| SystolicAttention/FSA | 是 | 未研究小阵列 D-split | 使用 128x128 | 单 FSA | 是 | 128x128 未上 FPGA |
| Zhao FPGA RSA/MCore-OPU | 是 | 通用矩阵分块 | 可配置 | 是 | 否，外置向量模块 | 是 |
| StreamAttention | 是 | 未强调该问题 | 未形成多小 FPGA 阵列方案 | 单阵列 | 是 | 否 |
| 我们拟研究的方案 | 是 | 是 | 是 | 8 个小 FSA | 是 | 目标是在 NM37 实测 |

## 5. 建议采用的论文主张

### 5.1 不应采用的表述

以下表述过强或已经被先例覆盖：

```text
We are the first to split the head dimension for FlashAttention.
We are the first to split query rows in FlashAttention.
We are the first to use a 16x16 systolic array for d=128 attention.
We are the first to fuse softmax with a systolic array.
```

### 5.2 当前可用的中文表述

> 本研究提出一种面向多 SLR FPGA 的可扩展多 FSA 架构。该架构通过 D-tiled score accumulation 将 Attention 的 head dimension 与物理 FSA 规模解耦，将互不依赖的 query 行块映射到多个小型 FSA，并在每个 FSA 内部继续完成 online-softmax 与 value accumulation，从而避免单体大 FSA 的跨 SLR 长连线和外置 Softmax 数据往返。

### 5.3 谨慎的英文表述

> We propose an SLR-scalable multi-FSA architecture for exact FlashAttention on FPGA. The architecture decouples the attention head dimension from the physical FSA size through D-tiled score accumulation, distributes independent query-row tiles across replicated small FSA engines, and retains online softmax and value accumulation within the FSA datapath.

只有在进一步完成系统检索和实验后，才考虑加入：

> To the best of our knowledge, this is the first FPGA design to combine D-tiled score accumulation with in-array online softmax on replicated small FSA engines.

“first”必须同时受以下条件限定：

- FPGA；
- exact dense FlashAttention；
- replicated small FSA；
- D-tiled score accumulation；
- in-array online-softmax；
- 最好再限定 multi-SLR implementation。

## 6. 中心科学问题与假设

### 6.1 中心问题

> 相比跨 SLR 的单体大 FSA，D-tiled 多小 FSA 能否在保持精确 FlashAttention 和阵列内 Softmax 的同时，获得更好的可布线性、工作频率和性能/资源比？

### 6.2 待验证假设

1. 16x16 FSA 的局部连接更容易完成布局布线，并可能获得高于 128x128 FSA 的实际频率。
2. 8 个 FSA 独立处理 8 个 query 行块，可以恢复 128 行的空间并行度。
3. Split-D 增加的 score 状态保存、阶段控制和重复加载开销，小于小阵列带来的布线和资源收益。
4. 阵列内 Softmax 相比“16x16 普通 SA + 外置 Softmax”能减少中间数据往返，并提高端到端利用率。
5. K/V 广播或复制不会成为新的决定性带宽瓶颈。

以上均为假设，目前不能写成实验结论。

## 7. 当前工程与目标方案的差距

当前 `FSA_HLS` 的接口和配置仍将：

```text
SA_ROWS = head dimension
SA_COLS = 每次处理的 query/key token 数
```

当前默认综合配置为 `SA_ROWS=4, SA_COLS=4`；已有文档提出的矩形方案使用 `SA_ROWS=128, SA_COLS=4`。这仍然要求物理数据通路高度覆盖完整 head dimension，并不属于本文设想的 Split-D 小 FSA。

要支持 16x16 FSA 处理 `d=128`，至少需要修改：

1. 配置语义：把物理阵列规模与逻辑 head dimension 解耦；
2. DMA：增加 D 分块地址生成和 K/V 广播策略；
3. score Accumulator：保存 `16x16` score tile 的跨 D 分块部分和；
4. ExecutionPlan：增加 `ACCUMULATE_D`、`FINAL_D_TILE` 等阶段；
5. Softmax 启动条件：仅在完整 score tile 形成后启动；
6. `PV` 调度：按 V 的列维度生成 8 个输出 tile；
7. 多 FSA 顶层：实现 query 行分配、K/V 供数、输出地址分区和完成同步；
8. 板级计数器：测量 `ap_start` 接受到 `ap_done` 的真实周期数。

## 8. 必须完成的实验

### 8.1 必做对比

| 编号 | 设计 | 回答的问题 |
|---|---|---|
| A | 128x128 单 FSA | 单体大阵列的资源、拥塞、SLR crossing 和 Fmax 到底如何？ |
| B | 单个 16x16 FSA + Split-D | D 分块能否保持正确性，控制和存储开销是多少？ |
| C | 8 个 16x16 FSA + Split-D | 多阵列能否恢复吞吐，K/V 供数是否成为瓶颈？ |
| D | 16x16 普通 SA + 外置 Softmax | FSA 内融合相对最接近先例到底贡献了多少？ |

四种设计必须使用相同的：

- FPGA 器件和 Vivado/Vitis 版本；
- 数值格式；
- 时钟约束；
- Q/K/V 输入规模与数据分布；
- HBM/BRAM 接口假设；
- 正确性阈值和周期统计边界。

### 8.2 必测指标

- C 仿真与独立 golden model 的最大误差、平均误差；
- C/RTL 协同仿真周期数；
- Vivado implementation 后的 Fmax 和 WNS；
- LUT、FF、DSP、BRAM、URAM；
- SLR 分布和跨 SLR net 数量；
- `ap_start` 接受到 `ap_done` 的板上周期数；
- 单次延迟、持续吞吐和有效 PE 利用率；
- K/V 实际带宽和各 FSA 的 stall 周期；
- 条件允许时测量板上功耗和能效。

### 8.3 关键消融

至少需要以下消融，才能证明结果来自提出的方法，而不是资源数量不同：

1. 相同 16x16 阵列数量下，FSA 内 Softmax 与外置 Softmax 对比；
2. 1、2、4、8 个小 FSA 的扩展曲线；
3. 不同 D tile 数量或物理阵列高度的对比；
4. K/V 单播、广播和多 bank 复制策略对比；
5. 只看理想计算周期与加入 DMA/存储后的端到端周期对比。

## 9. 当前最主要的审稿风险

### 风险 1：被认为只是组合已有技术

Split-D、16x16 FPGA 阵列和 FSA 内 Softmax 都已有先例。若没有新的状态保持机制、调度方法或显著的 post-route 收益，审稿人可能认为方案只是机械组合。

**应对：**明确给出 FSA 为支持 D 分块所需的新状态机、Accumulator 数据通路和调度不变量，并以设计 D 的外置 Softmax 基线做消融。

### 风险 2：理论吞吐估计忽略系统开销

“一个 16x16 阵列执行 8 个 D 分块约等于 128 个基本时间单位，8 个阵列并行后仍约为 128 个单位”只能表示理想 MAC 工作量。它没有包含 K token tile 循环、FSA 的 rowmax/exp/rowsum、权重装载、流水排空、DMA 和存储冲突。

**应对：**建立逐阶段周期模型，并用 RTL/板级计数器验证。论文中不得在测量前把 `128t` 写成最终延迟。

### 风险 3：K/V 广播取代跨 SLR 阵列成为新瓶颈

8 个 FSA 处理不同 Q 行但需要相同的全部 K/V。若每个 FSA 独立读取 K/V，存储带宽可能增长接近 8 倍；若广播，则可能重新产生跨 SLR 高扇出长连线。

**应对：**将 FSA 按 SLR 分组，为每组配置本地 K/V buffer，并比较分层广播、HBM bank 分片和本地复制策略。

## 10. 决策分支

### 正向结果

若 8 个 16x16 FSA 在 NM37 上获得更高 Fmax、较少跨 SLR 拥塞，并在相近资源下达到接近或超过单体 FSA 的端到端吞吐，则中心主张成立：小 FSA 的 D-tiled 架构为多 SLR FPGA 提供了更优的实现尺度。

### 混合结果

若可布线性和 Fmax 明显改善，但 K/V 带宽导致吞吐没有线性扩展，应把贡献收窄为“可实现性和频率优化”，并把存储系统作为主要限制，而不能声称获得近似 8 倍并行收益。

### 负向结果

若多小 FSA 的控制、存储和广播开销抵消了阵列缩小的收益，则应放弃“多阵列优于大阵列”的中心主张。仍可保留单个小 FSA 支持可变 head dimension 的工程结果，但其论文创新性会显著降低。

## 11. 最小充分论文故事

在停止继续扩展研究范围前，至少需要形成以下证据链：

```text
128x128 FSA 在多 SLR FPGA 上存在资源/布线/频率问题
    -> 16x16 FSA 通过 Split-D 正确计算 d=128 exact attention
    -> 新调度让 Softmax 仍保留在 FSA 内部
    -> 多个 FSA 对 query 行块并行且不需要跨阵列数值归约
    -> 分层 K/V 供数控制了广播和带宽开销
    -> post-route 与 NM37 板测证明性能/资源或性能/功耗收益
```

如果缺少最后的 post-route 和板测结果，当前工作只能证明功能可行，不能证明解决了最初的跨 SLR 问题。

## 12. 参考文献与资料

1. Dao, T. et al. *FlashAttention: Fast and Memory-Efficient Exact Attention with IO-Awareness*. NeurIPS 2022. <https://arxiv.org/abs/2205.14135>
2. Dao, T. *FlashAttention-2: Faster Attention with Better Parallelism and Work Partitioning*. ICLR 2024. <https://openreview.net/forum?id=mZn2Xyh9Ec>
3. Lin, X. et al. *S2-Attention: Hardware-Aware Context Sharding Among Attention Heads*. TMLR 2025. <https://arxiv.org/abs/2407.17678>
4. Li, Z. et al. *A 16x16 High-Utilization Systolic Array Hardware Accelerator for Long-Sequence Flash-Attention Computation in Transformer*. ASICON 2025. DOI: `10.1109/ASICON66040.2025.11326298`.
5. Lin, J. et al. *SystolicAttention: Fusing FlashAttention within a Single Systolic Array*. 2025. <https://arxiv.org/abs/2507.11331>
6. Zhao, T. *Acceleration of Deep Learning Algorithms with Transformers*. UCLA PhD dissertation, 2024. <https://escholarship.org/uc/item/3419t2z6>
7. Forland, O. and Kung, H. T. *StreamAttention: Energy-Efficient and High-Utilization Attention on Systolic Hardware*. 2026 workshop/preprint. <https://openreview.net/forum?id=SRQuPOMzkX>
8. Lin, Y. et al. *BLADE: Energy-efficient attention accelerator with fused kernel and bit-level redundancy elimination*. Electronics Letters, 2025. DOI: `10.1049/ell2.70137`.
9. Alexandridis, K. et al. *H-FA: A Hybrid Floating-Point and Logarithmic Approach to Hardware Accelerated FlashAttention*. 2025. <https://arxiv.org/abs/2511.00295>
10. FFPA open-source implementation. <https://github.com/xlite-dev/ffpa-attn>

## 13. 一句话总结

> 我们可能的创新不在于“把矩阵切小”，而在于证明一种面向多 SLR FPGA 的多小型 FSA 架构，可以跨 D 分块保存完整 score、在阵列内部继续执行 online-softmax，并以可复现的板级结果优于单体大 FSA 和“普通小 SA + 外置 Softmax”方案。
