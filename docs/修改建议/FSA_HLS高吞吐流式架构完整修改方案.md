# FSA-HLS 高吞吐流式架构完整修改方案

## 1. 结论先行

当前实现的数学流程已经接近 FSA，但综合结构仍然是“软件式逐 logical step 推进”：外层调度每次构造一个 `ExecutionPlanStep`，再把包含 SA、Delayer、Accumulator 和 SRAM 的整份 `FsaCoreDatapathState` 从 `current` 计算到 `next`。这种写法适合作为逐拍黄金模型，却把整个阵列状态变成 HLS 流水循环的反馈变量，最终形成 SA 顶层 II=16、数据通路推进 II=20、请求调度 II=39。局部函数内部即使标注 `PIPELINE II=1`，也无法转化成系统每拍一个 token 的吞吐率。

建议采用“双路径”策略：

- 保留当前 `step(current, next)`、`ExecutionPlan` 和相关测试，作为周期精确的功能参考模型；
- 新建独立的综合优化路径，以一次 query block 或完整 attention 为一次 `ap_ctrl_hs` 事务；
- 在事务内部用 `hls::stream` 和 `DATAFLOW` 连接加载、FSA 阵列、在线 Accumulator 和写回；
- FSA 阵列本身必须是一个持久化的 PE/CMP 流网络，`S -> N -> P` 始终保存在 PE 寄存器中；
- 每个 PE 只保留一条物理 FMA 流水线，QK、减 max、PWL exp2、rowsum 和 PV 通过不同 opcode 复用它；
- 不把 rowmax、exp2、rowsum 或 P 矩阵搬到阵列外，否则会退化为普通的“GEMM + materialized softmax + GEMM”，失去 FSA 的核心价值；
- 暂不处理 controller 指令重叠，但应把 `ATTENTION_SCORE_COMPUTE` 和 `ATTENTION_VALUE_COMPUTE` 在阵列内部融合，使 rowsum 与 PV 按论文时序重叠。

最优先的不是继续调小现有 `II=39`，而是取消“一个 logical step 对应一次多拍函数调用”的综合边界。

## 2. 审计范围与依据

本方案逐一检查了以下源码：

- `include/fsa/`：配置、数据类型、控制 token、PE、CMP、SA、Delayer、Accumulator、Banked SRAM、DMA、ExecutionPlan、Core 数据通路和全部 HLS 顶层接口；
- `src/core/`：`arithmetic.cpp`、`pe.cpp`、`cmp.cpp`、`systolic_array.cpp`、`delayer.cpp`、`accumulator.cpp`、`banked_sram.cpp`、`execution_plan.cpp`、`fsa_core_datapath.cpp`、`dma.cpp`；
- `src/hls/`：各模块顶层、`fsa_core_request_top.cpp` 和 `fsa_dma_top.cpp`。

同时对照：

- `C:/Users/30130/Desktop/高阶实验/论文调研/FSA.pdf`；
- `参考代码/gemm_hls/kernel/Top.cpp` 与 `Compute.cpp`；
- `参考代码/hls-fpga-accelerators/matmul/matmul.cpp`、`softmax/softmax.cpp`、`rmsnorm/rmsnorm.cpp`；
- `docs/综合报告/` 中现有 SA、PE、Accumulator、Core、Request 和 DMA 综合报告。

两个参考项目提供的是 HLS 结构方法，而不是可直接复制的 attention 算法：`hls-fpga-accelerators` 展示了 load/compute/store 的宏观 `DATAFLOW`，`gemm_hls` 展示了持久 PE、stream array、双缓冲和经证明的依赖消除。其普通 softmax 会读取输入两遍，不能照搬到 FSA，因为 FSA 的目标正是让 S/N/P 留在阵列内。

## 3. 当前实现为何性能低

### 3.1 `current/next` 形成了巨型循环携带依赖

`fsa_core_datapath_step()` 在一次调用中创建 `next_input_delayer`、`next_sa`、`next_output_delayer`、`next_accumulator`，末尾再整体提交：

```cpp
state.input_delayer = next_input_delayer;
state.sa = next_sa;
state.output_delayer = next_output_delayer;
state.accumulator = next_accumulator;
```

`systolic_array_top()` 同样保留完整静态 `current`，计算完整 `next` 后执行 `current = next`。这准确表达了软件模型的时钟沿，但在 HLS 中，下一次迭代必须等待整份状态写回；PE 内多周期浮点路径、转换路径和 CMP/Accumulator 状态都进入同一个 recurrence。

