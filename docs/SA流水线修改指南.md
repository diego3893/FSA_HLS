# SA 流水线修改指南

## 1. 文档用途

本文用于把当前 FSA-HLS 的 Systolic Array（SA）优化工作交接给另一个 Codex。

目标不是简单地修改一个 `#pragma`，而是在保持功能、器件、时钟和数据格式不变的前提下，让 SA 能够利用 PE 内部算术单元的流水线，尽量达到“每拍接收一个新 token”的效果。

本文只描述修改方案。开始修改前，接手者必须先阅读：

- `AGENTS.md`
- `docs/SystolicArray综合报告.md`
- `include/fsa/state.hpp`
- `include/fsa/control.hpp`
- `include/fsa/pe.hpp`
- `include/fsa/cmp.hpp`
- `include/fsa/systolic_array.hpp`
- `src/core/arithmetic.cpp`
- `src/core/pe.cpp`
- `src/core/cmp.cpp`
- `src/core/systolic_array.cpp`
- `src/hls/systolic_array_top.cpp`
- `tests/hls/test_systolic_array_top.cpp`
- `tests/hls/test_delayer_sa.cpp`

功能和时序语义最终以 `../FSA-main` 中对应的 Chisel 实现为准。

---

## 2. 不允许擅自改变的配置

除非用户明确同意，否则不得通过以下方式让报告变好：

- 不改变 4×4 阵列规模，即保持 `SA_ROWS=4`、`SA_COLS=4`。
- 不改变器件 `xcvu37p_CIV-fsvh2892-2-e`。
- 不放宽 10 ns（100 MHz）目标时钟。
- 不减小已有的 clock uncertainty。
- 不改变 `elem_t`、`acc_t` 的格式和位宽。
- 不删除或绕过 CMP、rowmax、exp2 等已有功能。
- 不使用虚假的 `DEPENDENCE false` 隐藏真实 RAW 相关。
- 不以减少 PE 数量、共享成一个 PE 的方式换取较小资源。
- 不删除现有测试中的检查来制造“通过”。

现有 `systolic_array_top` 应先保留为功能基准。建议新建流水线版本，验证完成后再决定是否替换旧版本。

---

## 3. 当前基线

当前顶层为：

```text
systolic_array_top
  ├─ 4×4 PE，共16个空间并行实例
  ├─ 4个CMP
  ├─ current/next状态
  └─ ap_ctrl_hs事务接口
```

当前综合报告记录的关键结果为：

| 项目 | 当前结果 |
|---|---:|
| SA 顶层 Latency | 17 拍 |
| SA 顶层 II | 16 |
| `peMacUnit` Latency | 16 拍 |
| `peMacUnit` II | 1 |
| `peExp2PWL` Latency | 13 拍 |
| `peExp2PWL` II | 1 |
| HLS Estimated Period | 7.150 ns |
| HLS 100 MHz 时序估算 | 通过，余量约 0.150 ns |
| DSP | 274 |
| FF | 59,266 |
| LUT | 77,960 |

基线已经完成 C 仿真、C 综合、C/RTL 协同仿真和 IP 导出。上述数据来自 2026-08-13 的报告，接手者开始工作前应重新运行一次 `./run_hls.sh sa`，确认源码和构建结果仍然一致。

### 3.1 当前已经做到的事情

- 行、列循环使用 `UNROLL`，16 个 PE 是16套独立硬件，不是一个 PE 循环使用16次。
- `current.mesh` 和各类 pipe 已进行完整 `ARRAY_PARTITION`。
- `peMacUnit` 本身是可流水的，能够做到 II=1。
- 当前 4×4 的 `S=Q×K^T` 和 rowmax 测试通过。

### 3.2 当前没有做到的事情

SA 顶层不能每拍接收一个新事务。当前顶层使用：

```cpp
static fsa::SystolicArrayState current{};
fsa::SystolicArrayState next{};

fsa::systolic_array_step(current, next, io);
current = next;
```

一次调用必须完成整张阵列的状态计算，再把 `next` 写回 `current`。下一次调用又依赖这次写回的状态，因此 HLS 看到的是一条真实的跨事务依赖链。

