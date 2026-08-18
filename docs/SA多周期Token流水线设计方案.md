# SA 多周期 Token 流水线设计方案

## 1. 文档目的

本文档展开说明如何把《处理器架构设计：基于高层次综合的 RISC-V 实现》中使用的显式流水线方法迁移到当前 FSA-HLS 的 4×4 Systolic Array（SA）中。

本文档只给出架构设计、迁移步骤和验收标准，不直接修改 SA、PE、CMP 或算术模块源码。

本文档重点回答以下问题：

1. 书中的流水线方法本质上解决了什么问题。
2. 当前 SA 已经具备哪些相同结构，还缺少哪些结构。
3. 为什么不能只把顶层 `#pragma HLS PIPELINE II=16` 改成 `II=1`。
4. 如何把固定多拍算术延迟表示成显式的 token 流水线。
5. 如何处理 PE 内部状态、上下左右数据流、CMP、空泡和阶段切换。
6. 如何分阶段实现，使每一步都能独立验证和回退。

本文档的当前范围是 SA 及其直接依赖的 PE、CMP 和算术路径，不包含 DMA、HBM、系统级调度器重写，也不改变以下项目约束：

- 阵列规模仍为 4×4。
- `elem_t` 仍为 FP16，`acc_t` 仍为 FP32。
- 目标器件仍为 `xcvu37p_CIV-fsvh2892-2-e`。
- 目标时钟仍为 10 ns（100 MHz）。
- 第一阶段仍保留 `ap_ctrl_hs` 顶层控制协议。

## 2. 当前 SA 的时序模型

### 2.1 软件行为模型

当前核心模型采用 `current/next` 状态推进方式：

1. `systolic_array_step` 只读取 `current`。
2. 本次逻辑拍产生的新状态只写入 `next`。
3. 顶层最后执行 `current = next`。

这种写法表达的是 RTL 中“所有寄存器在同一个时钟边沿同时更新”的语义。当前 `SystolicArrayState` 中已经保存了：

- 4×4 个 PE 的内部状态；
- 4 个 CMP 的内部状态；
- CMP 控制横向 pipe；
- PE 控制横向 pipe；
- CMP 到 PE 的数据 pipe；
- PE 左到右、上到下、下到上的数据 pipe。

因此，当前模型已经具有书中 `_from_/_to_` 结构的核心思想。`current` 相当于本拍所有 `_from_`，`next` 相当于下一拍所有 `_to_`。

### 2.2 HLS 事务模型

当前 `systolic_array_top` 使用 `ap_ctrl_hs`。因此顶层的一次函数调用首先是一笔 HLS 事务，而不能不加说明地等同于一个 100 MHz 物理时钟周期。

当前顶层有：

```cpp
#pragma HLS PIPELINE II=16
```

已有综合报告给出的基线为：

| 项目 | 当前报告值 |
|---|---:|
| 顶层 Latency | 17 拍 |
| 顶层 Interval | 16 拍 |
| 顶层 Final II | 16 |
| `peMacUnit` Latency | 16 拍 |
| `peMacUnit` II | 1 |
| `peExp2PWL` Latency | 13 拍 |
| `peExp2PWL` II | 1 |
| `cvtAtoE` Latency | 2 拍 |
| `cvtAtoE` II | 1 |
| 估算时钟周期 | 7.150 ns |
| DSP | 274 |
| FF | 59,266 |
| LUT | 77,960 |

这里最重要的区别是：

- `peMacUnit II=1` 表示单个算术流水模块在流水线填满后，理论上可以每拍接收一组新操作数。
- 顶层 `II=16` 表示整个 SA 顶层每 16 拍才能开始一笔新的 SA 事务。

所以“PE 算术单元已经 II=1”不等于“整个 SA 已经能够每拍推进一个逻辑 step”。当前设计虽然存在 16 个独立 `peMacUnit`，但算术流水线的吞吐能力还没有在 SA 顶层暴露出来。

### 2.3 当前设计为什么是正确的

当前设计通过拉长整笔顶层事务，等待所有算术结果就绪后再提交 `next`，因此：

- 控制和算术结果不会错配；
- `PE.reg`、`exp2Done` 等状态不会被未完成事务覆盖；
- C/RTL 协同仿真可以按照“一次调用推进一个 SA 逻辑拍”的方式验证。