现有报告已经表现出这一点：

| 层级 | 当前源码/报告结果 | 含义 |
| --- | ---: | --- |
| `peMacUnit` | 固定流水，II=1 | 单个算术子模块能逐拍接收 |
| SA stage | 局部 II=1，Latency 约 16 | 局部包装可流水 |
| `systolic_array_top` | II=16 | 整阵列状态提交仍串行 |
| `advanceDatapath` | II=20 | SA、Accumulator、SRAM 状态反馈共同限制 |
| request scheduler | II=39 | logical step 的发射速率只有每 39 拍一次 |

因此，“给 `systolic_array_step` 再加一个 `PIPELINE II=1`”不会解决根因。

### 3.2 logical step 与物理时钟没有解耦

`ExecutionPlan` 是按论文/Chisel 风格生成逐拍控制波前的，语义上没有问题。但当前每个 logical step 都通过 `advanceDatapath()` 发射，而该调用本身 II=20，外层调度又是 II=39。论文中的一个架构拍被放大成几十个物理拍。

当前增加的 `registerDatapathInput()` 也没有形成可靠的物理寄存器边界：已有 DMA 综合报告中该函数为 Latency=0、0 FF/0 LUT，本质上仍是连线。若确实需要切时序，应使用显式 stream/FIFO 或让生产者、消费者成为不同 DATAFLOW process，而不是依赖一个“返回输入值”的包装函数。

以 `SA_ROWS=128、SA_COLS=4` 为例，仅三条基本指令的 logical step 数约为：

- `LOAD_STATIONARY`：5；
- `ATTENTION_SCORE_COMPUTE`：400；
- `ATTENTION_VALUE_COMPUTE`：260；
- 合计：665 logical steps/KV tile，尚未计预装载和最终归一化。

即使所有 step 都有效，按调度 II=39 也已经约为 25,935 拍/KV tile。

### 3.3 128×4 配置存在严重的 K/V 预装载浪费

当前接口其实已经分配了：

```cpp
q[SA_COLS][SA_ROWS]
k[SA_ROWS][SA_ROWS]
v[SA_ROWS][SA_ROWS]
```

但 `active_keys` 被限制为 `<= SA_COLS`，`fsa_dma_top()` 每个 KV tile 只装入前 `SA_COLS` 个真实 key，随后 `fsa_core_request_run()` 仍把 `SA_ROWS` 行 K 和 `SA_ROWS` 行 V 全部写入 Scratchpad。对于 128×4：

- `SPAD_SUB_BANKS = 32`；
- Q 预装载：`4 × 32 = 128` step；
- K 预装载：`128 × 32 = 4096` step；
- V 预装载：`128 × 32 = 4096` step；
- 合计 8320 logical steps，其中绝大部分只是反复写零填充。

这是当前矩形配置性能极低的第一现场原因之一。主方案应把 `KEY_TILE` 设为物理 PE 行数，即 128×4 阵列一次处理 128 个 key、4 个 query；若坚持 4-key tile，则必须只搬运有效行并用 valid token 屏蔽其余行，不能写 124 行零。

### 3.4 PE 的 MAC 与 exp2 没有真正复用硬件

`peMacUnit()` 在运行时分支中调用：

```cpp
peMac(...)
peExp2PWL(...)
```

而 `peExp2PWL()` 内部还有独立的 `hls::fma`。C++ 中互斥不等于 RTL 自动共享。现有 4×4 SA 报告显示每个 `peMacUnit` 使用 17 DSP，整个 SA 为 274 DSP，并存在 16 个独立 `peExp2PWL` 层次。这说明当前实现更接近“普通 MAC 硬件 + PWL FMA 硬件同时存在”，与论文“利用同一 PE MAC 完成 PWL”的面积假设不一致。

此外，`pe_step()` 即使 token 无效也先计算 `peMacUnit()`；`valid` 只控制输出和状态提交，不能可靠阻止算术硬件存在或切换。

### 3.5 CMP 的 UPDATE 路径不必要地经过 FMA

`accCmp()` 用 `hls::fma(in_a, 1, -in_b)` 同时产生差值，再根据符号选择 max。可是在 `UPDATE` 阶段只需要 max，`oldMax-newMax` 只在每个 tile 的 `PROP_MAX_DIFF` 阶段计算一次。当前写法把高频 rowmax recurrence 绑定到浮点 FMA/减法器延迟上，并额外消耗 DSP。

