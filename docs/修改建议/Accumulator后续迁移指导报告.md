# Accumulator 后续迁移指导报告

## 1. 本报告解决的问题

本文用于说明并指导以下四项后续工作：

1. reciprocal 的功能、时序与测试；
2. Accumulator 的 Vitis HLS 顶层；
3. 带 Accumulator SRAM 一拍读延迟和读改写时序的集成测试；
4. Accumulator 列并行所需的 `UNROLL` 与 `ARRAY_PARTITION`。

在开始写代码前，必须先区分四个概念：

- `Accumulator`：计算模块，负责乘加、exp2、reciprocal，并保存少量工作状态；
- `Accumulator SRAM`：存储模块，长期保存 L 和 O；
- `attentionScale`：由配置决定的常量 `log2(e)/sqrt(dk)`；
- `scale`：Accumulator 内部每列一个的工作寄存器，其含义会随阶段变化。

---

## 2. Accumulator 和 Accumulator SRAM 的区别

### 2.1 Accumulator 是“计算器”

当前 C++ 中的 `AccumulatorState` 只保存：

```cpp
acc_t scale[SA_COLS];
ReciprocalDividerState reciprocal[SA_COLS];
```

其中没有保存完整的 L/O 矩阵。

Accumulator 每拍接收：

```text
sa_in    ：来自脉动阵列的新贡献
sram_in  ：从 Accumulator SRAM 读出的旧 L/O
ctrl_in  ：本拍执行什么命令
```

并产生：

```text
sram_out ：准备写回 Accumulator SRAM 的新 L/O
```

因此 Accumulator 类似计算器：输入旧数据，计算新数据，但不负责长期保存全部数据。

### 2.2 Accumulator SRAM 是“账本”

原 Scala 的 `accRAM` 逻辑上每行保存 `SA_COLS` 个 `acc_t`。当前配置：

```text
SA_ROWS = 4
SA_COLS = 4
ACC_ROWS = 1 + SA_ROWS = 5
```

可以把其逻辑内容理解为：

```cpp
acc_t acc_ram[ACC_ROWS][SA_COLS];
```

容量上对应一行 L 和 `SA_ROWS` 行 O。实际访问行由 matrix 指令中的地址和步长决定，不能在 Accumulator 内部把地址写死。

### 2.3 两者如何合作

一次典型的读改写是：

```text
第 t 拍：   控制器向 accRAM 发读地址 A
第 t+1 拍： accRAM[A] 作为 sram_in 到达 Accumulator
            Accumulator 计算 sram_out
第 t+2 拍： sram_out 写回同一个地址 A
```

为了避免写错位置，读地址 A、`rmw` 标志和数据必须一起延迟并保持对齐。

---

## 3. attentionScale 和 scale 的区别

### 3.1 attentionScale 是配置常量

定义：

```text
attentionScale = log2(e) / sqrt(dk)
```

它同时完成两件事：

1. Softmax attention score 的 `1/sqrt(dk)` 缩放；
2. 把自然指数 `e^x` 的输入转换成 `2^x` 的输入。

因为：

```text
exp(x/sqrt(dk))
= 2^(x * log2(e)/sqrt(dk))
= 2^(x * attentionScale)
```

当前 C++ 的 `attentionScale()` 返回固定值 `0.7213475204`，对应当前 `dk=4` 的配置。它不是运行过程中不断改变的状态。

注意：它不是单独的 `sqrt(dk)`，也不是单独的 `1/sqrt(dk)`，而是 `log2(e)/sqrt(dk)`。

### 3.2 scale 是会变化的工作寄存器

`scale[col]` 的含义随命令变化：

| 阶段 | `scale` 中保存的内容 |
|---|---|
| reset 后 | 0 |
| `EXP_S1` 后 | `(oldMax-newMax) * attentionScale`，即 exp2 的指数输入 |
| `EXP_S2` 后 | `exp((oldMax-newMax)/sqrt(dk))`，即旧 L/O 的缩放系数 |
| `ACC_SA` / `ACC` 期间 | 保持不变，反复用于缩放多行 L/O |
| `SET_SCALE` 后 | 从 SRAM 读出的 L |
| `RECIPROCAL` 后 | `1/L`，用于最终计算 `O/L` |