它的主要问题不是功能错误，而是吞吐率：一个原本在 Chisel 行为中表示“一拍”的 SA step，被 HLS 实现成了间隔 16 个物理时钟才能启动一次的事务。

## 3. 书中流水线方法的本质

书中的处理器流水线不是依靠一条 pragma 自动产生的，而是先在 C/C++ 模型中显式表达时序关系，再让 HLS 对已经相互独立的阶段进行调度。

### 3.1 上一拍输入和下一拍输出分离

每个流水级只读取 `_from_` 结构，只写 `_to_` 结构。主循环开始时把上一轮 `_to_` 复制为本轮 `_from_`。

这样做有两个作用：

1. 保证所有阶段看到的都是同一拍开始时的状态。
2. 避免某个阶段提前写出的结果被同一轮后面的阶段错误读取。

当前 SA 的 `current/next` 已经实现了这一点，因此这一部分不需要推倒重来。

### 3.2 使用 valid 表示真实操作和空泡

数据总线上的位模式始终存在，但只有 `valid=true` 时才能解释为真实 token。`valid=false` 表示流水线中的空泡。

空泡必须和真实 token 一样沿流水线传播，但空泡不能：

- 修改 PE 或 CMP 状态；
- 写入输出结果；
- 被当作真实控制信号；
- 触发 exp2 完成等一次性事件。

当前 SA 已经使用 `ValidData<T>`，但它主要覆盖相邻 PE/CMP 的一拍 pipe。若允许多个顶层事务重叠，还需要让 `valid` 一直伴随多拍算术结果移动。

### 3.3 使用 wait 冻结上游

当一个流水级不能接收新 token 时，它向上游发送 `wait`：

- 上游不接收新输入；
- 上游不改变当前输出；
- 下游看到无效输出，或者继续保持之前尚未消费的有效输出；
- 等待解除后从保存的输入继续执行。

这对应硬件中的回压。当前 `ValidData<T>` 没有 `ready`，SA 内部也没有 `busy/wait`，因此现有 SA 只能依靠顶层事务间隔保证不会覆盖未完成操作。

### 3.4 使用安全区保存多周期输入

多周期阶段不能直接对可能变化的输入端口持续计算。它需要在接受 token 时把所有输入锁存在内部安全区，然后只对安全区中的副本进行计算。

对于 PE，安全区至少要保存：

- 本次控制 `PECtrl`；
- 本次读取到的 `PE.reg` 快照；
- 左侧 `l_input`；
- 上方 `u_input`；
- 下方 `d_input`；
- 输入有效性；
- 输出方向和状态写回条件。

如果缺少这些信息，16 拍后返回的 MAC 结果无法判断属于哪个控制 token，也无法判断应该送到上方、下方还是写回 `PE.reg`。

## 4. 可以直接迁移和不能直接照搬的部分

### 4.1 可以直接迁移

| 书中结构 | SA 中的迁移方式 |
|---|---|
| `_from_/_to_` | 继续沿用 `current/next`，进一步把算术 issue 和 commit 分开 |
| `valid` | 给所有多拍 token、结果和延迟控制添加有效位 |
| `wait` | 在存在状态冲突或阶段切换时阻止接收新 token |
| 安全区 | 锁存本次 PE/CMP 操作所需的全部输入 |
| 阶段独立 | 将输入捕获、算术发射、结果选择、状态提交拆成独立阶段 |
| 反向调用顺序 | 在纯 C++ step 中继续避免不必要的 RAW 依赖 |
| 流水线 trace | 按 token 编号记录 issue、移动、commit 和输出 |

### 4.2 不能直接照搬

处理器中的取指、译码、执行、访存、写回不能直接对应 SA 的五个流水级。SA 的结构是二维数据流网络，不是一条单向指令流水线。

处理器中的以下机制也不能原样搬到 SA：

- 分支预测和错误路径取消；
- 通用寄存器文件旁路网络；
- load-use 冒险检测；
- 处理器流水级按固定顺序单向移动。

SA 中真正需要处理的是：

- 水平控制和水平元素数据的对齐；
- 竖直部分和与对应 MAC token 的对齐；
- PE 内部 `reg` 的读写相关；
- exp2 的一次性写回和 `exp2Done`；
- CMP 控制、CMP 输入和 CMP 输出的对齐；
- 快速透传操作和慢速算术操作之间的顺序。