应拆成：

- `UPDATE`：专用 ordered FP compare + mux，目标每拍更新一次 `newMax`；
- `PROP_MAX_DIFF`：每列每 tile 只发射一次 FP subtract；
- `PROP_MAX`、`PROP_ZERO`、PWL 截距：纯选择/常量流。

### 3.6 Accumulator 把快路径、PWL 和倒数混在同一命令函数

`accumulator_lane_step()` 用一个运行时 `cmd` 在以下路径间选择：

- `EXP_S1` 的 FMA；
- `EXP_S2` 的 PWL exp2；
- `ACC_SA`/`ACC` 的 FMA；
- `SET_SCALE`；
- 15 logical step 的 restoring reciprocal。

这导致函数 Latency/Interval 随命令变化，`scale` 和 `reciprocal` 状态又反馈到下一次调用。倒数只在最终归一化使用，却拖累每个 tile 的热路径结构。

### 3.7 SRAM 和 DMA 结构适合功能验证，不适合高吞吐顶层

`banked_sram.cpp` 为了通用性，完全展开端口仲裁、bank/sub-bank 冲突判断和动态地址到静态 row 的逐行比较。小配置可用，但在 128 行、多个端口和宽行配置下会形成较大的组合选择网络。

`fsa_dma_top()` 还存在：

- Q/K/V/O 全部放在同一个 `gmem` bundle；
- `loadRequestTile()` 内顺序调用行加载；
- `dma_load_elem_row` 被限制为一个实例；
- 顶层没有 `DATAFLOW`，下一 tile 搬运不能与当前 tile 计算重叠；
- request/response 通过大数组结构传递，而不是稳定的 stream/buffer 边界。

## 4. 目标架构

### 4.1 顶层宏观数据流

建议新建独立综合入口，例如 `fsa_attention_dataflow_top()`：

```text
DDR/HBM
  │
  ├─ Q loader ────────┐
  ├─ K loader → ping/pong ─┐
  └─ V loader → ping/pong ─┤
                            ▼
                    FSA tile scheduler
                            │ token
                            ▼
             persistent FSA PE/CMP stream array
                            │ rowsum/PV/scale token
                            ▼
               online accumulator fast path
                            │
               final reciprocal + normalize
                            │
                            ▼
                         O writer
```

顶层使用 `#pragma HLS DATAFLOW`，划分为固定消费/产生计数的进程：

1. `load_q_block()`；
2. `load_kv_tile_pingpong()`；
3. `schedule_fsa_tile()`；
4. `fsa_array_engine()`；
5. `online_accumulator_engine()`；
6. `normalize_and_store()`。

controller 指令重叠可以暂时不做；一次 `ap_start` 内仍按固定顺序处理 query block 和 KV tiles。这里的 DATAFLOW 用于搬运、阵列、Accumulator、写回的并行，不依赖双 controller FSM。

### 4.2 DATAFLOW 边界的关键原则

不建议写成：

```text
qk_stage -> rowmax_stage -> exp_stage -> rowsum_stage -> pv_stage
```

如果五个 stage 都各自调用一遍 SA，HLS 会生成多套阵列，既无法让 S/N/P 留在同一 PE，也无法复用 FMA。正确结构是只有一个持久的 `fsa_array_engine()`，内部 PE 根据 token opcode 在不同时间执行不同阶段。

宏观 load/compute/store 使用 DATAFLOW；阵列内部使用 stream array + 空间展开的持久 PE 进程。两层结构应同时存在。

## 5. PE/CMP 流网络的具体改法

### 5.1 用局部状态所有权替代整阵列 `current/next`

建议新增而不是直接破坏现有模型：

```text
include/fsa/stream_types.hpp
include/fsa/stream_pe.hpp
include/fsa/stream_array.hpp
src/core/stream_pe.cpp
src/core/stream_array.cpp
src/hls/fsa_attention_dataflow_top.cpp
```

每个 PE 进程独占自己的 `reg`、`exp2Done` 和必要 tag。PE 之间只通过 stream 交换 token，不再有一个函数读写 `SystolicArrayState mesh[...][...]`。

推荐的 stream 形状：