所以“scale 是指数”只对 `EXP_S1` 后、`EXP_S2` 前的短暂阶段成立。`EXP_S2` 后它已经是普通缩放系数；最终归一化阶段它又会变成 `1/L`。

---

## 4. 当前迁移到什么程度

### 4.1 已完成或已有基础

- `AccumulatorState` 已表示每列的 `scale`；
- `SET_SCALE`、`ACC`、`ACC_SA`、`EXP_S1` 已有核心逻辑；
- `accExp2PWL()` 已使用 8 段 FP32 PWL 表，而不是核心代码直接调用 `std::exp2()`；
- 已有 `test_accumulator.cpp`，覆盖普通命令和 `EXP_S1 -> EXP_S2`；
- 已有 `test_acc_exp2.cpp`，覆盖整数、分段中点、边界和带整数部分的输入；
- 已有临时 `hls/accumulator/run_csim.tcl`。

### 4.2 本轮新增的 reciprocal 实现

- 已实现 FP32 恢复除法，每拍组合产生 2 个商位；
- 24 位有效数加 guard/round 共生成 26 位商，余数产生 sticky 位；
- 13 个 ITER 周期，连同启动和 DONE 共固定 15 拍；
- 支持规格化数、次正规数、正负零、Infinity、NaN 和符号；
- 使用 RNE 规则舍入；
- 每列各自保存一套除法状态，不使用函数内共享 static；
- 一拍请求即可启动，外部输入随后变化不会污染在途操作；
- 已补入普通数和特殊值逐拍测试，并通过 C++14 严格语法检查；
- 独立软件位级模型已与 20 万个随机 FP32 输入的 `1.0f/x` 结果对照一致。

### 4.3 尚未完成

- 没有真正保存 L/O 的 C++ Accumulator SRAM；
- 没有 SRAM 一拍读延迟、地址延迟和 RMW 集成测试；
- 没有 Accumulator 正式 HLS top；
- 没有对 Accumulator 列循环执行 `UNROLL`；
- 没有对每列状态和端口执行必要的 `ARRAY_PARTITION`；
- 没有 Accumulator 综合报告和 C/RTL Co-simulation。

---

## 5. 开工前必须确定的时序协议

目前 `accumulator_step(current, next, io)` 是“一个函数调用表示一个逻辑拍”的模型，但 HLS 浮点除法可能自动产生多拍硬件。

不能同时采用以下两个互相矛盾的假设：

```text
假设 A：调用一次 accumulator_step 就严格经过一个硬件周期；
假设 B：在函数内部写 1.0F/value，HLS 自行使用若干周期完成，但外层仍按一拍提交 next。
```

后续需要在两种接口方向中做选择：

### 5.1 请求—完成事务接口

```text
ap_start -> 模块运行若干拍 -> ap_done
```

优点：容易先完成单模块 HLS 综合；HLS 可以自行安排浮点除法延迟。

缺点：一次函数调用不再等于原 FSA 的一个时钟拍，最终接回逐拍运行的 MatrixEngineController 时还需要适配。

### 5.2 逐拍自由运行接口

```text
每拍输入 ctrl_valid/cmd/data
模块内部保存状态
结果完成时输出 result_valid
```

优点：更接近原 Scala 的 `reciprocal_in_valid/reciprocal_out_valid`。

缺点：必须明确实现或封装多周期除法器，不能只写一个假的 counter。

### 5.3 推荐分阶段方案

第一阶段沿用现有 PE top 的 `ap_ctrl_hs` 风格，先完成 Accumulator 的 standalone C Simulation 和综合探索；同时用独立的 reciprocal probe 获取 `/` 的真实综合 latency。

第二阶段在整机集成前，把 reciprocal 和 Accumulator 改成明确的请求/结果 valid 协议，或明确采用生成的除法 IP。最终控制器必须等待真实完成信号，而不是使用未经综合验证的猜测值。

---

## 6. reciprocal 实现与后续验证指导

### 6.1 当前代码结构

恢复除法器位于 `src/core/accumulator.cpp` 的匿名命名空间中：