`peMacUnit` 虽然能每拍接收一个新输入，但当前 SA 每隔16拍才调用下一次有效状态事务，所以内部流水线没有被持续填满。

可以把它类比为：

```text
算术流水线：有16个工位，每拍都能让一件新工件进入
当前SA顶层：必须等第一件工件走完16个工位，才发送第二件
```

因此，“PE 内部 II=1”不等于“SA 顶层 II=1”。

---

## 4. 修改目标

修改应分成三个逐步验收的目标。

### 目标 A：MAC + CMP + rowmax 数据通路达到 II=1

- Q 已经装入 `PEState.reg` 后，连续的 K token 可以每拍进入阵列。
- PE 运算本身允许多拍完成，Latency 不要求等于1。
- 流水线填满后，输出 token 应连续有效，中间没有工具造成的16拍空洞。
- 4×4 点积和 rowmax 必须与原测试一致。

这是第一优先级，也是最容易验证的目标。

### 目标 B：补齐全部数据流动模式

在目标 A 通过后，加入并验证：

- `flow_lr`
- `flow_ud`
- `flow_du`
- `load_reg_li`
- `load_reg_ui`
- `update_reg`
- CMP 控制在列间的传播
- CMP 回送数据与 PE 控制的对齐

### 目标 C：把 exp2 纳入统一流水线

不能只证明 MAC 模式 II=1 就宣称完整 SA II=1。exp2 会写回 `PE.reg`，还使用 `exp2Done`，存在真实状态相关，需要单独设计和验证。

---

## 5. 推荐的新架构

### 5.1 不要直接删除现有实现

建议先新增以下文件：

```text
include/fsa/pe_pipeline.hpp
include/fsa/systolic_array_pipeline.hpp
include/fsa/hls/systolic_array_pipeline_top.hpp

src/core/pe_pipeline.cpp
src/core/systolic_array_pipeline.cpp
src/hls/systolic_array_pipeline_top.cpp

tests/hls/test_systolic_array_pipeline_top.cpp
hls/sa_pipeline/run_hls.tcl
```

原来的 `pe.cpp`、`systolic_array.cpp`、`systolic_array_top.cpp` 和测试先不删除，它们用于逐项比对新旧行为。

完成验证后，才考虑把新实现合并回原文件名。

### 5.2 把状态分成两类

现有 `SystolicArrayState` 把“长期保存的数据”和“正在阵列中流动的数据”混在一起。新实现应明确分为：

1. 持久状态：
   - `PE.reg`
   - `PE.exp2Done`
   - `CMP.oldMax`
   - `CMP.newMax`
   - `CMP.exp2_counter`

2. 在途 token：
   - 数据值
   - `valid`
   - 对应的 `PECtrl` 或 `CmpControl`
   - 数据流向
   - 必要时携带 mode、行列编号或上下文编号

持久状态表示“寄存器里长期保存什么”；token 表示“现在流水线里正在算哪一笔数据”。不能继续用一个 `current/next` 大结构体同时承担这两种职责。

### 5.3 token 必须和 valid、控制信号一起走

推荐定义类似以下结构。字段名可以调整，但含义不能缺失：

```cpp
struct PEInputToken{
    ValidData<PECtrl> ctrl{};
    ValidData<elem_t> l_input{};
    ValidData<acc_t> u_input{};
    ValidData<acc_t> d_input{};
};

struct PEOutputToken{
    ValidData<PECtrl> ctrl{};
    ValidData<elem_t> r_output{};
    ValidData<acc_t> u_output{};
    ValidData<acc_t> d_output{};
};
```

核心原则是：某个数据经过 N 拍算术流水线，它对应的 `valid` 和控制信号也必须延迟 N 拍。

错误做法：

```text
第0拍送入数据A和控制A
第1拍已经把控制A送到下一级
第16拍数据A才出来
```

正确做法：

```text
第0拍送入数据A、validA、控制A
三者一起经过流水线
第16拍同时得到结果A、validA、控制A
```