## 5. 为什么不能直接把顶层改成 II=1

假设只进行如下修改：

```cpp
#pragma HLS PIPELINE II=1
```

而不改数据结构和状态模型，至少会遇到以下问题。

### 5.1 顶层状态存在距离为 1 的相关

事务 `n+1` 在开始时要读取 `current`，而事务 `n` 的 `current = next` 可能要等 MAC 结果返回后才能完成。

如果该状态更新在 16 拍之后才可用，那么 HLS 不能安全地让下一笔事务在第 1 拍就读取更新后的状态。

### 5.2 `valid` 与算术结果没有显式延迟绑定

当前 `pe_step` 根据当前 `fire` 立即计算输出 `valid`。在现有 `II=16` 模型里，整笔事务等待完成，因此最终输出仍然能够与结果对应。

若多笔事务重叠，必须把以下信息一起延迟到算术结果返回的时刻：

- 是否有效；
- `mac` 和 `acc_ui`；
- 是否写 `reg`；
- 是否为 exp2；
- 输出选择方向；
- 需要传播的控制信号。

### 5.3 快路径和慢路径会发生重排序

`flow_lr`、`flow_ud`、`flow_du` 和简单装载不需要等待 16 拍算术结果，而 MAC/exp2 需要多拍。

如果快 token 直接输出、慢 token 延后输出，后发出的快 token 可能超过先发出的慢 token，从而破坏控制计划所要求的顺序。

### 5.4 PE 内部状态可能被提前覆盖

例如：

1. token A 读取旧 `reg` 并启动 MAC；
2. token B 在 A 完成前执行 `load_reg_li`；
3. token C 又启动 MAC。

如果没有明确的阶段边界、状态快照和提交规则，就无法判断 C 应读取旧值还是新值，也可能使 A 的延迟写回覆盖 B 的装载结果。

因此，降低 II 必须先重构数据和状态依赖，不能只调整 pragma。

## 6. 三种候选架构

### 6.1 方案 A：保持单事务在途

一次只允许一个 SA step 在途，下一笔事务等上一笔完成后再开始。

优点：

- 与当前模型最接近；
- 状态语义简单；
- 不需要 token tag、旁路或重排序；
- xxxxxxxxxx1 1./run_hls.sh srambash

缺点：

- 吞吐率不会明显改善；
- `peMacUnit II=1` 的吞吐能力仍未被利用。

该方案可以作为重构过程中的正确性基线，但不应作为最终的性能目标。

### 6.2 方案 B：所有操作统一延迟

所有 PE 操作，包括透传和装载，都延迟到与 MAC 相同的固定拍数后提交。

优点：

- 所有 token 保序，控制最简单；
- 不需要处理快慢路径重排序；
- 容易用固定长度 `valid` shift register 表达。

缺点：

- 相邻 PE 的数据不再一拍移动一格；
- 现有 ExecutionPlan 的拍数安排必须全面重算；
- 阵列填充和排空延迟显著增大；
- 大量透传操作会无意义地经过长流水线。

该方案结构规整，但会改变 SA 的全局时序，迁移成本较高。

### 6.3 方案 C：按执行阶段分流并在阶段切换时排空

FSA 的 SA 操作天然按阶段组织，例如装载 stationary 数据、MAC 波前、CMP/回流、exp2 和结果排空。不同阶段不会任意乱序混合。

因此可以把操作分成三类：

| 操作类 | 典型控制 | 特点 |
|---|---|---|
| 传输/装载类 | `flow_*`、`load_reg_li`、`load_reg_ui` | 延迟短，可能修改 PE 状态 |
| 稳态 MAC 类 | `mac=true`、`exp2=false` | `reg` 在一个计算阶段内保持不变，可连续发射 |
| 状态更新算术类 | `update_reg`、`exp2=true` | 结果返回后修改状态，存在明显 RAW/WAW 风险 |

每个阶段内部使用最适合该阶段的流水结构；从一种操作类切换到另一种操作类之前，等待当前流水线排空。

优点：

- 不需要支持任意快慢 token 混排；
- MAC 阶段有机会利用 `peMacUnit II=1`；
- 装载和透传不必无条件等待 16 拍；
- 状态相关可以通过阶段边界消除。