```text
IDLE：接收一拍请求，拆解FP32符号、指数和有效数
ITER：13拍恢复除法，每拍产生2位商
DONE：规格化、RNE舍入，把结果写回scale
```

每列状态保存在：

```cpp
ReciprocalDividerState reciprocal[SA_COLS];
```

状态包含 phase、remainder、divisor、quotient、iter_count、结果指数、符号和特殊值信息，不再使用负 latency 和假 busy counter。

### 6.2 已加入的功能和时序测试

`tests/test_accumulator.cpp` 现已覆盖：

```text
1/1   = 1
1/2   = 0.5
1/3   经过RNE得到FP32结果
1/4   = 0.25
1/0   = Inf
1/Inf = 0
负数和负零符号
```

测试同时检查：

1. 只用一拍 valid 启动；
2. 之后改变 `sram_in/sa_in` 不会污染在途除法；
3. 前 14 拍 scale 保持原值；
4. 第 15 拍进入完成路径并自动写回 scale；
5. 四列状态互不串扰。

### 6.3 下一步：在 Vitis 环境验证和综合

恢复除法已经显式展开成整数比较、减法和移位，不再依赖 HLS 浮点 `/`。下一步应运行 Accumulator C Simulation，再建立正式 top 并综合，记录：

```text
Latency
Interval/II
DSP
LUT
FF
关键路径
是否确实形成每拍两商位的恢复除法数据通路
```

逻辑协议固定为 15 拍，但仍要检查 HLS 能否在目标时钟内把一拍中的两次恢复步骤排进同一个周期。如果时序不收敛，就必须降低时钟、改为每拍 1 位，或者增加内部流水，不能只修改报告数字。

### 6.4 正式 top 仍需提供的接口可见性

正式接口建议表达：

```text
request_valid
request_operand
request_ready 或 busy
result_valid
result_value
```

当前结果会在 DONE 阶段自动写回内部 `scale`，但正式 top 仍应根据系统集成需要输出 busy 或 result_valid，避免外部控制器只能依赖固定拍数猜测完成状态。

### 6.5 C/RTL Co-simulation 测试顺序

建立 HLS top 后，应在 C/RTL Co-simulation 中复用当前测试顺序：

1. 用 `SET_SCALE` 写入 `[1, 2, 3, 4]`；
2. 发出一次 reciprocal 请求；
3. 等待期间改变外部输入；
4. 完成前不得提前改写 scale；
5. 第 15 拍结果有效；
6. 最终 scale 为 `[1, 0.5, 1/3, 0.25]`；
7. busy 期间重复请求的行为必须明确定义；
8. 四列结果互不串扰；
9. 检查 zero、Inf、NaN、负数和次正规数。

### 6.6 reciprocal 最终完成标准

- `reciprocalLatency` 固定为 15 且与实现状态数一致；
- Vitis C Simulation 中第 15 拍完成行为通过；
- 正式 top 暴露或正确消费真实 result valid；
- 功能测试与逐拍时序测试通过；
- 0、Inf、NaN、负数和次正规数行为有明确结论；
- C/RTL Co-simulation 通过；
- 综合报告中的资源、关键路径和延迟已记录。

---

## 7. Accumulator HLS top 完成指导

### 7.1 建议新增文件

```text
include/fsa/hls/accumulator_top.hpp
src/hls/accumulator_top.cpp
tests/hls/test_accumulator_top.cpp
hls/accumulator/run_hls.tcl
```

可以参考现有 `pe_top.cpp` 的结构，但不能简单复制后就认为时序已完成。

### 7.2 顶层职责

顶层应负责：

1. 提供 HLS 可识别的稳定端口；
2. 使用 `static AccumulatorState current` 保存跨调用状态；
3. reset 时复位全部状态；
4. 把 top 输入转换成 `AccumulatorIO`；
5. 调用 `accumulator_step(current, next, io)`；
6. 输出 `sram_out` 及必要的 valid/busy；
7. 在本次状态转移结束后执行 `current = next`。

顶层骨架可以是：