```cpp
hls::stream<PeToken> lr[PE_ROWS][PE_COLS + 1];
hls::stream<AccToken> down[PE_ROWS + 1][PE_COLS];
hls::stream<AccToken> up[PE_ROWS + 1][PE_COLS];
hls::stream<CmpToken> cmp_ctrl[PE_COLS + 1];
```

在 `DATAFLOW` 区域内用完全展开的循环实例化，并把位置作为函数实例化参数：

```cpp
for (int r = 0; r < PE_ROWS; ++r) {
    #pragma HLS UNROLL
    for (int c = 0; c < PE_COLS; ++c) {
        #pragma HLS UNROLL
        const int location = r * PE_COLS + c;
        pe_process(..., location);
    }
}
```

`pe_process()` 可使用 `FUNCTION_INSTANTIATE variable=location`，或者改用编译期 `static_for`/模板递归生成不同位置的实例。不能直接把普通 C++ 循环变量 `r/c` 当作模板实参。

每个进程在一次 tile 事务中循环固定 token 数，并以 II=1 读写 token。即使是 bubble，也传递 `valid=false` 的 token；不要让不同分支可选地少写一个 stream，否则很容易造成 DATAFLOW 死锁。

### 5.2 token 必须携带控制和延迟对齐信息

建议至少包含：

```cpp
enum class PeOp : ap_uint<4> {
    LOAD_Q, QK_MAC, SCORE_RESTREAM, SUB_MAX,
    EXP2_PWL, ROWSUM, PV_MAC, PASS, BUBBLE
};

struct PeToken {
    bool valid;
    PeOp op;
    ap_uint<TAG_W> tag;
    elem_t horizontal;
    acc_t vertical;
    elem_t coefficient;
    acc_t intercept;
    bool last;
};
```

`op/valid/tag/last` 必须经过与 FMA、FP16 转换和 Split 相同深度的控制延迟线。不能像当前 `pe_step()` 那样在函数调用返回时立即用本拍 control 解释一个多拍算术结果。

### 5.3 每个 PE 只保留一个 FMA 调用点

把普通 MAC 和 PWL 的操作数预选择合并到 FMA 之前：

```cpp
FmaOperands operands = select_operands(token, pe_reg, up, down);
acc_t fma_result = pe_fma(operands.a, operands.b, operands.c); // 唯一调用点
FmaCommit commit = delay_control(token, FMA_LATENCY);
commit_result(commit, fma_result, pe_reg, outputs);
```

其中：

- QK/PV/rowsum：`a=pe_reg`，`b=horizontal`，`c=vertical`；
- 减 max：`a=1`，`b=score`，`c=-newMax`，或者使用共享 subtract 模式；
- PWL：Split 先产生 `fractional_x`，然后 `a=fractional_x`，`b=slope`，`c=intercept`；
- pass/bubble 不发起有效提交，但仍保持 token 时序对齐。

为防止 HLS 再次复制 PE 内算术：

- `pe_fma()` 保持单一静态调用点；
- 优先用“一个调用点 + 固定 opcode 数据选择”表达复用；若使用 `ALLOCATION`，必须确认约束作用域只限制单 PE 内部，不能在阵列顶层把全阵列错误地限制成一条共享 FMA；
- PE 空间循环仍完全展开，因此目标是“每 PE 一条 FMA”，不是全阵列共用一条 FMA；
- 综合后必须用 operator/instance 报告确认每个 PE 只有一套 FMA，不以 C++ 分支外观作为证据。

论文的面积优势来自这种复用。以当前 4×4 报告为例，单 PE 17 DSP 明显偏高；重构后的验收标准不是某个预设 DSP 数，而是普通 MAC 与 PWL 不再各有独立 FMA 实例。

### 5.4 PWL Split 单元应从 FMA 中分离

`peExp2PWL()` 当前同时做 FP 特殊值、截断、分段选择、FMA、`ldexp` 和下溢舍入。建议拆成：

1. `split_exp2_input()`：从浮点位模式提取整数部分、fraction、segment/match；
2. 复用 PE FMA 计算 `fraction * slope + intercept`；
3. `scale_pow2()`：调整指数并处理零、下溢和饱和；
4. 根据 segment match 和 tag 只提交正确结果。

第一版保持 8 段 PWL 和现有数值行为。第二版可在误差测试允许时对过小负指数直接 clamp 为 0，从而简化次正规数变量移位逻辑。

### 5.5 CMP 改为无 DSP 的 rowmax 热路径