### 5.4 每条 PE 连线都要成为真正的流水通道

原 Chisel SA 中，相邻 PE 之间是一拍寄存器连接。HLS 浮点算术被实现为多拍 IP 后，不能再假设一次 `pe_step` 调用结束时结果就能在下一“逻辑拍”被相邻 PE 使用。

新结构应表现为：

```text
左侧token ──> PE(0,0)流水线 ──> PE(0,1)流水线 ──> ...
                    │
                    v
               PE(1,0)流水线
```

若一个 PE 的有效 Latency 为16拍，那么相邻 PE 收到结果的时间也会相应后移。控制器和 testbench 必须按照 `valid` 接收结果，不能继续只使用旧测试里写死的周期公式。

### 5.5 推荐使用固定的 PE token 延迟

当前 PE 内包含普通透传、MAC、类型转换和 exp2，几条路径的延迟不同。第一版流水线建议统一为固定 token 延迟：

```cpp
constexpr int PE_TOKEN_LATENCY = 16;
```

- MAC 路径按实际算术延迟输出。
- 比16拍短的透传路径补延迟寄存器。
- `valid` 和 `PECtrl`同样延迟16拍。
- 若后续报告显示真实延迟变化，应从统一配置处修改，不能在多个文件散落魔法数字。

固定延迟会增加整体 Latency，但能显著降低控制对齐的复杂度。等功能和 II=1 都通过后，再考虑不同模式使用不同延迟的高级优化。

---

## 6. 顶层形式的选择

### 6.1 推荐：先做批处理验证顶层

为证明流水线确实能连续接收 token，建议第一版新顶层一次接收一组输入，在顶层内部使用按 cycle 推进的循环：

```cpp
for(int cycle=0; cycle<TOTAL_CYCLES; ++cycle){
    #pragma HLS PIPELINE II=1
    // 本拍注入token
    // 推进PE/CMP流水线
    // 按valid收集输出
}
```

这种写法最容易让 HLS 看见“同一条流水线连续处理多笔数据”，C testbench 也能直接检查整个输出序列。

这只是验证内部架构的顶层，不代表最终控制器接口必须采用数组批处理。

### 6.2 最终系统顶层

未来 ExecutionPlan 和 Controller 加入后，推荐由系统顶层中的控制循环每拍向 SA 发送 token，而不是软件每拍发起一次新的 `ap_ctrl_hs` 调用。

最终可以选择：

- `ap_ctrl_hs`：一次事务处理完整的一段 ExecutionPlan。
- `ap_ctrl_none`：SA 作为自由运行模块，每拍观察输入 valid。
- AXI4-Stream：使用 ready/valid 流接口传输 token。

在当前阶段不要贸然改变现有公开接口。先用新增顶层验证架构，再结合 Controller 决定最终协议。

### 6.3 为什么只重连 PE/CMP 不够

即使完全不用 `systolic_array_step`，手动写16次 PE/CMP 连接，如果仍然是：

```cpp
读取单份状态 -> 等多拍运算完成 -> 写回同一份状态
```

HLS 仍然会看到 RAW 依赖，顶层 II 不会自动变成1。

决定 II 的关键不是“是否使用 SA 类或 step 函数”，而是：

- 是否允许多个 token 同时处于不同流水级；
- 每个 token 的数据、valid 和控制是否对齐；
- 是否存在必须等待上一 token 写回后才能开始下一 token 的真实状态相关。

---

## 7. 各文件的具体修改任务

### 7.1 `include/fsa/pe_pipeline.hpp`

负责声明：

- `PEInputToken`
- `PEOutputToken`
- PE 流水线持久状态
- PE 流水函数
- 统一的 token latency 常量或类型

建议函数：

```cpp
void reset_pe_pipeline_state(PEPipelineState& state);

void pe_pipeline_step(
    PEPipelineState& state,
    const PEInputToken& input,
    PEOutputToken& output
);
```

注释必须说明它和 `PE.scala`、现有 `pe_step` 的对应关系，以及哪些字段属于持久状态、哪些属于在途 token。

### 7.2 `src/core/pe_pipeline.cpp`

