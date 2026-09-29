# FlashAttention 类加速器：计算阵列的空间分解调研

> 目的：为 FSA-HLS 的「单个 D×D PE 阵列、D=16 目标、head dim 16/128、Vitis HLS 2024.2 @ xcvu37p、100 MHz（7.300 ns 估算）」给出可执行的架构建议。
>
> 本文档区分三类内容：
> - **[事实]** = 已引用的公开文献/官方文档中可查证的陈述，带 markdown 链接。
> - **[推断]** = 我基于这些事实对本项目做的推理，未经文献直接验证。
> - **[未验证]** = 我无法用可靠来源证实的内容，明确标出，不做猜测。

---

## 0. 先给出结论摘要

**[推断]** 对「必须只有一个物理 PE 阵列」这一硬约束，文献里最接近的先例是 SALO（DAC'22）：它的 PE 阵列**内部**依次跑完 QK、exp、行求和、归一化、PV 五个阶段，每个 PE 只有一个 MAC 单元和一个累加寄存器，阶段之间靠 PE 间数据流切换，而不是复制阵列。这与本项目「一个阵列复用做 QK / softmax / row-sum / PV」的意图完全一致。

**[推断]** 但 SALO 用的是 32×32 方阵，而方阵恰恰是导致「行归约」和「softmax 需要的行方向依赖」难以流水的主要原因之一：SWAT 论文明确指出 SALO 的方阵结构强制输入/中间矩阵**方形成块**，而这对**行方向 softmax 是次优的**，必须在加速器之外补做额外运算。对本项目，D=16 时方阵 = 256 PE，PE 内部若要自带归约树，控制流就复杂到 HLS 无法流水。

**[推断]** 因此建议把阵列从 **D×D 方阵** 改为 **D×P 矩形**（P = 沿 head 维度铺开的并行度），并把**归约从 PE 内部挪到每行的尾端小归约树**。这样每个 PE 退化成「一个 fp16 操作数寄存器 + 一个 fp32 累加器 + 一个纯 MAC 内循环」，控制流简单，才可能让 HLS 流水起来；D=16、P=16 时 256 个 PE 刚好对应 head dim 16 的完整点积宽度，**一个时钟周期出一列 QK 结果**。

---

## 1. 已发表的 attention 加速器如何做空间分解

### 1.1 沿 key 维度切分（split-K / split-KV / flash-decoding）