每列一个 CMP 进程，局部保存 `oldMax/newMax`：

- `UPDATE` 每拍接受一个 score，用 IEEE ordered compare + mux 更新 `newMax`；
- causal/padding 通过 token 的 `masked` 位将 score 替换为 `-Inf`；
- score 同时写入向下 restream FIFO，使其随后回到原 PE；
- tile 结束后只计算一次 `oldMax-newMax`，然后更新 `oldMax=newMax`；
- PWL 系数/截距和 max 反馈沿同一向下 token 通路传播。

必须显式测试 `-Inf`、`+Inf`、NaN、正负零。若 attention 输入策略规定 NaN 直接传播或屏蔽，应固定为一种语义，避免 HLS 原生比较和软件参考不一致。

## 6. 严格保持 FSA 的计算移动路径

### 6.1 不应移出阵列的计算

以下内容必须保留在 FSA 阵列及其顶部 CMP/底部 Accumulator 邻近路径：

- QK 产生 S；
- S 上行到 CMP 并逐行求 `new_m`；
- S 向下 restream，在 PE 内变为 N；
- 8 组 PWL 系数流经同一 PE FMA，将 N 原地变为 P；
- 左侧注入 1、顶部注入 0，在同一阵列求 rowsum(P)；
- 下一拍开始注入 V，利用仍驻留在 PE 中的 P 计算 PV；
- 旧 `l/O` 的 rescale 与新 rowsum/PV 在底部 Accumulator 合并。

尤其不要把整个 S tile 写入 BRAM 后交给独立 softmax 核。那会增加 S 的写读带宽、需要第二套 exp 计算资源，并破坏论文“单一 SA 完成 FlashAttention”的结构。

### 6.2 应移出热路径的内容

可以并且应该移动的是：

- `ExecutionPlan` 的逐 step 大型组合译码：改成 tile 级固定 token 发生器；
- SRAM 通用端口仲裁：用专用 Q/K/V ping-pong buffer 服务确定性访问；
- 最终 reciprocal：从每 tile 快路径拆出，只在最后一个 KV tile 后运行；
- AXI 打包/解包：独立 loader/writer DATAFLOW 进程；
- debug 大结构输出：改成可选计数器或小状态寄存器，不穿过主数据通路。

## 7. rowsum 与 PV 的融合

当前 `ExecutionPlan` 把 `ATTENTION_SCORE_COMPUTE` 和 `ATTENTION_VALUE_COMPUTE` 分成两条完整指令。这样虽然功能正确，但在请求层形成阶段边界和额外填充/排空。

建议新增一个 tile 内部 schedule：

```text
LOAD_Q
QK + score upward
CMP rowmax + score restream
subtract max + 8-cycle PWL coefficient wave
rowsum wave starts
one cycle later PV wave starts
bottom accumulator consumes rowsum/PV as tagged streams
```

rowsum 和 PV 并不是要求同一个 PE 同拍执行两次 FMA，而是利用波前的空间错位：rowsum 波前离开某个 PE 后，PV 波前下一拍进入，因此仍然是一 PE 一 FMA。token scheduler 必须根据实际 FMA/Split 延迟重新标定波前，而不是直接照抄论文的理想整数拍数。

论文给出的 `5N+10` 是架构级逻辑周期参考；FP16/FP32 HLS IP 具有多拍物理延迟，实际目标应表述为“稳态 token II=1，额外代价仅为固定 fill/drain”，而不是强制综合报告恰好等于 `5N+10`。

## 8. Accumulator 重构

### 8.1 拆成快、缩放、慢三条路径

建议把当前 `accumulator_lane_step()` 拆为：

1. `acc_fast_process()`：处理 `new_l = alpha * old_l + rowsum`、`new_O = alpha * old_O + PV` 和最终 `O * inv_l`；
2. `scale_process()`：处理 `alpha = exp2((old_m-new_m)*attentionScale)`，每列每 tile 一次；
3. `reciprocal_process()`：只在 finalize 后处理 `1/l`。

快路径以 query lane 完全展开，内部固定延迟、目标 II=1。`scale_process` 可以在阵列执行 PWL/rowsum 的同时提前完成，结果通过带 `query/tag` 的 FIFO 送到快路径。reciprocal 不再参与每个 logical step 的状态选择。

### 8.2 在线状态的存储

对每个 query block，需要保存：

- `m[QUERY_TILE]`；
- `l[QUERY_TILE]`；
- `O[QUERY_TILE][HEAD_DIM]`。