```cpp
void accumulator_top(
    const fsa::AccumulatorTopInput& input,
    fsa::AccumulatorTopOutput& output
){
    #pragma HLS INTERFACE ap_ctrl_hs port=return
    #pragma HLS AGGREGATE variable=input compact=bit
    #pragma HLS AGGREGATE variable=output compact=bit

    static fsa::AccumulatorState current{};

    if(input.reset){
        fsa::reset_accumulator_state(current);
        output = fsa::AccumulatorTopOutput{};
        return;
    }

    fsa::AccumulatorIO io{};
    io.ctrl_in = input.ctrl;
    io.sa_in = input.sa_in;
    io.sram_in = input.sram_in;

    fsa::AccumulatorState next{};
    fsa::accumulator_step(current, next, io);

    output.sram_out = io.sram_out;
    current = next;
}
```

这只是第一版 standalone 顶层。reciprocal 若让一次 top 事务运行多拍，必须重新审查“每次调用提交一次 current”的含义。

### 7.3 不建议为测试随意暴露 scale

正式原接口没有 `scale_out`。测试内部 scale 可以通过以下方式间接验证：

```text
SET_SCALE -> 下一次 ACC 检查乘法结果
EXP_S1 -> EXP_S2 -> 下一次 ACC 检查缩放结果
RECIPROCAL -> 下一次 ACC 检查 O/L
```

如果为了调试临时暴露 `scale_debug`，应明确它是 debug 端口，避免误当成最终接口。

### 7.4 顶层测试必须覆盖

- reset 后的行为；
- 两次顶层调用之间 static state 是否保持；
- `valid=false` 是否保持状态；
- `SET_SCALE -> ACC`；
- `EXP_S1 -> EXP_S2 -> ACC_SA`；
- reciprocal 请求、等待和完成；
- 四列不同输入；
- 连续命令是否发生状态串扰。

### 7.5 HLS top 完成标准

- C Simulation 通过；
- C Synthesis 通过；
- 接口报告与预期一致；
- static state 被综合成寄存器，而不是外部存储端口；
- reset 行为在 C Simulation 和 RTL 中一致；
- reciprocal 的实际 top latency 与控制协议一致；
- C/RTL Co-simulation 通过。

---

## 8. 带 SRAM 时序的集成测试指导

### 8.1 为什么必须单独写

当前 `test_accumulator.cpp` 直接执行：

```cpp
io.sram_in[col] = value;
```

这只是假设“正确的 SRAM 数据已经在这一拍到达”。它没有验证：

- 读地址；
- 一拍读延迟；
- 延迟后的写地址；
- `rmw` 写使能；
- 多行 O 连续更新；
- 同地址读写冲突。

### 8.2 建议先写测试专用 SRAM 模型

建议新增：

```text
tests/support/accumulator_sram_model.hpp
tests/test_accumulator_sram_timing.cpp
```

第一版只需要行为正确，不需要马上综合成 BRAM：

```cpp
struct AccumulatorSramModel{
    fsa::acc_t memory[fsa::ACC_ROWS][fsa::SA_COLS]{};

    bool response_valid = false;
    bool response_rmw = false;
    fsa::sram_address_t response_addr = 0;
    fsa::AccVector response_data{};
};
```

这里的 `response_*` 表示上一拍读请求经过 SRAM 延迟后，本拍返回的数据和元数据。

### 8.3 每个测试周期的建议顺序

```text
1. 把上一拍的 response_data 放到 io.sram_in；
2. 把与该数据对齐的 Accumulator 命令和 sa_in 放到 io；
3. 调用 accumulator_step；
4. 如果 response_valid && response_rmw，把 io.sram_out 写回 response_addr；
5. 接收本拍新的读地址，把对应 memory 行锁存为下一拍 response_data；
6. current = next，推进 Accumulator 状态。
```

控制命令也必须与 SRAM 返回延迟对齐。例如：

```text
第 t 拍发 readAccRAM
第 t+1 拍数据到达，同时发 ACC/ACC_SA/SET_SCALE
```

### 8.4 必须覆盖的集成场景

#### 场景 A：L 的在线更新

```text
alpha = exp((oldMax-newMax)/sqrt(dk))
L_new = alpha * L_old + L_block
```

检查：

- 读的是 L 地址；
- 写回的是同一个 L 地址；
- 未访问的 O 行保持不变。