按以下顺序实现：

1. 先只实现 `load_reg_li`，确认 Q 能装入每个 PE。
2. 实现普通 MAC，暂时不允许同一 token 写回 `reg`。
3. 给 MAC 结果、控制和 valid 加入固定延迟对齐。
4. 实现 `flow_lr`、`flow_ud`、`flow_du`。
5. 实现 `load_reg_ui` 和 `update_reg`，并检查写回相关。
6. 最后实现 exp2 和 `exp2Done`。

不能直接照搬当前 `pe_step` 的“函数返回即得到结果”假设。

### 7.3 `include/fsa/systolic_array_pipeline.hpp`

负责声明：

- 新 SA 输入输出 token。
- 4×4 PE 流水线状态。
- CMP 状态和 CMP 控制传播状态。
- SA 流水函数。

建议把端口继续组织成结构体，不必为了 HLS 把所有字段拆成大量裸参数。

### 7.4 `src/core/systolic_array_pipeline.cpp`

负责：

- 4×4 PE 的空间连接。
- 4个 CMP 的连接。
- 控制和数据的相同延迟传播。
- 底部输出 valid 的产生。

行列循环应继续完整展开：

```cpp
for(int row=0; row<SA_ROWS; ++row){
    #pragma HLS UNROLL
    for(int col=0; col<SA_COLS; ++col){
        #pragma HLS UNROLL
        // 一个(row,col)对应一套PE硬件
    }
}
```

不要在内部再添加一个要求整张阵列完成后才能开始下一次的 `PIPELINE II=16`。

### 7.5 `src/hls/systolic_array_pipeline_top.cpp`

负责：

- HLS 接口 pragma。
- 静态持久状态。
- reset。
- 输入映射。
- 调用流水核心。
- 输出映射。
- 必要的 `ARRAY_PARTITION`。

第一版可以继续使用 `ap_ctrl_hs`，但应通过内部循环或流接口真正连续注入 token。只有顶层综合报告的 Final II=1 才算达到目标。

### 7.6 `tests/hls/test_systolic_array_pipeline_top.cpp`

不能只复制旧测试并修改函数名。新测试至少包含：

1. reset 后所有输出 invalid。
2. Q 正确装入16个 PE。
3. 连续4拍注入4个 K token，中间不等待输出。
4. 使用独立三重循环计算 `Q×K^T` 金标准。
5. 验证所有16个 S 元素。
6. 验证4个 rowmax。
7. 验证流水线填满后输出 valid 连续出现。
8. 在输入中插入一个 invalid 空拍，验证输出在对应位置也出现空拍。
9. reset 能清空尚未流出的 token。
10. 输出判断以 `valid` 和 token 顺序为准，不写死旧 SA 的固定输出周期。

### 7.7 `hls/sa_pipeline/run_hls.tcl`

配置应与 `hls/sa/run_hls.tcl` 保持一致：

- C++14。
- 相同 include 路径。
- 相同器件。
- 10 ns 时钟。
- 执行 C 仿真、综合、C/RTL 协同仿真和 IP 导出。

新顶层必须添加所有实际依赖源文件。不得为通过时序而修改时钟。

---

## 8. CMP 和 rowmax 的处理

CMP 不能被当作无关外围逻辑。AttentionScore 测试要求它持续更新 `newMax`，并在后续把 rowmax 回送给 PE。

需要确认：

- `d_input.valid` 与对应点积结果同时到达 CMP。
- `CmpControlCmd::UPDATE` 与点积结果对齐。
- CMP 列间控制传播不会少覆盖后面的列。
- `PROP_MAX`、`PROP_MAX_DIFF` 和 `RESET` 的输出时序正确。
- causalCounter 的递减仍与 Chisel 一致。

若综合发现 `accCmp` 的状态反馈导致 II>1，不得使用 false dependence 强行消除。应先确认比较器实际 Latency：

- 若比较能在一拍完成，保持每列一套 CMP 状态即可。
- 若比较需要多拍，则 rowmax 是真实递推，需要按 query 交错多个上下文，或接受该模式更大的启动间隔。