`m/l` 可完全分区为寄存器；O 的 query lane 维完全分区，feature 深度映射到 BRAM/URAM 或成组宽字。读旧 O 与写新 O 用 ping-pong 或明确的一读一写端口，避免通用 `AccRAMIO` 仲裁进入热路径。

不要对深度 128/4096 的所有维度盲目 `ARRAY_PARTITION complete`。应只分区真正并行的 lane 维，对 feature/depth 维使用 `ARRAY_RESHAPE`、BRAM/URAM 和打包宽度匹配。

## 9. 参数体系必须解耦

当前 `SA_ROWS` 同时承担 head dimension、阵列行数和 K/V 存储高度，`SA_COLS` 同时承担阵列列数、query tile 和 key tile。这使矩形阵列语义混乱。

建议至少改成：

```cpp
constexpr int HEAD_DIM;
constexpr int PE_ROWS;
constexpr int PE_COLS;
constexpr int QUERY_TILE = PE_COLS;
constexpr int KEY_TILE;       // 非 Split-D 主方案取 PE_ROWS
constexpr int FEATURE_TILE;   // 未来 Split-D 时使用
```

非 Split-D 的 128×4 主配置建议：

```text
HEAD_DIM  = 128
PE_ROWS   = 128
PE_COLS   = 4
QUERY_TILE= 4
KEY_TILE  = 128
```

对应修改：

- `active_keys` 上限从 `SA_COLS` 改为 `KEY_TILE`；
- DMA 的 `key_base` 步长和 finalize 判断改为 `KEY_TILE`；
- K/V loader 循环装入 `KEY_TILE` 个真实 token；
- causal/padding mask 使用全局 query/key index 直接生成每列 masked 位，不再把 `causalCounter` 的范围绑定到 `SA_COLS`；
- Scratchpad/stream 计数由独立参数推导。

未来若采用真正的 Split-D（例如 16×16 阵列处理 D=128），必须先跨 8 个 D tile 累加完整 score，再进入 rowmax/exp；不能在部分 dot product 上提前做 softmax。该方向应作为第二阶段研究，不与当前非 Split-D 流式化同时修改。

## 10. 存储和 DMA 数据流

### 10.1 Q/K/V ping-pong

K/V tile 使用双缓冲：

```text
compute KV tile i from bank 0
load    KV tile i+1 into bank 1
swap
```

Q 在处理同一 query block 的每个 KV tile 前需要重新载入 PE，因为 PE 寄存器会依次被 S/N/P 覆盖；可以把 Q 保存在独立 Q buffer 中，再由 feeder 快速重灌阵列，而不必每次重新访问 DDR。

只有在证明 bank 交替且无别名后，才可对 ping/pong 数组使用 `#pragma HLS DEPENDENCE ... inter false`。绝不能对 PE reg、CMP `newMax`、Accumulator `m/l/O` 等真实反馈状态强行声明 false dependency。

### 10.2 AXI/HBM 端口

建议至少拆分 bundle：

```cpp
#pragma HLS INTERFACE m_axi port=q bundle=gmem_q ...
#pragma HLS INTERFACE m_axi port=k bundle=gmem_k ...
#pragma HLS INTERFACE m_axi port=v bundle=gmem_v ...
#pragma HLS INTERFACE m_axi port=o bundle=gmem_o ...
```

并为连续 row-major 数据设置合适的 burst length/outstanding 参数。若上 U280 HBM，再在链接配置中把 bundle 映射到不同 HBM pseudo-channel；只有 bundle 分开而物理上仍落同一 bank，不会获得真实带宽提升。

loader 每拍读取一个宽 word，内部 lane `UNROLL` 解包；不要为每个 FP16 发起独立 AXI 访问。参考 `hls-fpga-accelerators` 的 load/compute/store DATAFLOW 和 packet lane 展开即可。

### 10.3 FIFO 深度

初始建议而非最终常数：

- AXI loader 到 ping-pong writer：32～64；
- tile scheduler 到阵列入口：至少覆盖 feeder 短暂停顿和阵列 fill；
- CMP score restream：至少覆盖 CMP/compare 延迟与首行返回距离；
- array 到 accumulator：至少覆盖 scale 路径与 rowsum/PV 到达差；
- accumulator 到 writer：一到两个 burst 的数据量。