缺点：

- 控制器必须知道阶段是否已经排空；
- 阶段边界需要明确的 drain/barrier 状态；
- 竖直部分和的多拍传输仍需要重新对齐。

本文档推荐方案 C。

## 7. 推荐架构概览

推荐架构把 PE 的一次操作拆成以下逻辑阶段：

```text
输入/控制
   |
   v
[Capture / Classify]
   |  形成 IssueToken，保存本次所有输入和状态快照
   v
[Issue]
   |  发射到 peMacUnit，或进入短延迟传输路径
   v
[Arithmetic Pipeline]
   |  固定延迟、多 token 在途、目标 II=1
   v
[Align / Select]
   |  对齐 ctrl、valid、方向和转换结果
   v
[Commit]
   |  更新 PEState，产生 r/u/d 输出和 out_ctrl
   v
相邻 PE/CMP 或 SA 输出
```

SA 顶层不应再把“整个 4×4 阵列的所有状态更新”看成一个不可拆分的大事务，而应区分：

- 输入 token 是否已经被接受；
- 算术 token 是否正在流水线中；
- 结果 token 是否已经到达提交阶段；
- 状态写回是否允许发生；
- 相邻方向输出是否已经产生。

## 8. Token 数据结构

下面的数据结构是设计草图，字段名和位宽应在实现时根据综合结果调整。

### 8.1 PE 发射 token

```cpp
struct PEIssueToken{
    bool valid;
    PECtrl ctrl;

    elem_t reg_snapshot;
    ValidData<elem_t> l_input;
    ValidData<acc_t> u_input;
    ValidData<acc_t> d_input;

    bool use_mac_result;
    bool write_reg_from_mac;
    bool write_reg_from_left;
    bool write_reg_from_upper;
};
```

`reg_snapshot` 必须在接受 token 时锁存。不能在算术结果返回时重新读取 `PE.reg`，否则后续装载可能已经改变了它。

### 8.2 PE 结果 token

```cpp
struct PEResultToken{
    bool valid;
    PECtrl ctrl;

    PeMacUnitOutput mac;
    ValidData<elem_t> l_input;
    ValidData<acc_t> u_input;
    ValidData<acc_t> d_input;

    bool write_reg_from_mac;
    bool write_reg_from_left;
    bool write_reg_from_upper;
};
```

结果 token 不仅包含数值结果，还必须包含提交该结果所需的控制信息。`valid=false` 时，commit 阶段不得改变任何状态。

### 8.3 可选调试序号

在普通 C++ 测试中可以给 token 增加单调递增的 `sequence` 字段，用来检查：

- token 是否丢失；
- token 是否重复；
- 快路径是否越过慢路径；
- 每列输出是否保持正确顺序。

该字段可以只存在于 `#ifndef __SYNTHESIS__` 下，避免增加正式硬件位宽。正式硬件中如果阶段严格保序，则不需要通用重排序标签。

## 9. PE 状态拆分

当前 `PEState` 只有 `reg` 和 `exp2Done`。迁移时应把“架构状态”和“流水线控制状态”分开。

### 9.1 架构状态

架构状态是算法真正可见的状态：

- `reg`；
- `exp2Done`。

它们只能在 commit 阶段更新。

### 9.2 流水线状态

流水线状态只服务于多拍执行，例如：

- issue 安全区是否占用；
- 算术流水线中是否还有有效 token；
- 是否存在尚未提交的 `reg` 写；
- 当前操作类；
- 阶段排空计数或各级 `valid`。

示意结构如下：

```cpp
struct PEPipelineState{
    bool issue_full;
    PEIssueToken issue;

    bool pending_reg_write;
    bool pending_exp2;

    ValidData<PECommitInfo> ctrl_pipe[PE_ARITH_LATENCY];
};
```

如果 HLS 已经为 `peMacUnit` 自动生成算术数据寄存器，不应在 C++ 中重复保存每级浮点中间结果；C++ 侧主要需要保存与结果对齐的控制 token。

## 10. MAC 稳态阶段

MAC 稳态阶段是最有希望把吞吐率提高到 II=1 的部分。

### 10.1 可利用的条件

在典型矩阵乘阶段：