#### 场景 B：O 的多行更新

连续读出每一行 `O_old[row]`：

```text
O_new[row] = alpha * O_old[row] + O_block[row]
```

检查整个过程中 `scale=alpha` 保持不变，并验证地址按 stride 前进。

#### 场景 C：最终归一化

```text
读 L
SET_SCALE
RECIPROCAL
逐行读 O
ACC: O_norm = (1/L) * O
```

#### 场景 D：冲突和边界

- `rmw=false` 时不得写回；
- `valid=false` 时不得读写；
- 最后一个合法地址；
- 同拍读写相同地址时，明确采用 read-first、write-first，或直接断言该情况不允许；
- 无关行不能被修改。

### 8.5 集成测试完成标准

- 测试中确实存在 `memory[ACC_ROWS][SA_COLS]`，不再只手填单拍 `sram_in`；
- 读数据明确晚一拍返回；
- 地址、数据、`rmw` 同步延迟；
- L 更新、O 更新、最终 O/L 流程均通过；
- 每拍 trace 能打印 cycle、cmd、read/write addr、scale、sram_in、sa_in、sram_out；
- 测试失败时可以定位是哪一拍错位。

---

## 9. UNROLL 和 ARRAY_PARTITION 完成指导

### 9.1 两个 pragma 分别解决什么

`UNROLL` 复制循环体，产生多份并行计算硬件：

```cpp
for(int col=0; col<SA_COLS; ++col){
    #pragma HLS UNROLL
    ...
}
```

目标是让 4 列在同一时刻计算，而不是用一套计算单元轮流处理 4 列。

`ARRAY_PARTITION complete` 把数组拆成独立元素或独立寄存器，使展开后的 4 份计算逻辑能同时读写：

```cpp
#pragma HLS ARRAY_PARTITION variable=current.scale complete dim=1
#pragma HLS ARRAY_PARTITION variable=next.scale complete dim=1
```

具体 pragma 放置位置和结构字段语法必须以 Vitis HLS 报告验证，不能只看 C++ 编译通过。

### 9.2 为什么两个通常要一起使用

只加 `UNROLL`：

```text
有 4 份计算逻辑，但数组可能只有有限读端口，导致并行失败或仲裁。
```

只加 `ARRAY_PARTITION`：

```text
数据可以并行访问，但循环体仍可能只有一份计算硬件。
```

因此 Accumulator 的列维度通常需要同时：

```text
列循环 complete UNROLL
列状态 complete ARRAY_PARTITION
列输入输出 complete ARRAY_PARTITION
```

### 9.3 建议 partition 的对象

- `scale[SA_COLS]`；
- `reciprocal[SA_COLS]` 中每列的恢复除法状态；
- top 边界上的 `sa_in[SA_COLS]`；
- top 边界上的 `sram_in[SA_COLS]`；
- top 边界上的 `sram_out[SA_COLS]`。

### 9.4 不应 complete partition 的对象

不要把完整的：

```cpp
acc_ram[ACC_ROWS][SA_COLS]
```

在所有维度上 complete partition，否则整个 SRAM 可能被拆成大量寄存器，失去 BRAM/URAM 的意义。

对于真正的 Accumulator SRAM，应保持“行深度映射到 BRAM/URAM，列宽度或 bank 按需要并行”的结构。SRAM 的 banking 是单独的存储架构问题，不能用无脑 complete partition 代替。

### 9.5 资源代价

原 Scala 是每列一个 `FPAccUnit`。如果 C++ 对 4 列完全展开，理论上可能得到：

```text
4 路 FMA
4 路 exp2 数据通路
最多 4 路 reciprocal 数据通路
```

这会增加 DSP/LUT/FF。不能只追求 latency，必须看资源是否符合目标 FPGA。

如果 4 路浮点除法器资源过大，可以考虑共享 reciprocal，但这会改变原有列并行 latency，控制计划也必须随之修改，不能只改 pragma。

### 9.6 推荐实验方法

至少综合三版并记录报告：

| 版本 | UNROLL | PARTITION | 目的 |
|---|---:|---:|---|
| baseline | 否 | 否 | 获取当前基线 |
| unroll-only | 是 | 否 | 观察数组端口是否阻塞并行 |
| full lane parallel | 是 | 是 | 检查最终列并行 latency 和资源 |