最终深度必须由 token 生产/消费距离和 cosim stall 统计决定。所有 DATAFLOW 进程应有可证明的固定总 token 数或明确 end token。

## 11. 对现有文件的模块级修改建议

| 现有文件 | 建议 |
| --- | --- |
| `config.hpp` | 解耦 HEAD_DIM、PE_ROWS、PE_COLS、QUERY_TILE、KEY_TILE；保留兼容别名过渡 |
| `types.hpp` | 增加带 `op/valid/tag/last/masked` 的 stream token；不要只传裸数据 |
| `control.hpp` | 保留旧控制供黄金模型；新增 tile/PE opcode，不再为综合热路径逐拍构造大型 `PECtrl` |
| `state.hpp` | 保留旧 `FsaCoreDatapathState`；优化路径只保存各进程局部状态和在线 m/l/O |
| `arithmetic.cpp` | CMP compare 与 diff 分离；统一 PE FMA 调用点；拆 PWL Split/FMA/scale |
| `pe.cpp` | 保留参考实现；新增 `stream_pe`，用 commit token 对齐多拍 FMA 结果 |
| `cmp.cpp` | 新增每列持久 CMP process、score restream 和单次 diff 流 |
| `systolic_array.cpp` | 不直接改坏旧逐拍模型；新增 stream mesh 作为综合主阵列 |
| `delayer.cpp` | 优化路径用入口 token skew FIFO 代替整体 `current/next` 延迟矩阵 |
| `accumulator.cpp` | 拆快路径、scale 路径、reciprocal；倒数移出 tile 热路径 |
| `banked_sram.cpp` | 保留通用测试模块；综合主路径改用专用 ping-pong buffer/在线状态 RAM |
| `execution_plan.cpp` | 保留作参考；新增 tile 级 token scheduler，融合 score/value 阶段 |
| `fsa_core_datapath.cpp` | 继续作为黄金模型，不再作为高吞吐顶层每 logical step 的调用体 |
| `fsa_core_request_top.cpp` | 保留兼容入口；新增 q-block 事务入口，不在 `II=39` 循环中推进全状态 |
| `fsa_dma_top.cpp` | 改为多 bundle、DATAFLOW loader/compute/writer、KV 双缓冲 |

`legacy_or_experimental` 中已有的 accumulator pipeline 不应直接接入主设计；需要先验证其 token、状态和当前 online softmax 语义完全一致，再决定是否复用代码。

## 12. 推荐的顶层伪代码骨架

```cpp
void fsa_attention_dataflow_top(/* AXI ports, shape, causal */) {
    #pragma HLS DATAFLOW

    hls::stream<QBlock> q_blocks;
    hls::stream<KVTile> kv_tiles;
    hls::stream<ArrayCommand> array_cmds;
    hls::stream<ArrayResult> array_results;
    hls::stream<OutputWord> output_words;

    #pragma HLS STREAM variable=q_blocks depth=2
    #pragma HLS STREAM variable=kv_tiles depth=2
    #pragma HLS STREAM variable=array_cmds depth=64
    #pragma HLS STREAM variable=array_results depth=64
    #pragma HLS STREAM variable=output_words depth=64

    load_q_blocks(q, q_blocks, ...);
    load_kv_tiles_pingpong(k, v, kv_tiles, ...);
    schedule_tiles(q_blocks, kv_tiles, array_cmds, ...);
    fsa_array_engine(array_cmds, array_results, ...);
    online_accumulator_engine(array_results, output_words, ...);
    store_outputs(output_words, o, ...);
}
```

实际实现中不要在 stream 中传递完整 128×128 数组。`QBlock/KVTile` 应是按内存/阵列吞吐宽度打包的 word token，ping-pong 数据放本地 RAM，stream 只传 word 或 buffer-ready 描述符。

## 13. 分阶段实施顺序

### M0：冻结基线和修正参数

- 保留全部旧测试；记录当前 4×4 与 128×4 的功能、cycles、II、DSP/LUT；
- 引入独立维度参数；
- 先修正 `active_keys/KEY_TILE` 和 K/V 零写浪费；
- 对历史综合报告注明源码时间，避免把旧的 7 套 core/2383 DSP 结果当成当前结果。截至 2026-08-24 的较新 DMA 构建报告已经是一套约 300-DSP core，但它仍记录 II=20/39；源码修改晚于该构建时必须重新综合确认。

### M1：单 PE 真正复用一条 FMA