- `PE.reg` 中的 stationary 数据已经提前装载完成；
- MAC 阶段内 `PE.reg` 不再更新；
- 新的流动操作数可以连续进入；
- `peMacUnit` 已综合为 II=1；
- 4×4 阵列已有 16 个独立 `peMacUnit`。

这消除了最危险的 `reg` 写后读相关，因此可以让连续 MAC token 共享同一个只读 `reg` 快照。

### 10.2 竖直部分和的对齐

困难在于部分和不是独立结果。一个 PE 的 MAC 输出会成为同一列相邻 PE 的累加输入。

如果一个 MAC 跨 16 个物理拍，那么不能再假设部分和“一拍移动到相邻 PE”。有两种实现方向：

1. 固定延迟连接：相邻 PE 的控制和水平数据按已知延迟重新错拍。
2. token 汇合：PE 只有在水平 token、控制 token 和竖直部分和 token 都有效时才发射 MAC。

第二种更稳健，但需要小型输入缓冲或 ready/valid。推荐先为每个 PE 提供深度 1 的安全区：

- 水平 token 到达后可先保存；
- 竖直部分和到达后，如果另一侧也已准备好则发射；
- 安全区已满时对对应上游施加 wait；
- 发射后清空安全区。

这相当于把二维 SA 表示成 token 数据流网络，而不是依赖全阵列统一的隐含拍号。

### 10.3 MAC 阶段完成条件

MAC 阶段不能只根据“最后一个输入已经送出”结束，还必须确认：

- 所有水平 token 已离开输入端；
- 所有竖直部分和已穿过最后一个 PE；
- 所有 `peMacUnit` 控制 valid pipe 已清空；
- 所有列的最后结果已经提交到输出 pipe。

只有这些条件同时满足，才能切换到 CMP、回流或装载阶段。

## 11. 传输和装载阶段

传输/装载类操作延迟短，但会改变 pipe 或 `PE.reg`，不能与尚未完成的 MAC 结果任意混合。

推荐规则：

1. 进入装载阶段前，先排空 MAC/exp2 流水线。
2. `load_reg_li` 和 `load_reg_ui` 在 commit 时更新 `reg`。
3. 装载完成后，等待控制和数据传播到目标 PE，再开放 MAC 阶段。
4. `flow_lr/flow_ud/flow_du` 的数据与控制必须作为同一个传输 token 移动。
5. 若短路径输出端可能被慢路径占用，则短路径必须等待，不能越过已有慢 token。

由于推荐采用阶段隔离，第一版实现可以避免构建通用的快慢路径重排序缓冲。

## 12. exp2 和状态更新阶段

exp2 比普通 MAC 更复杂，因为它会使用 `exp2Done` 防止一次结果重复写回。

### 12.1 基本规则

- 接受 exp2 token 时保存输入和控制。
- 结果 token 返回且 `valid=true` 时，才能更新 `reg` 和 `exp2Done`。
- 同一个 PE 存在未完成 exp2 时，不允许另一个会修改 `reg/exp2Done` 的 token进入。
- 非 exp2 控制清除 `exp2Done` 的动作也必须在正确的 commit 顺序中发生。

### 12.2 第一版建议

第一版不追求 exp2 阶段 II=1。可以对同一个 PE 设置：

```text
pending_exp2 = true -> 拒绝新的状态更新 token
```

等待结果提交后再清除 `pending_exp2`。这样资源和控制都更简单。

只有在普通 MAC 阶段稳定达到目标后，才评估是否需要让多个 exp2 token 同时在途。

## 13. CMP 的处理

CMP 也有 `oldMax`、`newMax` 和 `exp2_counter` 等跨拍状态，因此必须遵循与 PE 相同的提交原则。

迁移时需要测量或从综合报告中确认：

- `accCmp` 的实际 Latency 和 II；
- 截距产生路径的延迟；
- CMP 输出到顶部 PE 的延迟；
- CMP 状态更新与输出结果是否在同一 commit 点完成。

CMP token 至少应包含：

- `valid`；
- `CmpControl`；
- `d_input`；
- 状态更新选择；
- 结果输出选择。

不能只延迟 `d_output.bits` 而不延迟 `out_ctrl`，否则 CMP 控制波前会与回送数据错位。

## 14. wait、ready 和安全区规则

### 14.1 接受条件