---

## 9. exp2 的特殊问题

`peExp2PWL` 的 Latency=13、II=1，含义是：

- 一笔 exp2 结果要13拍后产生；
- 算术流水线本身每拍可以接收一笔新的分段计算。

但是 PE 还有：

```cpp
next.exp2Done = current.exp2Done || macUnit.out_exp2;
next.reg = macUnit.out_elemType;
```

这里存在写回 `reg` 和 `exp2Done` 的真实状态相关。要让 exp2 模式也支持连续 token，需要选择以下方案之一：

1. 推荐的渐进方案：
   - 使用同一套 PE 硬件和同一条统一流水线；
   - MAC token 可以每拍进入；
   - 同一个 PE 的新一组 exp2 操作必须等上一组写回完成；
   - 报告中分别说明 MAC 和 exp2 的有效启动间隔。

2. 多上下文方案：
   - 为流水线中的不同 token 保存独立 `reg/exp2Done` 上下文；
   - token 携带 context id；
   - 结果返回后写回对应上下文；
   - 资源和控制复杂度都会明显增加。

3. 自定义低延迟浮点方案：
   - 手工实现更接近 Chisel `RawFloat_MulAddExp2` 的组合或浅流水硬件；
   - 风险最高，需要完整验证 IEEE 舍入、特殊值和时序。

第一轮修改不要直接选择方案3。先完成 MAC + CMP + rowmax 的 II=1 验证，再处理 exp2。

---

## 10. Pragma 原则

### 应该使用

- 4×4 PE 行列循环：`UNROLL`。
- 同拍并行访问的数组：按访问维度 `ARRAY_PARTITION complete`。
- 真正逐 token 推进的循环：`PIPELINE II=1`。
- 小型纯连线函数：可 `INLINE`。
- 需要保留明确算术流水边界的函数：可 `INLINE off`。

### 不应机械使用

- 不要在所有函数上都加 `PIPELINE`。
- 不要在已经 `UNROLL` 的 PE 网格循环上用 `DATAFLOW` 代替空间展开。
- 只有模块已经改成独立 stream task 时才考虑 `DATAFLOW`。
- 不要对真实的 `reg`、rowmax 或 `exp2Done` 相关添加虚假依赖声明。
- 不要仅凭 pragma 写了 `II=1` 就认为已经生效，必须看综合报告的 Final II。

---

## 11. 推荐实施顺序

严格按以下顺序工作，每一步通过后再进入下一步。

### 第一步：固定基线

1. 运行 `./run_hls.sh sa`。
2. 保存当前 C 仿真、综合和协同仿真结论。
3. 确认旧测试仍输出正确的 S 和 rowmax。
4. 不修改旧顶层。

### 第二步：建立 MAC-only 流水原型

1. 新建 pipeline 文件和顶层。
2. 保留 4×4 全展开。
3. 只支持 Q 装载和普通 MAC。
4. 连续注入 K token。
5. 先证明 Final II=1 和连续 valid 输出。

### 第三步：加入 CMP 和 rowmax

1. 把点积结果及 valid 送入 CMP。
2. 对齐 `CmpControl`。
3. 验证16个 S 和4个 rowmax。

### 第四步：加入三种 flow 和寄存器写回

1. 加入 `flow_lr/ud/du`。
2. 加入 `load_reg_ui/update_reg`。
3. 检查写回是否导致顶层 II 回升。
4. 若 II 回升，区分真实状态相关和工具误判，不要直接加 false dependence。

### 第五步：加入 exp2

1. 明确采用单上下文调度还是多上下文。
2. 对齐13拍 exp2 结果、`out_exp2`、控制和 valid。
3. 验证8个 PWL 分段的命中行为。
4. 验证 `exp2Done` 只允许正确结果写回一次。

### 第六步：与 Delayer 联合

在 SA 单独测试完全通过后，再改写或新增联合测试：

```text
InputDelayer -> Pipeline SA -> OutputDelayer
```

联合测试仍需完成完整 `S=Q×K^T`、rowmax 和输出对齐。

---

## 12. 验收标准