对比：

```text
Latency
II
DSP
LUT
FF
BRAM/URAM
是否出现循环未完全展开警告
是否出现存储端口冲突
```

### 9.7 pragma 完成标准

- 综合报告确认列循环已经完全展开；
- 每列状态没有被推断成阻塞并行的单端口 RAM；
- 没有数组端口冲突警告；
- latency/II 满足控制计划；
- 资源开销被记录并可接受；
- 加 pragma 前后的 C Simulation 结果完全一致；
- C/RTL Co-simulation 通过。

---

## 10. 推荐实施顺序

### 阶段 0：固定接口与时序约定

- 明确 standalone top 暂用 `ap_ctrl_hs`；
- 明确最终系统需要 request/result valid；
- 定义“reciprocal latency 从哪一拍数到哪一拍”；
- 不再保留可参与运算的 `-1` latency。

### 阶段 1：reciprocal 数值与综合探索

- 恢复除法核心和 `test_accumulator.cpp` 逐拍测试已经完成；
- 在服务器运行现有 Accumulator C Simulation；
- 建立 Accumulator top 后综合恢复除法；
- 记录 15 拍协议对应的 latency、II、resource 和关键路径；
- 如果每拍两商位无法满足时钟，再评估每拍一位或增加流水。

### 阶段 2：带 SRAM 时序的 C++ 集成测试

- 先用普通命令和 EXP 路径建立 SRAM 一拍延迟模型；
- 测 L 更新；
- 测连续 O 更新；
- reciprocal 完成后补最终 O/L 流程。

这一阶段可以继续调用 `accumulator_step`，不必等待正式 HLS top。

### 阶段 3：Accumulator HLS top

- 参考 `pe_top` 新建 top；
- 验证 static state；
- 建立 top C Simulation；
- 完成首次 baseline synthesis。

### 阶段 4：加入 UNROLL/PARTITION

- 先保存 baseline 报告；
- 加列循环 UNROLL；
- 加 lane 状态和端口 PARTITION；
- 重新综合并比较资源、latency、II。

### 阶段 5：RTL 验证

- C/RTL Co-simulation；
- 对比每条命令；
- 对比跨调用 state；
- 对比 reciprocal 完成拍；
- 对比 SRAM 集成测试中的关键 trace。

---

## 11. 可以进入哪一级环境测试

| 阶段 | 当前状态 | 进入条件 |
|---|---|---|
| Accumulator 普通命令 C Simulation | 可以尝试 | 服务器有 Vitis HLS |
| exp2 专项 C Simulation | 测试文件已有 | 建立独立 test target 或脚本 |
| reciprocal C Simulation | 代码已准备，可进 Vitis 验证 | 运行现有测试并核对第15拍 |
| Accumulator baseline synthesis | 缺正式 top | 新建 top 后进行 |
| pragma 后综合 | 尚不可 | 先有 baseline 报告 |
| C/RTL Co-simulation | 尚不可 | top、reciprocal、综合全部稳定 |
| L/O 系统级验证 | 尚不可 | 加 SRAM 时序模型与控制对齐 |

---

## 12. 最终验收清单

- [ ] 能清楚区分 Accumulator 与 Accumulator SRAM；
- [ ] `attentionScale` 与 `scale` 的含义没有混用；
- [ ] reciprocal 有真实完成协议；
- [ ] reciprocal latency 来自实现或综合报告；
- [ ] Accumulator top 能复位并跨调用保存状态；
- [ ] SRAM 模型具有一拍读延迟；
- [ ] 读地址、写地址和 `rmw` 对齐；
- [ ] L 在线更新通过；
- [ ] 多行 O 在线更新通过；
- [ ] 最终 O/L 归一化通过；
- [ ] Accumulator 列循环完全展开；
- [ ] lane 状态和端口完成 partition；
- [ ] Accumulator SRAM 没有被错误拆成全部寄存器；
- [ ] 综合报告记录 latency、II、DSP、LUT、FF、BRAM/URAM；
- [ ] C/RTL Co-simulation 通过。