一个阶段只有在以下条件都成立时才能接受新 token：

```text
input.valid && stage.ready
```

对于深度 1 安全区：

```text
stage.ready = !safe.full || current_token_will_leave
```

### 14.2 等待期间

当 `wait=true` 时：

- 安全区内容保持不变；
- 输出 token 保持不变，直到下游接受；
- 不重复发射算术操作；
- 不重复更新状态；
- 不把当前输入覆盖到安全区。

### 14.3 空输入

当没有有效输入时：

- 可以向下游发送 `valid=false` 空泡；
- 不应使用无效 token 的 `bits` 决定状态；
- 不应调用具有状态副作用的 commit；
- 流水线中的已有有效 token 仍继续前进。

## 15. 阶段排空和模式切换

推荐为 SA 增加内部操作类和 drain 状态，但第一阶段不改变外部顶层端口。

示意状态机：

```text
IDLE
  |
  +-- load/flow token --> TRANSPORT
  |
  +-- mac token -------> MAC_STREAM
  |
  +-- exp2/update -----> STATEFUL_ARITH

TRANSPORT / MAC_STREAM / STATEFUL_ARITH
  |
  +-- 同类 token：继续接收
  |
  +-- 不同类 token：停止接收 -> DRAIN

DRAIN
  |
  +-- 所有 valid/pending 清空 -> 切换到新操作类
```

第一版可以要求外部控制计划在模式切换前显式插入空 token，内部根据流水线是否为空判断何时完成切换。后续系统顶层可以增加单独的 busy/ready 映射，但不能在没有评估接口影响时擅自更改顶层协议。

## 16. 顶层接口策略

### 16.1 第一阶段保留 `ap_ctrl_hs`

保留当前协议的优点是：

- 不改变现有 HLS testbench 调用方式；
- 不立即影响 Vivado IP 集成；
- 可以通过综合报告中的 `ap_ready`、Latency 和 Interval 观察重构效果。

若顶层最终达到 II=1，`ap_ready` 应能够在流水线稳定时每拍接受一笔新事务。输出仍需结合 `ap_done` 和 `output.acc_out[].valid` 解释。

### 16.2 暂不切换为 free-running 或 stream 顶层

`ap_ctrl_none`、`ap_ctrl_chain` 或 `hls::stream` 可能更适合最终的数据流系统，但会改变：

- 软件/硬件启动方式；
- 输入保持要求；
- 输出回压能力；
- Vivado 包装和上板控制逻辑；
- 系统顶层与 SA 的连接方式。

因此协议变化应在 SA 内部 token 模型验证完成后单独决策。

## 17. HLS pragma 使用原则

### 17.1 保留空间展开

4×4 PE 和相关数组仍应保持完整展开和分区，并从综合报告确认存在 16 个独立 MAC 实例。

### 17.2 算术流水线

继续保留并验证：

```cpp
#pragma HLS PIPELINE II=1
```

但验收时必须区分：

- 算术函数 II；
- PE 阶段 II；
- SA 顶层 II；
- C/RTL 协同仿真的事务 Interval。

### 17.3 不使用不安全的 dependence 断言

在没有逐项证明状态独立之前，不应使用 `DEPENDENCE false` 强行消除 `PE.reg`、`exp2Done` 或 pipe 状态的相关。

错误的 dependence 断言可能让综合通过，但生成与 C 模型不一致的硬件。

### 17.4 不通过放宽时钟掩盖问题

继续保持：

- `create_clock -period 10`；
- 当前 clock uncertainty；
- 当前目标器件。

如果时序失败，应检查长组合选择器、回压扇出、跨阵列 valid 网络和状态旁路，而不是放宽目标时钟。

## 18. 推荐实施阶段

### 阶段 0：重建可信基线

目标：确认当前源码与报告一致。

工作：

1. 重新运行 `./run_hls.sh sa`。
2. 保存 C 仿真、C 综合、协同仿真和 IP 导出结果。
3. 记录顶层及子模块 Latency、II、资源、估算周期和实例数。
4. 保留当前逐事务输出作为后续金标准。

注意：现有 SA 报告生成于 2026-08-13，而当前相关源码时间晚于该报告，因此当前报告只能作为历史基线，构建结果可能过期。

### 阶段 1：只重构结构，不改变吞吐率