- 实现 `PeToken`、operand select、唯一 FMA、control delay、commit；
- 测试所有 opcode 和连续 token；
- 综合确认每 PE 一套 FMA，PE 输入 II=1。

### M2：构建 stream mesh 和 CMP

- 实例化 PE stream array；
- 实现上下/左右波前；
- CMP 使用 compare/mux 更新 max，score restream；
- 与旧 `systolic_array_step` 逐 tile 比较。

### M3：S→N→P 原地路径

- 加入 8 个 PWL coefficient token；
- 实现 Split 和结果 match/commit；
- 验证 S 不出阵列、每个 PE 最终只保存正确 P。

### M4：rowsum/PV 重叠

- 融合 score/value tile schedule；
- rowsum 后一拍启动 PV 波前；
- 检查任何 PE 每拍最多一个 FMA 请求。

### M5：在线 Accumulator

- 快路径固定延迟 II=1；
- scale 与阵列重叠；
- reciprocal 只在 finalize 运行；
- 验证 2 个以上 KV tile 的 online m/l/O 更新。

### M6：顶层 DATAFLOW 和双缓冲

- Q/K/V/O 分 bundle；
- K/V ping-pong；
- loader、array、accumulator、writer 并行；
- 用 cosim 检查 FIFO stall、死锁和实际 tile 间隔。

### M7：扩展与可选 Split-D

- 先完成 128×4 非 Split-D 主配置；
- 再研究 16×16 等小阵列跨 D tile 的 score 累加；
- Split-D 版本必须在完整 dot product 后才进入 softmax。

## 14. 验证与验收标准

### 14.1 功能测试

至少覆盖：

- 单 KV tile和 2～4 个 KV tile；
- `initialize/finalize` 的全部组合；
- 非 causal、causal、跨 tile causal；
- `active_keys=1`、`KEY_TILE-1`、`KEY_TILE`；
- padding score=`-Inf`；
- FP16/FP32 的零、次正规、Inf、NaN 策略；
- reset 后第一事务、连续多个 query block；
- stream tag 顺序和每个生产者/消费者 token 总数。

建议在阶段边界而不是每个物理拍比较：QK score、rowmax、P、rowsum、PV、online m/l/O 和最终 O。旧 current/next 模型作为 reference，新路径允许物理延迟不同，但 tile 边界结果必须一致。

### 14.2 HLS 验收

- PE FMA 请求循环 II=1；
- CMP score 输入 II=1；
- feeder/collector II=1；
- Accumulator 快路径 II=1；
- 每 PE 只有一套 FMA，PWL 不再生成额外 FMA；
- S/N/P 不写入阵列外 BRAM；
- 顶层总周期接近“token schedule + 固定 fill/drain”，不再是 `logical_steps × 20/39`；
- DATAFLOW viewer 中 loader、compute、accumulator、writer 存在真实重叠；
- 无 FIFO deadlock，无未证明的 dependence false。

### 14.3 实现验收

HLS 估算通过不代表最终性能。必须继续执行 Vivado synthesis/place/route，检查：

- WNS/TNS 和实际 Fmax；
- PE 阵列长连线、控制广播和 ready fanout；
- BRAM/URAM 推断是否符合预期；
- U280 HBM bundle 到物理 bank 的映射；
- 单 SLR/跨 SLR 拥塞；
- 实际 AXI burst 和带宽利用率。

## 15. 优先级排序

如果只按性能收益和风险排序，建议依次执行：

1. 修正 128×4 的 `KEY_TILE` 语义和 K/V 8320-step 预装载浪费；
2. 新建流式综合路径，停止在 `II=39` 循环中调用完整 `fsa_core_datapath_step`；
3. 每 PE 单 FMA 真复用，并用 tag/commit 对齐多拍结果；
4. CMP max/diff 拆分，去掉 UPDATE 热路径上的 FMA；
5. 融合 score/value，使 rowsum 与 PV 重叠；
6. Accumulator 快慢路径拆分；
7. 顶层 Q/K/V/O 多 bundle、KV ping-pong 和 DATAFLOW；
8. 完成 128×4 后再讨论 Split-D。

这条路线既保留当前实现可验证的 FSA 语义，又把综合对象改造成 HLS 能识别的持久流网络。最终目标不是让现有逐步模型勉强达到更小 II，而是让每个物理拍都有有效 token 在 PE/CMP/Accumulator 流水线上前进。