### 12.1 功能验收

- C 仿真通过。
- 4×4 的16个 S 元素全部正确。
- 4个 rowmax 全部正确。
- reset、valid、空拍和状态保持正确。
- MAC、flow、CMP 和 exp2 的控制行为与 Chisel 一致。
- C/RTL 协同仿真通过。

### 12.2 结构验收

- 综合层次中仍有16个独立 PE 算术实例。
- 4个 CMP 没有被错误共享成一个。
- 数组 partition 确实生效。
- 没有因优化而删除必要的 valid/control 流水寄存器。

### 12.3 性能验收

目标 A 的最低要求：

| 项目 | 要求 |
|---|---|
| MAC + CMP + rowmax 顶层 Final II | 1 |
| PE 数量 | 16 |
| 目标时钟 | 10 ns，不放宽 |
| HLS Estimated Period | 不超过有效预算，且无时序违例 |
| 输出行为 | 流水线填满后连续 valid |

Latency 可以大于当前17拍。II=1表示每拍能开始一个新 token，不表示一拍得到结果。

完整功能加入后，如果 exp2 的真实状态相关使统一顶层不能保持 II=1，必须在报告中明确区分：

- MAC 模式 II。
- exp2 模式 II。
- 完整 ExecutionPlan 的实际调度间隔。

不能只报告最好的一个数字。

### 12.4 报告验收

至少记录：

- C 仿真结果。
- C 综合结果。
- C/RTL 协同仿真结果。
- 顶层 Latency 和 Final II。
- `peMacUnit`、`peExp2PWL`、`cvtAtoE` 的 Latency 和 II。
- PE/CMP 实例数量。
- DSP、FF、LUT、BRAM、URAM。
- 目标时钟、Estimated Period 和时序余量。
- IP 是否成功导出。
- 是否进行 Vivado 实现和上板；未进行时必须明确写“未验证”。

---

## 13. 常见错误

### 错误一：只把顶层改成 `PIPELINE II=1`

真实状态依赖没有消失，HLS 仍会把 Final II 提高，或直接报告 II violation。

### 错误二：认为16个 PE 已经并行，所以 SA 自然 II=1

16个 PE 只说明空间硬件数量；II 表示相邻输入事务的启动间隔，两者不是一回事。

### 错误三：只延迟数据，不延迟 valid 和控制

这会使结果属于 token A，但控制已经变成 token B，数值可能偶尔正确，时序语义一定错误。

### 错误四：继续用旧测试的固定周期编号

新 PE 每跳可能增加多拍 Latency。应根据输出 `valid` 和 token 顺序收集结果。

### 错误五：用 `DEPENDENCE false` 强行得到 II=1

如果依赖确实存在，报告可能变好，但硬件会读到错误状态。

### 错误六：C 仿真通过就认定流水线正确

C 函数调用不会自然展示 RTL 中多笔事务重叠的细节。必须同时检查综合调度报告和 C/RTL 协同仿真。

---

## 14. 交接给其他 Codex 的任务描述

可以把下面这段直接作为新对话的任务：

> 阅读 `AGENTS.md` 和 `docs/SA流水线修改指南.md`，并以 `docs/SystolicArray综合报告.md` 记录的当前 SA 为基线。不要修改器件、10 ns 时钟、4×4 阵列规模、数据位宽和现有功能；不要删除旧 SA 实现。先新增 `systolic_array_pipeline_top` 及对应核心、testbench 和 Tcl，完成指南中的“目标 A”：Q 装载后连续注入 K token，使 4×4 MAC + CMP + rowmax 路径的顶层综合 Final II=1，同时保持16个独立 PE、功能仿真正确、C/RTL 协同仿真通过和 HLS 100 MHz 时序估算通过。先不要处理 exp2；完成目标 A 后报告修改文件、测试结果、Latency、II、实例数量、时序和资源，再等待确认。

这段任务有意把第一次修改限制在 MAC + CMP + rowmax。不要一开始同时重构全部模式，否则出现错误时很难判断是 token 对齐、CMP、状态写回还是 exp2 导致。