目标：建立 `IssueToken/ResultToken/Commit`，但仍只允许一笔状态相关事务在途。

工作：

1. 把 PE 输入捕获与状态提交分开。
2. 让控制 token 与算术结果显式绑定。
3. 增加 `valid`、`pending` 和安全区。
4. 保持顶层 II=16，先验证功能完全一致。

验收：

- 原有 C++ 测试通过；
- HLS 顶层 testbench 通过；
- C/RTL 协同仿真通过；
- 每个 token 只提交一次；
- 复位和空泡行为不变。

### 阶段 2：加入阶段分类和排空屏障

目标：禁止不同操作类无约束混排。

工作：

1. 区分 TRANSPORT、MAC_STREAM、STATEFUL_ARITH。
2. 增加 drain 判断。
3. 阶段切换前停止接受新 token。
4. 排空后再允许新操作类进入。

验收：

- 模式切换时无 token 丢失或越序；
- 最后一个 MAC 结果不会被后续 load 覆盖；
- 第一个新阶段 token 不会读取旧阶段的未提交状态。

### 阶段 3：开放 MAC 多 token 并发

目标：在 stationary `reg` 保持不变的 MAC 阶段降低 II。

工作：

1. MAC 阶段将 `reg` 视为只读。
2. 给水平输入和竖直部分和增加安全区或小 FIFO。
3. 对齐 MAC 控制和结果。
4. 从 II=16 逐步尝试 II=8、4、2、1，而不是一次跳到 II=1。

每次调整都记录：

- Final II；
- 顶层 Latency；
- Estimated Period；
- DSP、FF、LUT；
- 16 个 `peMacUnit` 是否仍独立；
- 协同仿真 Interval；
- 是否出现新的 HLS 调度警告。

### 阶段 4：处理 exp2 和状态更新并发

目标：在普通 MAC 稳定后，再优化 exp2/update。

第一版只需确保正确的 wait 和 commit，不要求 II=1。只有性能数据证明 exp2 是系统瓶颈时，才增加多 exp2 token 支持。

### 阶段 5：系统顶层集成

目标：让控制器根据 SA ready/busy/drain 发送控制和数据。

此阶段才决定是否：

- 保留独立 `ap_ctrl_hs` IP；
- 使用 `ap_ctrl_chain`；
- 改成内部 `hls::stream`；
- 将 SA 与 Delayer/Accumulator 放入同一个 DATAFLOW 顶层。

## 19. 测试方案

### 19.1 单 PE token 测试

至少覆盖：

- 连续空泡；
- 连续 MAC token；
- MAC 后 load；
- load 后 MAC；
- `update_reg`；
- exp2 第一次完成写回；
- exp2 结果不能重复写回；
- 上游 wait 时输入保持；
- 下游等待时输出保持；
- reset 时清空所有 pending/valid。

### 19.2 SA 数据流测试

正式 4×4 测试应继续覆盖完整矩阵乘和 rowmax，并增加：

- 每拍记录每个 PE 接受和提交的 token；
- 检查水平控制与水平数据序号一致；
- 检查竖直部分和序号不跳变；
- 检查每列输出顺序；
- 在输入中主动插入空泡；
- 在阶段边界插入不同长度的排空周期；
- 对比重构前后的最终数值和 valid 序列。

### 19.3 金标准

金标准应继续使用独立的软件矩阵乘计算，不应复制新的 token 流水线实现。

同时保留一个非流水的 step 参考模型：它按逻辑 token 顺序计算结果，用于验证深流水实现只改变物理延迟，不改变 token 顺序和数值语义。

### 19.4 C/RTL 协同仿真

协同仿真至少检查：

- Latency 和 Interval 与综合报告一致；
- 连续输入事务不会丢失；
- 输出事务数量与有效输入数量一致；
- reset 后流水线中不存在旧 token；
- 模式切换和排空在 RTL 中与 C 模型一致。

## 20. 综合和实现验收标准

### 20.1 功能标准

- C 仿真通过。
- C/RTL 协同仿真通过。
- 原有 4×4 完整测试结果不变。
- valid、复位、空泡、状态保持和阶段排空测试通过。

### 20.2 结构标准