**[事实]** FlashAttention-2 的 forward 并行维度是 `grid(num_m_block, b, h)`，即沿 **Q 的行块**并行；论文与代码分析明确指出：**每个 Q 块的 online softmax 状态 `(m, l, O)` 相互独立，所以可以沿 Q 并行；而沿 KV 并行会让多个 block 写同一个 Q 块的 O，需要跨 block 的 reduce**，这正是 FA1 的 split-K 思路，较慢，因此 FA2 把 warp 间划分从 split-K 改成 split-Q。
来源：[llm-infra-atlas 对 FA2 work partitioning 的逐段代码对照](https://raw.githubusercontent.com/llm-infra-atlas/llm-infra-atlas.github.io/52e065bbaecfc42256a25b97713adc8fb1c9a200/docs/attention/fa/02_fa2_parallelism.md)（**工程性技术文档，非同行评审**）；原始论文 [FlashAttention-2, arXiv:2307.08691](https://arxiv.org/abs/2307.08691)、[FlashAttention, arXiv:2205.14135](https://arxiv.org/abs/2205.14135)。

**[事实]** 同一来源说明：当 `seqlen_q = 1`（decode）时，沿 Q 并行退化成 `b·h` 个 block，并行度不足，此时才走 **split-KV** 路径：把 KV 维切 `num_splits` 段并行，每段产生 partial `(O, LSE)`，再用独立的 combine kernel 归并；**「沿 KV 并行需要额外 combine」是它的代价，只在并行度不够时才值得用**。对应代码 `grid(num_m_block, num_splits, b*h)` 与 `flash_fwd_splitkv_combine_kernel`。
来源：同上 [llm-infra-atlas FA2 文档](https://raw.githubusercontent.com/llm-infra-atlas/llm-infra-atlas.github.io/52e065bbaecfc42256a25b97713adc8fb1c9a200/docs/attention/fa/02_fa2_parallelism.md)。

**[事实]** FlashAttention 本身的贡献是 **IO-aware tiling**：通过分块让 `S = QK^T` 这个 `N×N` 矩阵**不进 HBM**，在片上 SRAM 里完成，并给出 IO 复杂度分析，证明在一段 SRAM 容量范围内是最优的。
来源：[FlashAttention, arXiv:2205.14135](https://arxiv.org/abs/2205.14135)。

**[推断]** 对本项目：split-K / flash-decoding 要解决的问题是「**并行度不足**」（GPU 上 SM 填不满）。单个 FPGA 上只有一个物理阵列，这个动机**不成立**。而且 split-KV 的代价（partial `(m,l,O)` + combine + 浮点累加顺序不确定）会直接落到本项目的 row-statistics 通路上。**因此 split-K 不是本项目的首要分解维度。**

**[事实]** 但 split-K 的**数学**是有先例且被形式化的：SALO 的 "window splitting" 把同一 query 的窗口切成 `T1, T2`，各自得到 `W1 = Σ_{j∈T1} exp(S_ij)`、`W2`，再用重归一化变换合并：
`output_i = W1/(W1+W2)·output_i^1 + W2/(W1+W2)·output_i^2`。
来源：[SALO, DAC'22, arXiv:2206.14550](https://ar5iv.labs.arxiv.org/html/2206.14550)。

### 1.2 head 维度：分到周期上 vs 分到空间上

**[事实]** A3（HPCA'20）的 base design 把 attention 拆成三个模块流水：dot-product module → exponent computation module → output computation module，**每个 key 行 `i` 内部对 head 维度 `j = 0..d` 做 `parallel for`**，即一个 dot product 在空间上跨 `d` 展开；同时它把 max 的更新和 `exp` 也放进流水线（先算 `dot_product[i]`，更新 `max`，再做 `dot_product[i] -= max`、`score[i] = exp(...)`、`expsum += score[i]`）。
来源：[A3, arXiv:2002.10941](https://arxiv.org/html/2002.10941v1)。

**[事实]** SALO（DAC'22）的 PE **只有一个定点 MAC 和一个累加寄存器 `Reg_acc`**，QK 阶段是典型 output-stationary 脉动：`PE_{i,j}` 每周期收一个 `q_i` 元素和一个 `k_j` 元素，相乘累加进 `Reg_acc`，同时把操作数传给邻居，**一直累到该 `S_ij` 算完**。也就是说 head 维度是**在周期上串行流过** PE 的，空间上只有 `32×32` 个 (query, key) 位置。
来源：[SALO, DAC'22, arXiv:2206.14550](https://ar5iv.labs.arxiv.org/html/2206.14550)。

**[推断]** 这是两种不同的空间/时间分配：
- **A3 式**：一个 (q,k) 对的 `d` 维点积在空间展开（`d` 个 MAC 并行），周期上只累 1 次 → 延迟低、DSP 消耗 = `d` × 同时在算的 (q,k) 对数。
- **SALO 式**：一个 (q,k) 对只占 1 个 PE，`d` 维在周期上串行累加 → DSP 少、需要的周期数 = `d`。

对本项目：head dim 16 时二者代价接近（16 次串行累加 vs 16 个 DSP）；**head dim 128 时差别巨大**——若完全照 SALO 式，单个 QK 结果要 128 个周期，而行/列并行度大幅减少。**[推断] 折中是必要的：沿 head 维只展开 `P < HD` 份（例如 P=16），剩下的 HD/P 份在周期上串行累加到同一个 fp32 累加器。**

### 1.3 tiling：query 行 vs key 行

**[事实]** SWAT（DAC'24）明确采用 **row-wise（row-major）dataflow**，理由是：window attention 下相邻 query 共享绝大多数被 attend 的 K/V 行（`w−1` 个复用），row-major 能最大化复用并最小化中间矩阵 `S`/`S'` 的存储；它同时用 **kernel fusion**——把 softmax 分母推迟到 PV 之后（`Z = (1/Σexp)·(Σ exp(S)·V)`），从而让三个阶段合成一个 row-wise kernel。
来源：[SWAT, DAC'24, arXiv:2405.17025](https://ar5iv.labs.arxiv.org/html/2405.17025)。

**[事实]** SWAT 明确批评方阵结构：「SALO … utilizes structured sparsity in window attention with a **2D square systolic array**. However … the **systolic array's square structure requires square tiling of the input and the intermediate matrices. This tiling is suboptimal for row-wise SoftMax operations, necessitating supplementary computations outside the accelerator's capabilities.**」
来源：[SWAT, DAC'24, arXiv:2405.17025](https://ar5iv.labs.arxiv.org/html/2405.17025)。

**[事实]** FlashAttention/FA2 的 tiling 是两维的：`Br`（query 行块）× `Bc`（key 行块），序列维 `N` 被切成 `N/Br` 个 Q 块并行、每块内部串行扫 `N/Bc` 个 KV 块；causal 下还做块裁剪 + 把循环拆成「需要逐元素 mask 的对角线区」和「完全在下三角、零分支的主体区」两段，以把 element-wise 开销压到最低。
来源：[llm-infra-atlas FA2 文档](https://raw.githubusercontent.com/llm-infra-atlas/llm-infra-atlas.github.io/52e065bbaecfc42256a25b97713adc8fb1c9a200/docs/attention/fa/02_fa2_parallelism.md)（工程文档）。

**[事实]** FlashAttention-2 还指出一个对本项目极重要的约束：A100 上 FP16 matmul（Tensor Core）312 TFLOPS，而非 matmul 的 element-wise（CUDA Core/SFU，含 `exp`、`rowmax`、`α` 修正）只有约 19 TFLOPS，**差约 16 倍**；因此 FA2 的核心优化之一是「循环内尽量少做非 matmul 运算」，把逐块 `1/l` 归一化**推迟到循环结束只做一次**，循环内只保留 `α = exp2((m_prev − m_cur)·scale)` 的修正。
来源：同上 [llm-infra-atlas FA2 文档](https://raw.githubusercontent.com/llm-infra-atlas/llm-infra-atlas.github.io/52e065bbaecfc42256a25b97713adc8fb1c9a200/docs/attention/fa/02_fa2_parallelism.md)（注：其引用的 FA2 源码锚点为 `csrc/flash_attn/src/softmax.h#L136-L162`，`Is_first` 特化跳过首次 α 修正）。

### 1.4 QK 阵列与 PV 阵列分离 vs 合并

**[事实]** 存在两种被明确记录的做法：

**(A) 合并到同一阵列 / 不复制**：SALO 的原则就是「**minimize the cost of data transmission, our design enables the whole computation of attention to be finished within PEs: the matrix multiplication of Q and K, softmax and the second matrix multiplication of S' and V**」，5 个阶段在同一 PE 阵列内切换数据流完成（stage 1 QK output-stationary、stage 2 分段线性 exp、stage 3 行方向水平累加得分母、stage 4 乘逆归一化、stage 5 PV weight-stationary）。
来源：[SALO, DAC'22, arXiv:2206.14550](https://ar5iv.labs.arxiv.org/html/2206.14550)。

**(B) 按算子/层专门化（spatial architecture）**：把不同算子给不同 PE，用 FIFO/多缓冲直连，避免中间结果写回 off-chip。Cornell 的 TRETS'24 分析文章把 FPGA LLM 加速器分成 temporal（overlay，复用 PE，中间结果必须写回内存，延迟/能耗高）与 spatial（每个算子/层专用 PE + streaming buffer 直连）两大类。
来源：[Understanding the Potential of FPGA-Based Spatial Acceleration for LLM Inference, ACM TRETS'24, arXiv:2312.15159](https://ar5iv.labs.arxiv.org/html/2312.15159v2)。

**(C) 矩阵引擎 + 独立的特殊功能单元（SFU）**：FlightLLM 用统一的 MPE（Matrix Processing Engine）做 GEMM/SpMM/GEMV/SpMV/SDDMM，另设 SFU 专门处理 softmax、LayerNorm 等 MISC；软硬件并行时 SFU 与 MPE 的计算可以**融合**——softmax/LayerNorm 是 "two-phase operation"（先归约出参数再逐元素），SFU 会**读两遍向量**（一遍算参数，一遍算输出）。
来源：[FlightLLM, FPGA'24, arXiv:2401.03868](https://ar5iv.labs.arxiv.org/html/2401.03868v2)。

**(D) 统一 PE 但要模块化**：ADAPTOR（TRETS 投稿）主张「**the data access and computation patterns differ across various blocks within the transformer, which also prevents acceleration. Therefore, assigning a dedicated hardware module to each block allows easier design and optimization**」，为 MHA 和 FFN 各设一套 buffer 与 tiling（`TS_MHA` / `TS_FFN`），并用运行时自适应参数在同一 bitstream 下切换。
来源：[ADAPTOR, arXiv:2411.18148](https://ar5iv.labs.arxiv.org/html/2411.18148)。

**[推断]** 对本项目「no per-stage copies」的硬约束，(A) 是唯一合法的路线，且 SALO 已证明可行。但要注意 SALO 之所以能在同一阵列里切换，是因为它**每个 PE 只保留一个累加寄存器**、阶段间的数据搬移靠 PE 间的水平/对角脉动连线完成——代价是阶段 3、5 引入了「沿 PE 行方向串行扫描」的时间开销（见 §2.4）。

### 1.5 其它相关先例（ASIC，非 FPGA）

**[事实]** SpAtten（HPCA'21）是算法-架构协同设计：cascade token/head pruning + progressive quantization，硬件上配高并行 top-k 引擎、专用存储层次和**全流水数据通路**；输入是 Q/K/V 各自分成多头。
来源：[SpAtten, arXiv:2012.09852](https://ar5iv.labs.arxiv.org/html/2012.09852)。

**[事实]** SpAtten 还给出一个与本项目相关的量化观察：**attention 在 generation 阶段算术强度只有 ~0.5 ops/Byte，是 memory-bounded**；而在 BERT（summarization）阶段是 compute-bounded；GPT-2 生成 32 token 时 attention 占端到端延迟 97%。
来源：[SpAtten, arXiv:2012.09852](https://ar5iv.labs.arxiv.org/html/2012.09852)。

**[事实]** A3 的前提是：attention 本质是 content-based search，softmax 之后大量 score 接近 0，因此可以不穷举；它用「预处理 key 矩阵得到候选行 → 只对候选算点积」来省掉无效计算。
来源：[A3, arXiv:2002.10941](https://arxiv.org/html/2002.10941v1)。

---

## 2. 各分解方式的资源/吞吐/softmax 代价，以及单 FPGA（无 HBM）可行性

### 2.1 方阵 N×N 的定义与官方参考尺寸

**[事实]** Vitis BLAS 库 L2 GEMM 的脉动阵列尺寸由模板参数决定，且**「size is set according the external memory datawidth. For single-precision floating point GEMM and 512-bit DDR interface, the systolic array size is 16 x 16」**；矩阵被切成「size should be multiple of the size of systolic array」的块。
来源：[Vitis BLAS Library L2 GEMM Kernel 官方文档](https://xilinx.github.io/Vitis_Libraries/blas/2022.1/user_guide/L2/L2_gemm_content.html)。

**[推断]** 即：**阵列规模是跟着内存位宽定的**（512-bit / 32-bit fp32 = 16 个元素/周期 → 16×16）。对本项目：VU37P 的 DMA 位宽（AXI 位宽）决定了一个周期能灌多少个 fp16，这应当是选 `D` 与 `P` 的**硬约束来源**，而不是先定 D=16 再想办法喂饱它。**[未验证]** 我无法从公开来源确认该 kernel 在具体器件上的 DSP 占用数字。

### 2.2 已报道的阵列形状与规模（具体数字）

| 设计 | 平台 | 阵列/并行结构 | 每 PE 内容 | 来源 |
|---|---|---|---|---|
| SALO (DAC'22) | ASIC 45nm（Chisel→Verilog, Synopsys DC） | PE 阵列 **32×32** + 1 个 global PE row + 1 个 global PE column | 1 个定点 MAC + 1 个累加寄存器 `Reg_acc` + 2 个 LUT（分段线性 exp 的斜率/截距） | [SALO](https://ar5iv.labs.arxiv.org/html/2206.14550) |
| Vitis BLAS L2 GEMM | AMD/Xilinx | **16×16** 脉动阵列（fp32 + 512-bit DDR） | 官方未在该页给出 | [Vitis BLAS 文档](https://xilinx.github.io/Vitis_Libraries/blas/2022.1/user_guide/L2/L2_gemm_content.html) |
| NPE (FPGA'21) | Xilinx Zynq Z-7100 @200 MHz | **128 PE，每 PE 16 个 MAC**（共 2048 个乘法器），映射到 DSP | 每 PE 做 inner product + adder tree + accumulate | 综述转述，见 [A Survey on Hardware Accelerators for LLMs, arXiv:2401.09890](https://ar5iv.labs.arxiv.org/html/2401.09890) |
| FlightLLM (FPGA'24) | Alveo U280 / Versal VHK158 | 多 core，每 core 一个 MPE；MPE = 多 MPU，MPU = 多 VPU，VPU 做向量点积；DSP 用量 `DSP = (p_M·p_K·p_N·MPU)·MPE` | VPU 基于可配置稀疏 DSP 链，每个 DG 两个 DSP48 | [FlightLLM](https://ar5iv.labs.arxiv.org/html/2401.03868v2) |
| SWAT (DAC'24) | FPGA（HLS） | row-wise dataflow + kernel fusion + input-stationary；**论文明确不用方阵**，理由是方阵强制方形成块、对行 softmax 次优 | — | [SWAT](https://ar5iv.labs.arxiv.org/html/2405.17025) |
| ADAPTOR | Alveo U55C / VC707 / ZCU102 | 每功能模块一套 tiling 参数（`TS_MHA`、`TS_FFN`），HLS 2022.2.1 | DSP48 + BRAM/LUTRAM/PE 内寄存器 | [ADAPTOR](https://ar5iv.labs.arxiv.org/html/2411.18148) |
| A3 (HPCA'20) | ASIC 40nm | dot-product / exponent / output 三段流水；`d` 维在段内 **parallel for** 展开 | — | [A3](https://arxiv.org/html/2002.10941v1) |
| SpAtten (HPCA'21) | ASIC 40nm | 全流水数据通路 + 高并行 top-k 引擎 | — | [SpAtten](https://ar5iv.labs.arxiv.org/html/2012.09852) |

**[事实]** ADAPTOR 给出一个可用于估算的器件背景数字：UltraScale+ FPGA 约 **9024 个 DSP**；低端器件（ZCU104）片上内存约 5 MB，高端（Alveo U200）约 35 MB。
来源：[ADAPTOR, arXiv:2411.18148](https://ar5iv.labs.arxiv.org/html/2411.18148)。
（**[推断]** 这与 VU37P 的 DSP48E2 数量同量级；**[未验证]** 我没有找到 VU37P 官方资源表的可直接引用版本，故不写具体数字。）

### 2.3 softmax / row-statistics 的处理方式（各方案对比）

**[事实]** 三条被明确记录的路线：
1. **SALO：全部在 PE 阵列内做**。exp 用分段线性近似（引 Softermax），两个 LUT 存斜率/截距，**用 MAC 单元就能算**；分母 `Σexp(S_ij)` 用 **PE 行内水平脉动累加**得到（每个 PE 从左邻收 partial sum，加上自己的 exp，再传右邻）；右端 PE 出界后算 **倒数** 并广播回整行（**刻意避免除法器**，因为 divider 面积/周期成本大）；每 PE 乘回得到 `S'_ij`。
   来源：[SALO](https://ar5iv.labs.arxiv.org/html/2206.14550)。
2. **FlightLLM：matrix engine 之外的 SFU**。softmax/LayerNorm 归类为 "two-phase operation"（先归约出参数、再逐元素），SFU 读两遍向量；softmax 与 LayerNorm 按 **fp16** 计算（理由：SFU 硬件成本可接受），并用细粒度子向量切分来隐藏 MISC 延迟。
   来源：[FlightLLM](https://ar5iv.labs.arxiv.org/html/2401.03868v2)。
3. **FA2：留在 matmul 同一个循环里，但压到最少**。循环内只保留 `reduce_max / exp2 / reduce_sum` 和一次 α 乘加；逐块 `1/l` 归一化推迟到循环外；`Is_first` 编译期特化跳过首次 α 修正。
   来源：[llm-infra-atlas FA2 文档](https://raw.githubusercontent.com/llm-infra-atlas/llm-infra-atlas.github.io/52e065bbaecfc42256a25b97713adc8fb1c9a200/docs/attention/fa/02_fa2_parallelism.md)。

**[事实]** A3 指出一个重要的语义细节：它的 `Module 1: Dot-Product` 里同时更新 `max`，`Module 2` 才做 `dot_product[i] -= max` 然后 `exp`——即 **max 的归约在流水线里前移**，与点积并行推进。
来源：[A3](https://arxiv.org/html/2002.10941v1)。

### 2.4 各分解方式的代价（综合）

**[推断]** 下表是我把上述事实对本项目做的映射，**不是文献原表**：

| 分解方式 | DSP 需求 | 片上存储 | 吞吐/周期 | 对 softmax/row-stat 的影响 | 单 FPGA（无 HBM）是否可行 |
|---|---|---|---|---|---|
| 方阵 D×D（现状） | D² | 小 | 每周期 ~1 个 (q,k) 部分和 | **行归约在方阵里方向不对**（SWAT 已指出方阵强制方形成块、对行 softmax 次优）；PE 内加归约树 → 控制流复杂、HLS 不流水 | 可行但**不 scale**（D=16 即 256 PE 已失败） |
| 沿 K 切（split-K / split-KV） | 不变 | 需要 partial `(m,l,O)` 的缓冲；若用 HBM/DRAM 反而更差 | 每段各自吞吐 | 必须做 **combine/rescale**，且浮点累加顺序不确定 | **动机不成立**（无 SM 填不满的问题）；且引入 combine 代价 |
| head 维分到空间（d 全展开） | = d × (同时在算的 q,k 对数)，head dim 128 时爆炸 | 低 | 一个点积 1 周期 | 归约树最宽，最坏情况 | head dim 16 可行；**128 不可行** |
| head 维分到周期（d 全串行，SALO 式） | 最小 | 低 | 一个点积要 d 个周期 | 归约简单 | head dim 128 时**延迟不可接受** |
| **head 维折中：D×P 矩形（P<HD，余下串行）** | D×P | D×P 个 fp16 reg + fp32 acc | 一个 (tile 行 × P 个 k) 的部分和/周期 | 行归约移到**行尾小归约树**，PE 内只剩纯 MAC | **推荐**（见 §4） |
| 1-D lane + 归约树 | = 并行 lane 数 | 低 | 每周期一条点积的部分和 | 归约树独立、最易流水 | 可行，PV 需要转置 V |
| QK 阵列与 PV 阵列分离 | 2× 阵列 | 中 | 两段可并行 | row-stat 可跨两阵列共享 | **被约束禁止**（exactly one array） |

**[事实]** 关于「专用模块 vs 统一阵列」的代价，ADAPTOR 与 Cornell TRETS 都指向同一结论：专用化降低设计/优化难度并减少中间结果回写，但代价是 FPGA 面积上要放多份硬件。Cornell 的空间架构分析把 temporal（overlay）的代价描述为「**intermediate results must be written back to memory … cost in terms of both latency and energy consumption that is significantly higher than direct on-chip memory access**」。
来源：[Cornell TRETS'24](https://ar5iv.labs.arxiv.org/html/2312.15159v2)、[ADAPTOR](https://ar5iv.labs.arxiv.org/html/2411.18148)。

---

## 3. 实践中典型的阵列形状及其原因

**[事实] 方阵 N×N 出现的地方**：
- Vitis BLAS L2 GEMM：**16×16**，理由是「size is set according the external memory datawidth」（512-bit fp32）。[来源](https://xilinx.github.io/Vitis_Libraries/blas/2022.1/user_guide/L2/L2_gemm_content.html)
- SALO：**32×32** PE 阵列（ASIC 45nm），行 + 列外挂 global PE row/column 做 global attention。[来源](https://ar5iv.labs.arxiv.org/html/2206.14550)

**[事实] 矩形/非方阵或「lane + 归约」出现的地方**：
- A3：**模块级流水 + 段内 `parallel for`**，没有方阵；dot-product / exponent / output 三个模块串联。[来源](https://arxiv.org/html/2002.10941v1)
- NPE：**128 PE × 每 PE 16 MAC**（2048 乘法器），PE 内做 inner product + adder tree + accumulation。[来源](https://ar5iv.labs.arxiv.org/html/2401.09890)
- FlightLLM：**VPU = 向量点积**（DSP 链），MPU = 多 VPU，MPE = 多 MPU；粒度是「向量」而非「方阵」。[来源](https://ar5iv.labs.arxiv.org/html/2401.03868v2)
- SWAT：显式拒绝方阵，改 row-wise dataflow + kernel fusion + input-stationary（固定长度 FIFO 管住滑动窗口输入）。[来源](https://ar5iv.labs.arxiv.org/html/2405.17025)

**[事实] 为什么方阵对 attention 不友好（原文）**：
> 「The systolic array's square structure requires square tiling of the input and the intermediate matrices. This tiling is suboptimal for row-wise SoftMax operations, necessitating supplementary computations outside the accelerator's capabilities.」
> —— [SWAT, DAC'24](https://ar5iv.labs.arxiv.org/html/2405.17025)

**[事实] 行方向归约 + 行方向并行的理由（原文/转述）**：
> 「softmax 沿「行（query）」方向归约。让每个 warp 拥有完整的行，归约就落在 warp 内部（warp shuffle，便宜）；让 warp 只拥有列的一部分，归约就要跨 warp（SMEM 加 barrier，昂贵）。」
> —— [llm-infra-atlas FA2 文档](https://raw.githubusercontent.com/llm-infra-atlas/llm-infra-atlas.github.io/52e065bbaecfc42256a25b97713adc8fb1c9a200/docs/attention/fa/02_fa2_parallelism.md)（工程文档，非同行评审）

**[推断]** 归纳成一句设计律：**并行的维度应该是「互相独立的行（query）」，归约的维度应该是「同一行内的列（key/head 元素）」**。方阵把这两件事都放在空间上（行和列各占一个物理维度），于是行归约必须在阵列内部跨越物理 PE 边界完成——这正是 SWAT 说的「需要加速器能力之外的补充运算」，也正是本项目 D=16 时控制流复杂、无法流水的结构性来源。

**[未验证]** 我没有找到明文写「方阵导致 HLS 无法流水」的官方文档；「control-flow is too complicated」这一具体错误串我也**没有在 AMD/Xilinx 官方文档中找到可引用出处**（见 §5.2）。§3 的设计律是我自己的归纳。

---

## 4. 针对本项目约束的推荐方案

**约束回顾（用户给定）**：exactly one physical PE array；no per-stage copies；必须满足 7.300 ns HLS 估算；D=16 目标；head dim 16（默认）/128（目标）；一个 kernel 内含 QK、row-max/softmax、row-sum、PV；DMA 搬 Q/K/V/O；xcvu37p @ 100 MHz。

### 4.1 推荐：D×P 矩形阵列（P 沿 head 维铺开），行尾归约树，online-softmax 式行状态

**[推断] 形状**：`D × P`，其中
- `D` = 每批处理的 query 行数（= 现有 tile 的 D，默认 4 → 目标 16）；
- `P` = **沿 head 维展开的并行度**，只要求 `P | HD`，**不要求 `P = D`**。

**head dim 16（默认）**：取 `D=16, P=16` → 256 PE，**一个时钟周期出一个 (q_i, k_j) 的完整点积**（因为 `P = HD`，无需串行累加）。

**head dim 128（目标）**：保持 `P=16` 不变，`HD/P = 8` 次串行累加进同一个 fp32 累加器；或按 §2.1 的官方思路，**让 `P` 由 DMA/AXI 位宽决定**（fp16 时 256-bit 接口 → P=16）。**[推断]** 关键是 `P` 不随 `D` 平方增长：`D=16, P=16` 是 256 PE，而方阵 `D=16` 也是 256 PE——**同样 PE 数，矩阵形状从「行×列都占物理维度」变成「行占物理维度、head 占物理维度」，key 变成流经的串行维度。** 这是把 §3 的设计律落到实处的关键一步。

**[推断] 每个 PE 的内容**（与 SALO 对齐，但操作数是 fp16）：1 个 fp16 操作数寄存器 + 1 个 fp32 累加器 + 1 个纯 MAC。**PE 内不放假归约树**，因此内循环是「读两个 fp16 → 乘 → 加进 fp32 累加器」，无分支、无跨 PE 依赖。

### 4.2 QK 阶段

```
for j in 0..NT-1:              # NT = key tile 数，流式
  for d in 0..HD/P-1:          # 串行累加，head dim 默认时 = 1 次
    PE[i][p].acc += Q[i][d*P+p] * K[j][d*P+p]
```
**[推断]** Q 常驻在 PE 的 fp16 寄存器里（每个 PE 只持自己那份 `Q[i][d*P+p]`，与「每个 PE 持一个 fp16 工作寄存器」的现状一致），K 从 BRAM 流式广播。**行 i 与列 p 都占物理 PE，key j 是时间维度**——这样 QK 完全不需要方阵式的行列脉动。

### 4.3 PV 阶段（复用同一阵列）

**[推断]** PV 的映射建议把阵列旋转 90° 使用：令 `P` 这一维承载 value 的 head 维输出列，`D` 这一维承载 P（softmax 概率）的行：
```
for l in 0..D-1:              # 归约维：概率行 = 时间维度（流经 PE）
  PE[i][p].acc += Prob[l] * V[l][p]
```
即 **PV 的归约维是 key/概率行 `l`，它成为串行流经阵列的维度**；`D` 物理维承载 query 行、`P` 物理维承载 output 的 head 通道。这样：
- 同一个 256 PE 阵列同时服务 QK 和 PV，不复制阵列；
- `Prob`（softmax 输出）需要**逐行广播**到 PE 行，`V` 需要以「`l` 为慢维、`p` 为快维」的转置顺序流出。

**[事实]** 这一「QK 与 PV 用不同数据流、同一批算术单元」的做法在 SALO 里有直接先例：stage 1（QK）是 **output-stationary** 脉动，stage 5（PV）改成 **weight-stationary** 脉动（PE 从左邻收 partial sum，加上 `S'_ij · v_j`，传右邻）。
来源：[SALO](https://ar5iv.labs.arxiv.org/html/2206.14550)。

**[推断] 代价与风险**：V 的转置读取会加剧 BRAM 端口压力，可能需要 `ARRAY_PARTITION` 或让 DMA 按转置布局搬运（浪费一些带宽）。这是本方案最主要的实现风险，见 §5.1。

### 4.4 softmax 与 row-statistics 通路

**[推断]** 按「行状态留在阵列里、归约在行尾」组织，且严格照 FA2 的「循环内只做必要的事」原则：

1. **running max `m_i`（每行 D 个）**：因为每个 query 行 `i` 横跨 `P` 个 PE 且 `i` 只由这一行的 PE 服务，可以让**每行的第 0 个 lane 持有该行的 `m_i`**，或让 `P` 个 lane 各持 local max、每个 key tile 末尾用**行尾归约树**（深度 `⌈log2 P⌉`）合并一次。这比在 PE 内部做归约简单得多。
2. **running sum `l_i`（每行）**：同样归约。注意 FA2 的做法——**`l` 在循环内不做逐块归一化，只在最后除一次**，循环内只维护 `l` 的累加和 `α` 的修正。
3. **α 修正与 PV 累加器重缩放**：每个 key tile 得到新 max 后，`α = exp2((m_old − m_new)·scale)`，**只乘到 PV 的 fp32 累加器上**（并同步修正 `l`），不碰概率本身（FA2 的 `softmax_rescale_o`，`Is_first` 时跳过）。
   **[推断]** 本项目为省寄存器，可以退化为更简单的 **FA2 之前的经典 online-softmax**：`m_i` 更新后把已存的部分 `O_i` 与 `l_i` 都乘 `exp(m_old − m_new)`，再继续。两者数学等价，前者省一次乘法但也需要第二个累加器；[事实] FA2 用模板特化把「第一块」的 α 修正整段省掉。
   来源：[llm-infra-atlas FA2 文档](https://raw.githubusercontent.com/llm-infra-atlas/llm-infra-atlas.github.io/52e065bbaecfc42256a25b97713adc8fb1c9a200/docs/attention/fa/02_fa2_parallelism.md)。
4. **exp 的实现**：用 `exp2` + 预乘 `log2(e)` 与 scale（FA2 的 `scale_apply_exp2`），落到硬件是移位/加法，比通用 `expf` 便宜得多；若 LUT 有余量，也可用 SALO/Softermax 的**分段线性近似 + 两个 LUT**，其好处是**可以用 MAC 单元本身来算 exp**，不额外消耗 DSP 做非线性。
   来源：[SALO](https://ar5iv.labs.arxiv.org/html/2206.14550)（引 Softermax）、[llm-infra-atlas FA2 文档](https://raw.githubusercontent.com/llm-infra-atlas/llm-infra-atlas.github.io/52e065bbaecfc42256a25b97713adc8fb1c9a200/docs/attention/fa/02_fa2_parallelism.md)。
5. **概率 `Prob` 的存放**：在线 softmax 下 `Prob` 必须**先存在片上再喂给 PV**（因为 PV 的归约维就是 `l`，而 `Prob` 是 `l × D` 的块）。建议放 BRAM 并对第 1 维做 `ARRAY_PARTITION`（让 PV 的归约维可整列并行读入），这**不是**「per-stage 阵列副本」，只是数据缓冲，与约束不冲突。
6. **除法**：只在**每个 query 行的最后**做一次 `1/l`（或 `O_i · (1/l_i)`）。SALO 明确用「算倒数再广播」而不是逐元素除法，理由是 divider 面积/周期成本高。来源：[SALO](https://ar5iv.labs.arxiv.org/html/2206.14550)。

### 4.5 为什么这个方案能解决「D=16 无法流水」

**[推断]** D=16 时方阵失败的**结构性**原因是：阵列的两个物理维度分别承载 query 行和 key 列，而行归约/softmax 需要跨 key 列（即跨物理 PE 边界）通信，于是「谁的累加器是完整的 `S_ij`」「谁来算行 max/sum」「max 更新后谁去缩放谁」变成了**数据依赖 + 条件分支交织**的控制流——HLS 会把这些当成不可流水的区域。

改成 `D×P` 后：
- 每个 PE 的 `acc` **语义单一**（就是 `Q[i][·]·K[j][·]` 或 `Σ_l Prob[l]·V[l][p]`），内循环无分支；
- 行归约只在**每个 key tile 的边界**发生一次，位置固定在「行尾小树」，不在 PE 内；
- 软化的条件（`Is_first`、是否更新 max）可以**在 tile 循环外层特化**（照 FA2 的编译期特化思路），而不是放在最内层。

---

## 5. 主要风险与用 Vitis HLS 低成本验证的方法

### 5.1 风险清单（按我判断的严重程度排序）

**[推断] R1 — V 的转置读取会拖垮 PV 的 II 或吃光 BRAM 端口。** PV 需要 `V[l][p]` 以 `l` 为慢维流入，而 V 在 DRAM/BRAM 里通常是行主序 `V[l][:]`。若 BRAM 只有 2 个端口，`D×P` 阵列要同时读 `D` 个不同的 `l` 行就会撞端口，可能需要 `dim=1 complete` 的 `ARRAY_PARTITION`，BRAM 数量按 `D` 倍增长。**这是本方案最可能「纸面可行、综合爆炸」的地方。**

**[推断] R2 — K 的广播/分区压力。** QK 阶段每周期要广播 `K[j][d*P+p]` 给一整行 `D` 个 PE；同一个 `j` 的所有 PE 需要不同 `p` 份数据，实质是需要 `P` 个读端口（或宽字打包）。这会推动「用宽 BRAM 字 + 位切片」或 `ARRAY_PARTITION`，也直接影响 §2.1 里「阵列尺寸由内存位宽决定」那条官方经验。

**[推断] R3 — 即使改了形状，HLS 仍然不流水。** 「控制流太复杂」可能并非只来自方阵，而是来自 softmax 的条件更新、可变循环边界、或 PE 的多周期语义。**必须用消融实验区分**：把 softmax 整段删掉只留 QK，看 II 是否变好、资源是否随 `D·P` 线性增长。

**[推断] R4 — 阵列被隐式复制。** Vitis HLS 在展开/内联时可能把同一个函数实例化成多份（尤其在 `UNROLL` 或 `DATAFLOW` 混用时），从而违反「exactly one array」。[事实] 我检索到确有与「QK、PV 和中间的 online-softmax 在同一阵列内执行」直接相关的近期工作，但**未能获取其正文**（见 §5.2），因此不引用其结论。工具是否真照做，必须**从综合报告的层次/实例数（以及 RTL 实例数）确认**——[事实] ADAPTOR 明确提到其设计中不同单元用不同数量的计算/tile/array 形状，且「tiling 沿矩阵列进行、需要被补充新的数据 `d_model/TS_MHA` 次」，说明 tiling 与 buffer 形状是由阵列结构决定的、需要逐项核对。
来源：[ADAPTOR, arXiv:2411.18148](https://ar5iv.labs.arxiv.org/html/2411.18148)。

**[推断] R5 — fp32 累加路径打不到 7.300 ns。** 每个 PE 一个 fp32 累加器，加上可能的 max-rescale 乘法，关键路径会落在 fp32 加法/比较上。**[未验证]** 我无法从公开来源确认 Vitis HLS 2024.2 在 xcvu37p 上 fp16×fp16→fp32 MAC 的具体延迟与 DSP 映射方式，因此这条只能靠综合报告确认。

**[推断] R6 — 浮点累加顺序与数值一致性。** 若将来为了 latency 引入 split-K，partial combine 会带来浮点非确定性（FA2 的 backward 就因此有 `deterministic` 选项）。**[推断]** 对本项目，正确做法是**不做 split-K**（§1.1 已论证动机不成立），head dim 128 的串行累加是**定序**的，不引入该风险。

**[推断] R7 — 我引用的关键先例是 ASIC/非 HLS。** SALO 是 Chisel→Verilog + Synopsys DC（45nm），SWAT 是 HLS 但架构与 tiling 目标（window attention）不同。**ASIC 上成立的结构在 HLS 上未必能流水**，这是整个方案最大的不确定性。

### 5.2 我明确无法验证的内容

- **[未验证]** 「control-flow is too complicated」这一 Vitis HLS 具体错误串：我在多轮检索中未能定位到 AMD/Xilinx 官方文档或可引用论坛帖。检索到的 UG1399 官方 PDF 之类来源无法通过抓取工具读取内容（返回 `unsupported content type "application/pdf"`），因此我**不给这条错误信息配任何引用**。用户如果手上有该报错的完整文本与所在循环，那是本调研最有价值的补充输入。
- **[未验证]** xcvu37p 的 DSP/LUT/BRAM 官方资源表数字（因此 §2.2 里我只引用 ADAPTOR 给出的「UltraScale+ 约 9024 DSP」这一通用量级，不写 VU37P 的具体数）。
- **[未验证]** FARE（ACM/SIGDA FPGA 2026, DOI 10.1145/3748173.3779572）的正文：ACM DL 返回 403/Cloudflare 拦截，我**没有读到该论文内容**，因此本文不对 FARE 做任何事实性陈述，只把它的标题与 DOI 列为待读来源：[FARE: A Fine-grained Pipelined Reconfigurable FlashAttention Kernel](https://dl.acm.org/doi/10.1145/3748173.3779572)。考虑到标题（细粒度流水 + 可重构 + FlashAttention）与本项目高度重合，**这是最该优先补读的一篇**。
- **[未验证]** SALO 的完整评测表（功耗/面积数字）因 ar5iv 页面截断未取全；本文只引用其 PE 阵列规模 32×32、每 PE 内容与 5 阶段数据流。
- **[未验证]** 我未能获取 AMD Versal DSP58 白皮书与 Versal DSP Engine 手册（PDF 抓取受限），因此不对「DSP58 可打包两个 16-bit 乘法」之类的细节做陈述。

### 5.3 低成本验证计划（Vitis HLS，按顺序）

**[推断]** 每一步都只做 C 综合，看报告里的 **II、Latency、DSP/LUT/FF/BRAM、层次实例数**，并把时钟目标保持 7.300 ns 不变：

| 步 | 微基准 | 看什么 | 通过标准 |
|---|---|---|---|
| V0 | **单 PE**：fp16 reg + fp32 acc，内循环纯 MAC，`PIPELINE II=1` | 关键路径、DSP 映射方式、II | II=1 且估算时钟 ≥ 100 MHz（即 ≤7.300 ns） |
| V1 | **D×P QK 阵列**（D=16, P=16），K 从 BRAM 流式、`ARRAY_PARTITION` 到位，**不带任何 softmax** | 资源是否 ≈ 256 × 单 PE；II；是否有阵列被复制 | 资源线性、II=1、层次只有一份阵列 |
| V2 | V1 + **行尾归约树**（只做 row-max） | 归约树深度、LUT 增量、II 是否退化 | II 仍为 1（或明确可接受的 II） |
| V3 | V2 + **online rescale**（`m` 更新 + `α` 乘到 acc）+ exp2 | 控制流是否仍可流水；比较 **不加** `Is_first`/tile 边界特化 vs 加了之后 | 加特化后 II 改善，这是验证「条件分支是元凶」的关键实验 |
| V4 | V3 + **PV 复用同一阵列**（含 V 的转置读取） | BRAM 端口/数量爆炸点；`ARRAY_PARTITION` 后 BRAM 数 | BRAM 在器件预算内；若爆炸则回到 4.3 讨论 DMA 侧转置 |
| V5 | 完整 kernel + DMA 接口 | 接口协议、`ap_ctrl_hs` 事务语义、端到端延迟 | 与 §4 分析一致 |

**[推断]** 这个顺序的价值在于：**V1→V3 的对比直接回答了「D=16 不流水到底是方阵造成的还是 softmax 造成的」**。如果 V1 在 256 PE 下轻松 II=1，而 V3 一加 softmax 就崩，那么结论是「把 softmax 移出最内层、改成 tile 边界特化」，而不是改阵列形状；反之如果 V1 就已经不流水，那是 PE 的多周期语义/连线的问题，形状要改得更彻底（退到 1-D lane 方案）。

**[推断]** 另外强烈建议保留一份「只改形状、其他 pragma 不变」的对照综合，并把两份报告的资源/II 并列——因为 §5.1-R3 是最大风险，只有这个对照能证伪它。

---

## 6. 排序后的推荐与置信度

| 排名 | 方案 | 理由 | 置信度 |
|---|---|---|---|
| **1** | **D×P 矩形阵列（P 沿 head 维，P 由 AXI 位宽定），行尾小归约树；QK 与 PV 用同一阵列的两个数据流（PV 把归约维 `l` 变成时间维）；softmax 用 FA2 式「循环内最少 + tile 边界特化 + 最后除一次」** | 满足「一个阵列」；把行归约从 PE 内挪到行尾，消掉控制流复杂度的结构性来源；head dim 16→128 通过 `HD/P` 串行累加平滑扩展，**不随 D 平方增长**；SALO 已证明「同一 PE 阵列跑完 QK/softmax/row-sum/PV」可行 | **medium** |
| 2 | 纯 **1-D lane + 独立归约树**（P 个点积 lane，行归约完全外置） | 控制流最简单、最可能一次流水成功；但 PV 需要 V 转置读取，且阵列的「行」不再有物理含义，`D` 行的并行度要靠多遍循环换回 | medium-low |
| 3 | 保持方阵，只把 **softmax/row-stat 移出阵列**（用独立小单元 + BRAM 存 `Prob`） | 改动最小，能快速验证「到底是不是方阵的锅」；但按 SWAT 的分析，方阵仍需方形成块、对行 softmax 次优，head dim 128 上方阵会迅速吃掉 DSP | medium-low |
| 4 | 沿 key 维 split-K / flash-decoding 风格切分 | 数学上有先例（SALO 的 window splitting 重归一化），但**它解决的是并行度不足**，单 FPGA 无此问题；且引入 partial 状态与 combine 的额外复杂度 | low（作为**本项目的主要分解手段**）；若只是为了「保留 partial 累加结构以便将来多器件」则有意义 |

**总体置信度：medium。**

**支撑 medium 的证据**：SALO 证明单阵列可跑完整个 attention；SWAT 明确指出方阵对行 softmax 次优；FA2/FlashAttention 给出了在线 softmax 与该省什么、该延后什么的明确准则；Vitis BLAS 官方文档给出「阵列尺寸由内存位宽决定」的直接经验。

**拉低置信度的缺失证据（按重要性排序）**：
1. **FARE 论文正文**（[DOI](https://dl.acm.org/doi/10.1145/3748173.3779572)）——标题与约束高度重合，但被 Cloudflare 拦截，我完全没读到。这是第一优先。
2. **用户的 HLS 报错原文**（"control-flow is too complicated" 的完整上下文与所在循环）——我能找到的所有公开资料都无法证实这一错误串，它直接决定 §5.3 的 V1 vs V3 哪个更关键。
3. **Vitis HLS 2024.2 在 xcvu37p 上 fp16→fp32 MAC 的调度与 DSP 映射**（UG1399 相关章节）——决定 7.300 ns 与 DSP 预算是否成立，我只能靠 V0 微基准实测。
4. **VU37P 官方资源表**——决定 P 能取到多大（我目前只能引用「UltraScale+ 约 9024 DSP」这一通用量级）。
5. **SALO 的完整资源/功耗评测**——目前只拿到 PE 阵列 32×32 与每 PE 结构。

---

## 附：本文引用来源一览

**同行评审论文**
- [FlashAttention: Fast and Memory-Efficient Exact Attention with IO-Awareness (NeurIPS 2022), arXiv:2205.14135](https://arxiv.org/abs/2205.14135)
- [FlashAttention-2, arXiv:2307.08691](https://arxiv.org/abs/2307.08691)
- [SpAtten: Efficient Sparse Attention Architecture with Cascade Token and Head Pruning (HPCA 2021), arXiv:2012.09852](https://ar5iv.labs.arxiv.org/html/2012.09852)
- [A3: Accelerating Attention Mechanisms in Neural Networks with Approximation (HPCA 2020), arXiv:2002.10941](https://arxiv.org/html/2002.10941v1)
- [SALO: An Efficient Spatial Accelerator Enabling Hybrid Sparse Attention Mechanisms for Long Sequences (DAC 2022), arXiv:2206.14550](https://ar5iv.labs.arxiv.org/html/2206.14550)
- [SWAT: Scalable and Efficient Window Attention-based Transformers Acceleration on FPGAs (DAC 2024), arXiv:2405.17025](https://ar5iv.labs.arxiv.org/html/2405.17025)
- [FlightLLM: Efficient Large Language Model Inference with a Complete Mapping Flow on FPGAs (FPGA 2024), arXiv:2401.03868](https://ar5iv.labs.arxiv.org/html/2401.03868v2)
- [ADAPTOR: A Runtime-Adaptive Transformer Neural Network Accelerator on FPGAs, arXiv:2411.18148](https://ar5iv.labs.arxiv.org/html/2411.18148)
- [Understanding the Potential of FPGA-Based Spatial Acceleration for LLM Inference (ACM TRETS 2024), arXiv:2312.15159](https://ar5iv.labs.arxiv.org/html/2312.15159v2)
- [A Survey on Hardware Accelerators for Large Language Models, arXiv:2401.09890](https://ar5iv.labs.arxiv.org/html/2401.09890)
- [DFX: A Low-latency Multi-FPGA Appliance for Accelerating Transformer-based Text Generation, arXiv:2209.10797](https://arxiv.org/abs/2209.10797)

**官方 AMD/Xilinx 文档**
- [Vitis BLAS Library L2 GEMM Kernel（脉动阵列尺寸与内存位宽的关系）](https://xilinx.github.io/Vitis_Libraries/blas/2022.1/user_guide/L2/L2_gemm_content.html)

**工程性技术文档（非同行评审，已在正文标注）**
- [llm-infra-atlas: FA2 并行度与 work partitioning 的逐段代码对照](https://raw.githubusercontent.com/llm-infra-atlas/llm-infra-atlas.github.io/52e065bbaecfc42256a25b97713adc8fb1c9a200/docs/attention/fa/02_fa2_parallelism.md)

**待补读（未能获取正文）**
- [FARE: A Fine-grained Pipelined Reconfigurable FlashAttention Kernel (ACM/SIGDA FPGA 2026), DOI 10.1145/3748173.3779572](https://dl.acm.org/doi/10.1145/3748173.3779572) —— HTTP 403 / Cloudflare 拦截
- [AMD Versal DSP Engine 手册](https://0x04.net/~mwk/xidocs/am/am004-versal-dsp-engine.pdf)、AMD Versal AI Engine DSP 白皮书 —— PDF 抓取受限