- 顶层仍存在 16 个独立 `peMacUnit`，除非后续明确选择资源共享方案。
- 控制 token 的延迟与算术结果一致。
- 任何状态更新都只发生在有效 commit。
- reset 能清除所有流水线 valid 和 pending 状态。

### 20.3 性能标准

第一目标不是立即达到 II=1，而是每一步都比可信基线更好且保持正确。

建议里程碑：

| 里程碑 | 顶层目标 II | 说明 |
|---|---:|---|
| M0 | 16 | token 化后保持现有吞吐，验证结构正确 |
| M1 | 8 | 验证至少两笔事务可以安全重叠 |
| M2 | 4 | 检查状态依赖和部分和对齐是否成为瓶颈 |
| M3 | 2 | 评估回压、FIFO 和布线成本 |
| M4 | 1 | 仅在时序、资源和系统接口同时可接受时作为最终目标 |

### 20.4 时序标准

- HLS Estimated Period 必须小于 7.30 ns 有效预算。
- 不能出现未解释的 `HLS 200-871` 等时序违例。
- 最终仍需 Vivado 综合、布局布线并检查 WNS。
- HLS 估算通过不能替代实现时序通过。

### 20.5 资源标准

当前资源已经较高，新增 token 控制、FIFO 和 valid pipe 预计会增加 FF/LUT。每个里程碑都需要报告资源增量。

如果 II 改善很小而 FF/LUT 或布线压力显著增加，应停止继续降低 II，先从系统级吞吐评估是否值得。

## 21. 主要风险

### 21.1 把物理拍和逻辑拍混为一谈

深流水后，一个 token 可能经过很多物理拍才离开 PE，但流水线填满后可以每拍处理新的 token。文档、测试和控制器必须明确区分：

- 单个 token 的 Latency；
- 相邻 token 的 II；
- 阵列级算法中的逻辑顺序。

### 21.2 回压形成长组合路径

如果 wait 从阵列末端组合传播到所有上游 PE，会形成高扇出长路径。实现时应优先考虑：

- 局部 ready；
- 小 FIFO；
- 寄存后的回压；
- 阶段排空而不是全局逐 token 任意停顿。

### 21.3 快慢路径越序

只要允许传输 token 和 MAC token 混排，就必须提供保序结构。第一版通过阶段隔离避免该问题，不应过早实现通用重排序缓冲。

### 21.4 状态写回冲突

`load_reg_li`、`load_reg_ui`、`update_reg` 和 exp2 都可能写 `PE.reg`。必须定义唯一优先级，并保证该优先级作用于 commit 顺序，而不是结果返回的偶然物理顺序。

### 21.5 报告与源码不一致

每次修改 pragma、token 深度或接口后都必须重新综合。不能继续引用旧报告中的 II、Latency 和资源数据作为当前结论。

## 22. 推荐的第一批实际改动

若开始编码，建议第一批只完成以下内容：

1. 新增 PE issue/result token 类型。
2. 将 `pe_step` 拆为输入捕获、算术计算和 commit 三个逻辑函数。
3. 给所有状态写回增加统一的 `valid` 门控。
4. 增加仿真用 token sequence trace。
5. 仍保持顶层 `II=16`。
6. 运行普通 C++ 测试、SA HLS C 仿真、C 综合和协同仿真。

这一步的目标不是性能提升，而是建立后续降低 II 所需要的显式依赖模型。如果在这一步就出现功能差异，应先修复 token 与状态语义，不应继续尝试更低 II。

## 23. 最终结论

书中的方法可以迁移到当前 SA，但应迁移其“显式时序建模方法”，而不是照搬处理器的流水级名称。

当前 SA 已经拥有：

- `current/next` 状态分离；
- PE/CMP 级间寄存器；
- `ValidData`；
- 4×4 空间展开；
- 内部 II=1 的算术流水单元。

真正缺少的是：

- 多拍算术 token；
- 控制与结果延迟对齐；
- issue 安全区；
- wait/ready 或等价的阶段排空；
- 状态提交点；
- 快慢路径保序规则。

推荐采用“按执行阶段分流、阶段切换时排空”的方案，先把 token 和 commit 语义做正确，再只对 stationary MAC 阶段逐步降低 II。这样既能利用现有 16 个 `peMacUnit` 的 II=1 能力，又能避免一开始就引入通用乱序、复杂旁路和大范围接口修改。
