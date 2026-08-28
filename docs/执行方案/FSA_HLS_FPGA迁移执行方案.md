# FSA 使用 HLS 迁移到 FPGA：本科生文件规划大纲

> 适用对象：学过计算机组成原理、C/C++ 和基础数字电路，了解 FlashAttention 基本公式，但不熟悉 Scala/Chisel、大型 FPGA 工程和 HLS。
>
> 当前工作范围：只进行项目结构设计、接口设计和静态检查，不安排仿真、综合和上板。

---

## 0. 这份文档解决什么问题

这份文档不要求你立即写出完整的 FSA，而是回答以下问题：

1. HLS 工程中应该建立哪些文件；
2. 每个文件负责原 FSA 的哪一部分；
3. 每个文件内部准备哪些结构体和函数；
4. 文件之间怎样调用；
5. 哪些文件先写，哪些文件后写；
6. 当前没有测试环境时，可以进行哪些静态检查。

整个迁移仍分成两个阶段：

```text
阶段一：迁移核心计算部分
PE、CMP、Delayer、Systolic Array、Accumulator、片上存储和控制器

阶段二：迁移外围部分
DMA、指令解码、信号量、Fence 和系统顶层接口
```

当前应专注于阶段一的文件框架和接口设计。阶段一的文件关系稳定后，再建立阶段二文件。

### 0.1 全文统一命名规则

为了方便逐行对照 Chisel，本文从现在开始遵守以下规则：

1. **模块、Bundle、命令类型名称尽量照抄 Chisel**，例如 `PECtrl`、`CmpControl`、`AccumulatorControl`、`MatrixInstruction`；
2. **端口名称照抄原模块 `io` 字段**，例如 PE 使用 `u_input`、`d_input`、`l_input`、`r_output`，SA 使用 `cmp_ctrl`、`pe_ctrl`、`pe_data`、`acc_out`；
3. **寄存器状态名称照抄 Chisel 中的 `Reg/RegInit/Counter` 变量**，例如 `exp2Done`、`oldMax`、`newMax`、`computeTimer`、`accumTimer`；
4. **指令字段保持原来的大小写和嵌套层次**，例如 `instruction.header.func`、`instruction.spad.revInput`、`instruction.acc.zero`；
5. `elem_t`、`acc_t`、`ValidData<T>`、`current/next` 和 `xxx_step()` 是 HLS/C++ 为表达数据类型与逐周期状态转移新增的名称，Chisel 中没有同名对象；
6. Chisel 本来没有名字、但 HLS 必须显式保存的流水寄存器，使用“原端口名 + `_pipe`”，例如 `r_output_pipe`；
7. 不再使用 `left/from_pe/column_output` 等只描述含义、却无法直接搜索 Chisel 的别名。通俗含义写在注释中，不写进变量名。

常用名称对照如下：

| Chisel 原名 | 文档/HLS 名称 | 说明 |
|---|---|---|
| `SA_ROWS`、`SA_COLS` | 保持不变 | 阵列行数、列数 |
| `SPAD_ROWS`、`ACC_ROWS` | 保持不变 | 两类片上 SRAM 深度 |
| `elemType`、`accType` | `elem_t`、`acc_t` | HLS 需要可直接声明变量的类型别名 |
| `Valid(T)` | `ValidData<T>` | `valid + bits`，没有 `ready` |
| `CmpControlCmd` | 保持不变 | 不再简写成 `CmpCommand` |
| `AccumulatorCmd` | 保持不变 | 不再简写成 `AccCommand` |
| `MatrixEngineController` | 保持完整名称 | 不再在结构体名中缩写为 `MatrixController` |

---

## 1. 先理解原项目的模块关系

原项目的核心数据流是：

```text
Matrix 指令
    ↓
MatrixEngineController：决定各模块当前做什么
    ↓
Scratchpad → InputDelayer → Systolic Array → OutputDelayer → Accumulator
                                      ↑                         ↕
                                    CMP                  Accumulator RAM
```

可以把各模块理解为：

| 原模块 | 通俗理解 | HLS 中的任务 |
|---|---|---|
| PE | 保存一个数并执行乘加的小计算格 | 写成带状态的 `pe_step()` |
| CMP | 每列顶部的比较和 softmax 辅助单元 | 写成带 `oldMax/newMax` 的 `cmp_step()` |
| Delayer | 让各路数据延迟不同拍数的排队通道 | 写成移位寄存器状态更新函数 |
| Systolic Array | PE、CMP 和级间寄存器组成的二维网络 | 连接多个 PE/CMP，并管理上下左右数据流 |
| Accumulator | 保存并更新 softmax 的 L 和输出 O | 实现六类累加命令和 scale 状态 |
| Banked SRAM | 片上 Scratchpad 和 Accumulator RAM | 表示地址、整行/窄口和一拍读响应 |
| ExecutionPlan | 每类 Matrix 指令的控制步骤 | 提供静态控制表或查表函数 |
| MatrixEngineController | 按指令和计数器发出控制信号 | 读取控制表并更新地址、状态和完成标志 |
| DMA | 在外部内存与片上存储之间搬数据 | 在阶段二实现 `LoadQueue/StoreQueue` |

阅读原源码时按以下顺序：

1. [`src/main/scala/fsa/sa/PE.scala`](../../FSA-main/src/main/scala/fsa/sa/PE.scala)
2. [`src/main/scala/fsa/sa/CMP.scala`](../../FSA-main/src/main/scala/fsa/sa/CMP.scala)
3. [`src/main/scala/fsa/InputDelayer.scala`](../../FSA-main/src/main/scala/fsa/InputDelayer.scala)
4. [`src/main/scala/fsa/sa/SystolicArray.scala`](../../FSA-main/src/main/scala/fsa/sa/SystolicArray.scala)
5. [`src/main/scala/fsa/Accumulator.scala`](../../FSA-main/src/main/scala/fsa/Accumulator.scala)
6. [`src/main/scala/fsa/BankedSRAM.scala`](../../FSA-main/src/main/scala/fsa/BankedSRAM.scala)
7. [`src/main/scala/fsa/ExecutionPlan.scala`](../../FSA-main/src/main/scala/fsa/ExecutionPlan.scala)
8. [`src/main/scala/fsa/MatrixEngineController.scala`](../../FSA-main/src/main/scala/fsa/MatrixEngineController.scala)
9. [`src/main/scala/fsa/FSA.scala`](../../FSA-main/src/main/scala/fsa/FSA.scala)
10. 阶段一完成后，再读 `dma/`、`frontend/` 和 `AXI4FSA.scala`

---

## 2. 建议的 HLS 工程目录

以下目录以现在已经建立的 `FSA-HLS` 为根目录：

```text
FSA-HLS/
├── include/fsa/
│   ├── config.hpp
│   ├── types.hpp
│   ├── control.hpp
│   ├── instruction.hpp
│   ├── state.hpp
│   ├── arithmetic.hpp
│   ├── pe.hpp
│   ├── cmp.hpp
│   ├── delayer.hpp
│   ├── systolic_array.hpp
│   ├── accumulator.hpp
│   ├── banked_sram.hpp
│   ├── execution_plan.hpp
│   ├── matrix_engine_controller.hpp
│   ├── fsa.hpp
│   ├── dma.hpp
│   └── axi4_fsa.hpp
├── src/
│   ├── core/
│   │   ├── arithmetic.cpp
│   │   ├── pe.cpp
│   │   ├── cmp.cpp
│   │   ├── delayer.cpp
│   │   ├── systolic_array.cpp
│   │   ├── accumulator.cpp
│   │   ├── banked_sram.cpp
│   │   ├── execution_plan.cpp
│   │   ├── matrix_engine_controller.cpp
│   │   └── fsa.cpp
│   ├── dma/
│   │   ├── request_partitioner.cpp
│   │   ├── lsq.cpp
│   │   └── dma_top.cpp
│   └── system/
│       ├── decoder.cpp
│       ├── semaphores.cpp
│       ├── system_control.cpp
│       └── axi4_fsa.cpp
├── docs/
│   ├── module_mapping.md
│   ├── interface_notes.md
│   ├── call_graph.md
│   └── static_checklist.md
└── README.md
```

当前没有测试环境，因此暂时不建立或不填写 `tb/`、仿真脚本、综合脚本和上板脚本。以后环境具备时再补充，不影响当前的文件接口设计。

---

## 3. 文件之间的依赖关系

编写顺序不能随意，因为后面的文件会使用前面定义的类型和函数。

```text
config.hpp
    ↓
types.hpp
    ↓
control.hpp + instruction.hpp
    ↓
state.hpp
    ↓
arithmetic.hpp/.cpp
    ↓
pe.hpp/.cpp + cmp.hpp/.cpp + delayer.hpp/.cpp
    ↓
systolic_array.hpp/.cpp
    ↓
accumulator.hpp/.cpp + banked_sram.hpp/.cpp
    ↓
execution_plan.hpp/.cpp
    ↓
matrix_engine_controller.hpp/.cpp
    ↓
fsa.hpp/.cpp
    ↓
阶段一完成
    ↓
DMA 文件
    ↓
decoder + semaphores + system_control
    ↓
axi4_fsa
```

如果前面的公共类型还没有确定，不要提前编写后面的顶层文件，否则会反复修改接口。

---

# 阶段一：核心计算部分

## 4. 公共基础文件

### 4.1 `include/fsa/config.hpp`

#### 负责什么

集中保存整个工程的固定参数。其他文件只能读取这些参数，不应在各自文件中重复写 `4`、`16`、存储深度等数字。

#### 建议内容

```cpp
namespace fsa {

// 名称对应 HasFSAParams 中的同名方法。
constexpr int SA_ROWS = 4;
constexpr int SA_COLS = 4;
constexpr int SPAD_ROWS = 2 * SA_COLS + 4 * SA_ROWS;  // 24
constexpr int ACC_ROWS = 1 + SA_ROWS;                 // 5

// 名称对应 FSAParams.nMemPorts 和 HasArithmeticParams.exp2PWLPieces。
constexpr int nMemPorts = 4;
constexpr int dmaLoadInflight = 16;
constexpr int dmaStoreInflight = 8;
constexpr int exp2PWLPieces = 8;
constexpr int N_SEMAPHORES = 32;

// Chisel 中只有 reciprocalLatency 是显式传给 ExecutionPlan 的延迟参数。
// 当前没有综合结果，用 -1 明确表示尚未确定，不能假装它等于 1。
constexpr int LATENCY_NOT_DETERMINED = -1;
constexpr int reciprocalLatency = LATENCY_NOT_DETERMINED;

}  // namespace fsa
```

#### 当前静态检查

- 所有数组长度是否来自这里；
- 是否避免在其他文件中散落相同的数字；
- 参数名称是否能看出单位和含义；
- 暂时无法确定的参数是否有清晰的 `TODO`，而不是随便写一个值后忘记。

#### 编写顺序

这是第一个文件。

---

### 4.2 `include/fsa/types.hpp`

#### 负责什么

定义工程中最基础的数据类型和 `valid + data` 结构。

#### 建议内容

第一版为了便于阅读，可以先使用：

```cpp
namespace fsa {

using elem_t = float;  // 最终目标是 FP16
using acc_t = float;   // 最终目标是 FP32

template <typename T>
struct ValidData {
    bool valid;
    T bits;
};

// 对应 Chisel 的 Decoupled(T)，比 Valid(T) 多一个 ready。
template <typename T>
struct DecoupledData {
    bool valid;
    bool ready;
    T bits;
};

}  // namespace fsa
```

还可以增加：

```cpp
using ElemVector = std::array<elem_t, SA_ROWS>;
using AccVector = std::array<acc_t, SA_COLS>;

// 对应 frontend/Semaphores.scala 中的 class Semaphore。
// 放在公共类型文件中，是因为阶段一的 MatrixControllerIO 已经有 sem_release 端口。
struct Semaphore {
    unsigned id;
    unsigned value;
};
```

如果担心 `std::array` 的 HLS 兼容性，可以直接使用固定长度 C 数组。全工程应选择一种写法并保持一致。

#### 当前静态检查

- 不使用 `std::vector`、动态数组和 `new/malloc`；
- 不使用字符串表示硬件命令；
- 所有数组长度在编译时固定；
- `ValidData<T>` 的 `valid` 和 `bits` 始终一起传递；
- 在文件注释中说明 `elem_t` 与 `acc_t` 的区别。

#### 编写顺序

在 `config.hpp` 之后编写。

---

### 4.3 `include/fsa/control.hpp`

#### 负责什么

定义 PE、CMP、Accumulator 的控制命令。它相当于把 Chisel 的 `Bundle` 和命令编号翻译成 C++ 结构体和枚举。

#### 建议结构

```cpp
namespace fsa {

struct PECtrl {
    bool mac;
    bool acc_ui;
    bool load_reg_li;
    bool load_reg_ui;
    bool flow_lr;
    bool flow_ud;
    bool flow_du;
    bool update_reg;
    bool exp2;
};

enum class CmpControlCmd {
    UPDATE = 0,
    PROP_MAX = 1,
    PROP_MAX_DIFF = 2,
    PROP_ZERO = 3,
    RESET = 4,
    PROP_EXP2_INTERCEPTS = 5
};

struct CmpControl {
    CmpControlCmd cmd;
    unsigned causalCounter;
};

enum class AccumulatorCmd {
    EXP_S1 = 0,
    EXP_S2 = 1,
    ACC_SA = 2,
    ACC = 3,
    SET_SCALE = 4,
    RECIPROCAL = 5
};

struct AccumulatorControl {
    AccumulatorCmd cmd;
};

}  // namespace fsa
```

#### 当前静态检查

- PE 九个控制位是否一个不少；
- 命令名称是否与 Scala 原命令一一对应；
- 不在 PE/CMP 源文件中重复定义命令；
- 所有控制结构是否能默认初始化为“不做任何操作”。

#### 编写顺序

在 `types.hpp` 之后，与 `instruction.hpp` 同一层级。

---

### 4.4 `include/fsa/instruction.hpp`

#### 负责什么

定义 Matrix 指令和阶段二使用的 DMA 指令。这里只定义数据格式，不实现解码和执行。

#### 建议结构

```cpp
namespace fsa {

enum class InstTypes {
    FENCE = 0,
    MATRIX = 1,
    DMA = 2
};

enum class MxFunc {
    LOAD_STATIONARY = 0,
    ATTENTION_SCORE_COMPUTE = 1,
    ATTENTION_VALUE_COMPUTE = 2,
    ATTENTION_LSE_NORM_SCALE = 3,
    ATTENTION_LSE_NORM = 4
};

struct MatrixInstructionHeader {
    InstTypes instType;
    unsigned semId;
    bool acquireValid;
    unsigned acquireSemValue;
    bool releaseValid;
    unsigned releaseSemValue;
    MxFunc func;
    bool waitPrevAcc;
};

struct MatrixInstructionSpad {
    unsigned addr;
    int stride;
    bool revInput;
    bool revOutput;
    bool delayOutput;
};

struct MatrixInstructionAcc {
    unsigned addr;
    int stride;
    bool zero;
    bool causal;
};

struct MatrixInstruction {
    MatrixInstructionAcc acc;
    MatrixInstructionSpad spad;
    MatrixInstructionHeader header;
};

enum class DMAFunc {
    LD_SRAM = 0,
    ST_SRAM = 1
};

struct DMAInstructionHeader {
    InstTypes instType;
    unsigned semId;
    bool acquireValid;
    unsigned acquireSemValue;
    bool releaseValid;
    unsigned releaseSemValue;
    DMAFunc func;
    unsigned repeat;
};

struct DMAInstructionSRAM {
    unsigned addr;
    int stride;
    bool isAccum;
    unsigned mem_stride1;
};

struct DMAInstructionMem {
    unsigned long long addr;
    unsigned stride2;
    unsigned size;
};

struct DMAInstruction {
    DMAInstructionMem mem;
    DMAInstructionSRAM sram;
    DMAInstructionHeader header;
};

// 对应 DMAInstruction.scala 的 getStride：拼接 mem_stride1 与 stride2 后按有符号数解释。
int getStride(const DMAInstruction& instruction);

struct FenceInstruction {
    InstTypes instType;
    bool matrix;
    bool dma;
    bool stop;
};

}  // namespace fsa
```

信号量字段可以在阶段二再补充，避免阶段一一开始就被外围依赖干扰。

#### 当前静态检查

- Matrix 五类功能是否完整；
- 地址和 stride 是否分开表示；
- 有符号 stride 是否使用有符号类型；
- 字段名称是否能和原 Scala 指令字段对照；
- 不在这个文件中编写执行逻辑。

#### 编写顺序

在 `types.hpp` 之后编写。

---

### 4.5 `include/fsa/state.hpp`

#### 负责什么

集中定义“需要跨周期保存”的状态。输入和输出不是状态，不要混在这里。

#### 建议结构

```cpp
namespace fsa {

struct PEState {
    elem_t reg;
    bool exp2Done;
};

struct CMPState {
    acc_t oldMax;
    acc_t newMax;
    unsigned exp2_counter;
};

template <typename T, int rows>
struct InputDelayerState {
    T out_delay_pipe[rows][rows];
    bool rev_out_r;
    bool delay_r;
};

using ElemInputDelayerState = InputDelayerState<elem_t, SA_ROWS>;
using OutputDelayerState = InputDelayerState<acc_t, SA_COLS>;

struct SystolicArrayState {
    PEState mesh[SA_ROWS][SA_COLS];
    CMPState cmp_array[SA_COLS];

    // Chisel 的 Pipe 没有单独变量名；HLS 使用“原输出端口名 + _pipe”。
    ValidData<elem_t> r_output_pipe[SA_ROWS][SA_COLS];
    ValidData<acc_t> d_output_pipe[SA_ROWS][SA_COLS];
    ValidData<acc_t> u_output_pipe[SA_ROWS][SA_COLS];
};

struct AccumulatorState {
    acc_t scale[SA_COLS];

    // 以下两项是 HLS 为显式表示 RawFloat_Div 内部状态而新增的名字。
    bool reciprocal_busy[SA_COLS];
    unsigned reciprocal_counter[SA_COLS];
};

}  // namespace fsa
```

这里的数组形状只是规划起点。阅读 Scala 连线后，应在 `docs/interface_notes.md` 中说明每一维代表行、列还是流水级。

#### 当前静态检查

- 每个状态都能在原 Scala 中找到对应寄存器或 Pipe；
- 没有把纯组合中间变量错误地放进状态；
- 每个状态都说明复位值或初始值；
- 行列下标含义保持统一：始终使用 `[row][col]`；
- `current` 与 `next` 使用同一种状态结构。

#### 编写顺序

在公共类型和控制类型确定后编写，其他核心模块依赖它。

---

## 5. 算术文件

### 5.1 `include/fsa/arithmetic.hpp`

#### 负责什么

只声明算术操作，不保存 PE、CMP 或 Accumulator 状态。

#### 建议函数

```cpp
namespace fsa {

acc_t peMac(elem_t in_a, elem_t in_b, acc_t in_c);
acc_t accUnit(acc_t in_a, acc_t in_b, acc_t in_c);

struct CmpUnitOutput {
    acc_t out_max;
    acc_t out_diff;
};

CmpUnitOutput accCmp(acc_t in_a, acc_t in_b);

elem_t cvtAtoE(acc_t a);
acc_t viewEasA(elem_t e);
elem_t viewAasE(acc_t a);

elem_t peExp2PWL(elem_t x, elem_t slope, acc_t intercept);
acc_t accExp2PWL(acc_t x);
acc_t reciprocal(acc_t value);

elem_t elemZero();
elem_t elemOne();
acc_t accZero();
acc_t accMinimum();
acc_t attentionScale(int dk = SA_ROWS);

}  // namespace fsa
```

### 5.2 `src/core/arithmetic.cpp`

#### 负责什么

实现上述函数。当前只做接口和基础实现时，可以先用 `float` 写清公式，并用注释标记最终要与 EasyFloat 对齐的地方。

#### 函数内部规划

- `peMac()`：对应 `ArithmeticImpl.peMac`，供 PE 使用 mixed-precision 乘加；
- `accUnit()`：对应 `ArithmeticImpl.accUnit`，供 Accumulator 使用 FP32 乘加；
- `accCmp()`：对应 `ArithmeticImpl.accCmp`，结果字段保持为 `out_max/out_diff`；
- `cvtAtoE()`：对应原同名函数，执行真正的数值格式转换；
- `viewEasA()/viewAasE()`：对应原同名函数，只做位视图转换；
- `peExp2PWL()/accExp2PWL()`：分别对应 `FPMacUnit` 与 `FPAccUnit` 的 exp2 路径；
- `reciprocal()`：对应 `FPAccUnit` 中的 `RawFloat_Div`；
- 常量函数：统一产生 0、1、负无穷和 attention scale。

#### 当前静态检查

- 算术函数没有静态可变状态；
- PE 与 Accumulator 没有各写一套重复的 MAC；
- 位视图转换和数值转换是不同函数；
- `std::exp()` 只能作为临时占位，并带明确 `TODO`；
- 不使用异常、递归和动态内存。

#### 编写顺序

公共类型和状态文件完成后，第一个编写的功能模块。

---

## 6. PE 文件

### 6.1 `include/fsa/pe.hpp`

#### 负责什么

定义单个 PE 一拍所需的输入、输出和函数声明。

#### 建议结构与函数

```cpp
namespace fsa {

// 字段名与 PE.scala 的 io 完全一致。
struct PEIO {
    ValidData<PECtrl> in_ctrl;
    ValidData<PECtrl> out_ctrl;

    ValidData<acc_t> u_input;
    ValidData<acc_t> u_output;
    ValidData<acc_t> d_input;
    ValidData<acc_t> d_output;

    ValidData<elem_t> l_input;
    ValidData<elem_t> r_output;
};

void reset_pe_state(PEState& state);

void pe_step(
    const PEState& current,
    PEState& next,
    PEIO& io);

}  // namespace fsa
```

### 6.2 `src/core/pe.cpp`

#### `reset_pe_state()`

只设置 PE 的初始状态，例如 `reg` 和 `exp2Done`。不要在这里修改外部 Pipe。

#### `pe_step()` 内部规划

建议按以下顺序写组合逻辑：

1. `next = current`，默认保持状态；
2. 读取九个控制位；
3. 计算 MAC 的三个输入；
4. 选择普通 MAC 或 exp2；
5. 按原 Scala 优先级决定是否更新 `next.reg`；
6. 更新 `next.exp2Done`；
7. 产生 `io.r_output`；
8. 产生 `io.d_output`；
9. 产生 `io.u_output`；
10. 分别计算三个输出的 `valid`。

#### 当前静态检查

- `pe_step()` 不直接修改 `current`；
- 所有状态更新只写 `next`；
- `load_reg_li`、`load_reg_ui`、`update_reg` 的优先级与 Scala 一致；
- 输出旧 `reg` 还是新 `reg` 的语义写在注释中；
- 输出数据和输出 `valid` 在相邻代码中计算；
- PE 不知道当前是 QK、softmax 还是 PV，它只执行控制命令。

#### 编写顺序

在算术文件之后编写，是第一个带状态的计算模块。

---

## 7. CMP 文件

### 7.1 `include/fsa/cmp.hpp`

#### 建议结构与函数

```cpp
namespace fsa {

// 字段名与 CMP.scala 的 io 完全一致。
struct CMPIO {
    ValidData<acc_t> d_input;
    ValidData<acc_t> d_output;
    ValidData<CmpControl> in_ctrl;
    ValidData<CmpControl> out_ctrl;
};

void reset_cmp_state(CMPState& state);

void cmp_step(
    const CMPState& current,
    CMPState& next,
    CMPIO& io);

}  // namespace fsa
```

### 7.2 `src/core/cmp.cpp`

#### `cmp_step()` 内部规划

1. 默认 `next = current`；
2. 根据 `io.in_ctrl.bits.causalCounter` 决定使用 `io.d_input.bits` 还是负无穷；
3. 计算 `max(d_input, newMax)`；
4. 计算 `0-newMax` 或 `oldMax-newMax`；
5. 根据 CMP 命令选择输出；
6. 更新 `newMax`；
7. 在规定命令下更新 `oldMax`；
8. 更新 exp2 intercept counter；
9. 将控制传给右侧，并更新 causal counter。

#### 当前静态检查

- 六条 CMP 命令都有明确分支；
- `RESET` 同时处理 `oldMax` 和 `newMax`；
- causal mask 使用 `accMinimum()`，不使用任意大负数；
- exp2 counter 只在对应命令下更新；
- 不把 `oldMax` 与 `newMax` 的含义写反。

#### 编写顺序

在 PE 之后编写。

---

## 8. Delayer 文件

### 8.1 `include/fsa/delayer.hpp`

#### 建议结构与函数

```cpp
namespace fsa {

// 对应 InputDelayer.scala 中 io.in.bits 的匿名 Bundle。
struct InputDelayerInBits {
    ElemVector data;
    bool rev_input;
    bool delay_output;
    bool rev_output;
};

struct InputDelayerIO {
    ValidData<InputDelayerInBits> in;
    ElemVector out;
};

struct OutputDelayerIO {
    AccVector in;
    AccVector out;
};

void reset_input_delayer_state(ElemInputDelayerState& state);
void reset_output_delayer_state(OutputDelayerState& state);

void input_delayer_step(
    const ElemInputDelayerState& current,
    ElemInputDelayerState& next,
    InputDelayerIO& io);

void output_delayer_step(
    const OutputDelayerState& current,
    OutputDelayerState& next,
    OutputDelayerIO& io);

}  // namespace fsa
```

如果 `elem_t` 与 `acc_t` 不同，可以给 Delayer 写成模板；如果模板让代码过于难读，也可以分别定义 `ElemDelayerState` 与 `AccDelayerState`。

### 8.2 `src/core/delayer.cpp`

#### `input_delayer_step()` 内部规划

1. 根据 `io.in.bits.rev_input` 选择输入顺序，得到与 Chisel 同名的 `in_data`；
2. 更新每一路移位寄存器；
3. 根据 `delay_output` 选择 `in_data` 或 `out_delay`；
4. 根据 `rev_output` 选择最终顺序并写入 `io.out`；
5. 在输入无效时保持需要跨周期保存的布局控制。

#### `output_delayer_step()` 内部规划

复用相同的阶梯延迟思想，但使用 OutputDelayer 固定的反转和延迟配置。

#### 当前静态检查

- 第 `i` 路有且只有 `i` 级延迟；
- 反转发生在延迟之前还是之后写得清楚；
- 原 `InputDelayer.io.out` 本身没有 valid；哪些拍有效由 `MatrixEngineController` 的计划保证，不能自行添加一条不同步的 valid 流水；
- 输入无效时，保存的布局控制不会被错误清空；
- InputDelayer 与 OutputDelayer 没有把 `SA_ROWS`、`SA_COLS` 混用。

#### 编写顺序

在 PE、CMP 之后编写，阵列顶层依赖它。

---

## 9. Systolic Array 文件

### 9.1 `include/fsa/systolic_array.hpp`

#### 建议结构与函数

```cpp
namespace fsa {

// 字段名与 SystolicArray.scala 的 io 完全一致。
struct SystolicArrayIO {
    ValidData<CmpControl> cmp_ctrl;
    ValidData<PECtrl> pe_ctrl[SA_ROWS];
    ElemVector pe_data;
    ValidData<acc_t> acc_out[SA_COLS];
};

void reset_systolic_array_state(SystolicArrayState& state);

void systolic_array_step(
    const SystolicArrayState& current,
    SystolicArrayState& next,
    SystolicArrayIO& io);

}  // namespace fsa
```

### 9.2 `src/core/systolic_array.cpp`

#### `systolic_array_step()` 内部规划

建议把函数分成几个小的内部辅助函数：

```cpp
void connect_r_output_pipe(...);
void connect_d_output_pipe(...);
void connect_u_output_pipe(...);
void step_cmp_array(...);
void step_mesh(...);
void update_pipe_state(...);
void collect_acc_out(...);
```

主函数只负责按顺序调用这些辅助函数。

核心原则：

- 每个 PE 都只读取 `current` 中的邻接 Pipe；
- 所有 PE 的结果先写入临时输出；
- 临时输出在本拍末写入 `next` 中的 Pipe；
- 不能在二维循环中让后一个 PE 直接读取前一个 PE 刚算出的结果；
- 行列下标始终明确写成 `row`、`col`，不要使用含义不清楚的 `i`、`j`。

#### 当前静态检查

- 横向数据只从左向右；
- CMP 控制只从左向右；
- 向下通路为 CMP→顶行 PE→底行 PE；
- 向上通路为底部零→底行 PE→顶行 PE→CMP；
- 相邻单元之间都经过显式 Pipe 状态；
- 阵列函数调用 PE/CMP，不重复实现它们的内部算法；
- 输出取自每列底部 PE 的下行输出。

#### 编写顺序

在 PE、CMP、Delayer 接口稳定后编写。

---

## 10. Accumulator 文件

### 10.1 `include/fsa/accumulator.hpp`

#### 建议结构与函数

```cpp
namespace fsa {

// 字段名与 Accumulator.scala 的 io 完全一致。
struct AccumulatorIO {
    ValidData<AccumulatorControl> ctrl_in;
    AccVector sa_in;
    AccVector sram_in;
    AccVector sram_out;
};

void reset_accumulator_state(AccumulatorState& state);

void accumulator_step(
    const AccumulatorState& current,
    AccumulatorState& next,
    AccumulatorIO& io);

}  // namespace fsa
```

### 10.2 `src/core/accumulator.cpp`

#### `accumulator_step()` 内部规划

对每一列执行相同流程：

1. 根据命令选择 `in_a`；
2. 根据命令选择 `in_b`；
3. 根据命令选择 `in_c`；
4. 调用 `accUnit()` 或 `accExp2PWL()`；
5. 在 `EXP_S1/EXP_S2/SET_SCALE/RECIPROCAL` 下更新 `next.scale[col]`；
6. 产生要写回 AccRAM 的数据；
7. 更新 reciprocal 的 busy/counter 状态。

六条命令必须分别写清：

| 命令 | 行为 |
|---|---|
| `EXP_S1` | `scale = sa_in × attentionScale(SA_ROWS)` |
| `EXP_S2` | `scale = exp2(scale)` |
| `ACC_SA` | `sram_out = scale × sram_in + sa_in` |
| `ACC` | `sram_out = scale × sram_in` |
| `SET_SCALE` | `scale = sram_in` |
| `RECIPROCAL` | `scale = 1 / scale` |

#### 当前静态检查

- 六条命令都有独立分支；
- 各列状态彼此独立，不共用一个 scale；
- `sram_out` 与“是否真正写 SRAM”分开；
- reciprocal 的多周期状态不混入普通 MAC；
- attention scale 由算术公共文件提供。

#### 编写顺序

可以在 SA 文件之后编写，也可以在 SA 连线尚未完成时独立设计接口。

---

## 11. BankedSRAM 文件

### 11.1 `include/fsa/banked_sram.hpp`

#### 负责什么

定义核心使用的 Scratchpad 和 Accumulator RAM。当前先写清接口，不急于实现所有 bank 冲突处理。

#### 建议结构与函数

```cpp
namespace fsa {

// 名称对应 BankedSRAM.scala 的四类端口。
template <typename T, int rowSize>
struct SRAMFullRead {
    bool valid;
    unsigned addr;
    bool ready;
    T data[rowSize];
    bool subBankMask[rowSize];  // 实际长度以后按 nSubBanks 收紧
};

template <typename T, int rowSize>
struct SRAMFullWrite {
    bool valid;
    unsigned addr;
    bool ready;
    T data[rowSize];
    bool subBankMask[rowSize];
};

template <typename T>
struct SRAMNarrowRead {
    bool valid;
    unsigned addr;
    bool ready;
    unsigned subBankIdx;
    T data;
};

template <typename T>
struct SRAMNarrowWrite {
    bool valid;
    unsigned addr;
    bool ready;
    unsigned subBankIdx;
    T data;
};

template <typename T, int rows, int rowSize>
struct BankedSRAMState {
    T banks[rows][rowSize];

    // Chisel 使用 RegNext(bankIdx) 对齐同步读数据；这是 HLS 显式保存的流水状态。
    unsigned bankIdxReg;
};

using SpRAMState = BankedSRAMState<elem_t, SPAD_ROWS, SA_ROWS>;
using AccRAMState = BankedSRAMState<acc_t, ACC_ROWS, SA_COLS>;

void reset_banked_sram(...);
void banked_sram_step(...);

}  // namespace fsa
```

### 11.2 `src/core/banked_sram.cpp`

#### 函数规划

- `banked_sram_step()`：统一处理 `fullRead/fullWrite/narrowRead/narrowWrite` 四类端口；
- 阶段一先实例化 `spRAM` 和 `accRAM`，只完成计算侧使用的 `fullRead/fullWrite`；
- 使用 `bankIdxReg` 保存一拍 bank 编号，对应 Chisel 的 `bankIdxReg = RegNext(bankIdx)`；
- 阶段二再增加窄口 Load/Store 访问和 bank/sub-bank 仲裁。

#### 当前静态检查

- 读请求和读响应是两个不同的时刻；
- `spRAM` 行宽为 `SA_ROWS`；
- `accRAM` 行宽为 `SA_COLS`；
- 地址类型不能为负数；
- stride 的计算放在 Controller/DMA，不放在 SRAM 内；
- 数组边界检查条件写清楚；
- 核心整行端口与 DMA 窄口概念分开。

#### 编写顺序

在核心数据类型确定后编写，在核心顶层之前完成接口。

---

## 12. ExecutionPlan 文件

### 12.1 `include/fsa/execution_plan.hpp`

#### 负责什么

定义五类 Matrix 指令每个逻辑步骤要产生的控制信号。当前只规划静态表结构，不做自动优化。

#### 建议结构

```cpp
namespace fsa {

// 名称和字段对应 MatrixEngineController.scala。
struct SpRead {
    bool is_constant;
    unsigned addr;
    bool rev_sram_out;
    bool delay_sram_out;
    bool rev_delayer_out;
};

struct AccRead {
    bool is_constant;
    unsigned addr;
    unsigned const_idx;
    bool rmw;
};

// MicroOperation 是 HLS 为把原 MatrixControllerIO 的逐拍输出集中保存而新增的类型；
// 其中每个字段仍使用原 io 名称。
struct MicroOperation {
    ValidData<SpRead> sp_read;
    ValidData<AccRead> acc_read;
    ValidData<CmpControl> cmp_ctrl;
    ValidData<PECtrl> pe_ctrl[SA_ROWS];
    ValidData<AccumulatorControl> acc_ctrl;
    ValidData<Semaphore> sem_release;
    bool conflictFree;
    bool computeDone;
    bool accumDone;
};

MicroOperation getMicroOperation(
    MxFunc func,
    unsigned computeTimer,
    unsigned accumTimer,
    const MatrixInstruction& instruction);

unsigned computeMaxCycle(MxFunc func);
unsigned accStartCycle(MxFunc func);
unsigned accumulateMaxCycle(MxFunc func);

}  // namespace fsa
```

### 12.2 `src/core/execution_plan.cpp`

#### 函数规划

- `getMicroOperation()`：根据 `func` 和 timer 返回当前控制；
- `computeMaxCycle()`：对应 `ExecutionPlan.computeMaxCycle`；
- `accStartCycle()`：对应 `ExecutionPlan.accStartCycle`；
- `accumulateMaxCycle()`：对应 `ExecutionPlan.accumulateMaxCycle`；
- 可以为五类指令各写一个内部函数：

```cpp
MicroOperation LoadStationary(...);
MicroOperation AttentionScoreExecPlan(...);
MicroOperation AttentionValueExecPlan(...);
MicroOperation AttentionLseNormScale(...);
MicroOperation AttentionLseNorm(...);
```

当前不需要重写 Scala 的 `ControlGen.optimize()`。先用容易阅读的条件和静态表表达原计划，后续有工具环境时再考虑自动生成微码。

#### 当前静态检查

- 五类指令均有独立计划函数；
- 一个周期的所有控制集中在 `MicroOperation` 中；
- PE 控制按行存放；
- compute 与 accumulator timer 没有混为一个；
- 地址值和地址递增行为没有重复放在多个模块中；
- 所有未使用控制字段都明确初始化为无效。

#### 编写顺序

在 PE/CMP/Accumulator 控制类型稳定后编写，在 MatrixEngineController 之前。

---

## 13. MatrixEngineController 文件

### 13.1 `include/fsa/matrix_engine_controller.hpp`

#### 建议状态与函数

```cpp
namespace fsa {

// 一个状态对应原来的一个 MatrixControlFSM。
struct MatrixControlFSMState {
    MatrixInstructionHeader header;
    MatrixInstructionSpad rs1;
    MatrixInstructionAcc rs2;
    bool conflictFreeFlag;
    bool computeFlags[5];
    bool accumFlags[5];
    unsigned computeTimer;
    unsigned accumTimer;
};

struct MatrixEngineControllerState {
    MatrixControlFSMState fsm_list[2];
    unsigned enq_ptr;
};

// 字段对应原 MatrixControllerIO；in 是 Chisel Decoupled 接口。
struct MatrixControllerIO {
    DecoupledData<MatrixInstruction> in;
    ValidData<SpRead> sp_read;
    ValidData<AccRead> acc_read;
    ValidData<CmpControl> cmp_ctrl;
    ValidData<PECtrl> pe_ctrl[SA_ROWS];
    ValidData<AccumulatorControl> acc_ctrl;
    ValidData<Semaphore> sem_release;
    bool busy;
};

void reset_matrix_engine_controller(MatrixEngineControllerState& state);

void matrix_engine_controller_step(
    const MatrixEngineControllerState& current,
    MatrixEngineControllerState& next,
    MatrixControllerIO& io);

}  // namespace fsa
```

### 13.2 `src/core/matrix_engine_controller.cpp`

#### 建议内部函数

```cpp
void accept_instruction(...);
void advance_computeTimer(...);
void advance_accumTimer(...);
void update_rs1_rs2(...);
void update_computeFlags_accumFlags(...);
MicroOperation mux_fsm_io(...);
bool canEnq(...);
```

#### 编写策略

为了降低难度，先只实现 `fsm_list[0]`：

```text
接收一条指令
→ 按 timer 读取 ExecutionPlan
→ 指令结束
→ 再接收下一条
```

单上下文接口稳定后，再保留 `fsm_list[1]` 的结构和注释。双 FSM 的真正重叠逻辑放在后续实现，不要在第一次写 Controller 时完成。

#### 当前静态检查

- Controller 只产生控制，不直接进行 PE 算术；
- 地址只在对应读请求有效时按 stride 更新；
- compute 与 accumulator 状态分开；
- `current` 不被直接修改；
- 合并两个 context 时，每类控制都要检查是否冲突；
- 单上下文模式下，第二 context 明确保持无效。

#### 编写顺序

在 ExecutionPlan 之后编写。

---

## 14. 核心顶层文件

### 14.1 `include/fsa/fsa.hpp`

#### 建议状态与接口

```cpp
namespace fsa {

// 成员名对应 FSA.scala 中实例化子模块时使用的 val 名称。
struct FSAState {
    MatrixEngineControllerState mxControl;
    SpRAMState spRAM;
    AccRAMState accRAM;
    ElemInputDelayerState inputDelayer;
    OutputDelayerState outputDelayer;
    SystolicArrayState sa;
    AccumulatorState accumulator;
};

struct FSAIO {
    DecoupledData<MatrixInstruction> inst;
    ValidData<Semaphore> sem_release;
    bool busy;
};

void reset_fsa_state(FSAState& state);

void fsa_step(
    FSAState& state,
    FSAIO& io);

}  // namespace fsa
```

### 14.2 `src/core/fsa.cpp`

#### `reset_fsa_state()`

依次调用各子模块的 reset 函数，不在顶层重复写每个字段的初始值。

#### `fsa_step()` 内部规划

顶层需要显式建立本拍 `current` 和下一拍 `next`：

```cpp
FSAState current = state;
FSAState next = current;
```

然后按数据关系组织：

1. `mxControl` 产生本拍控制；
2. `spRAM/accRAM` 根据上一拍请求产生读数据，并接收本拍新请求；
3. InputDelayer 处理 Scratchpad 数据；
4. `sa` 执行一个逻辑步骤；
5. OutputDelayer 对齐阵列列输出；
6. Accumulator 计算写回数据；
7. `accRAM` 接收写请求；
8. 统一 `state = next`。

由于函数在 C++ 中的书写顺序可能让人误以为存在先后数据穿透，顶层应通过局部输入/输出结构明确说明每个子模块读取的是哪一拍的数据。

#### 建议内部辅助函数

```cpp
void connect_inputDelayer_io(...);
void connect_sa_io(...);
void connect_accumulator_io(...);
void connect_spRAM_accRAM_io(...);
void collect_fsa_busy(...);
```

#### 当前静态检查

- 顶层只负责连接，不重复实现 PE/CMP 算法；
- 每个子模块只调用一次 step；
- 所有 step 读取同一个 `current` 快照；
- 最后只进行一次统一状态提交；
- Scratchpad→Delayer→SA→Accumulator→AccRAM 的方向清楚；
- DMA 尚未接入时，预留片上存储初始化/访问接口，但不在核心中实现 DMA。

#### 编写顺序

阶段一最后编写。

---

## 15. 阶段一推荐编写顺序汇总

按照下面顺序推进，每一步只在接口稳定后进入下一步：

1. `config.hpp`：确定所有固定参数；
2. `types.hpp`：确定数据与 valid 表达；
3. `control.hpp`：确定三类模块控制；
4. `instruction.hpp`：确定 Matrix 指令字段；
5. `state.hpp`：确定所有跨周期状态；
6. `arithmetic.hpp/.cpp`：统一算术函数；
7. `pe.hpp/.cpp`：实现最小计算格；
8. `cmp.hpp/.cpp`：实现最大值和 softmax 辅助状态；
9. `delayer.hpp/.cpp`：实现输入/输出错拍结构；
10. `systolic_array.hpp/.cpp`：连接 PE、CMP 和 Pipe；
11. `accumulator.hpp/.cpp`：实现 L/O 更新接口；
12. `banked_sram.hpp/.cpp`：实现片上存储接口；
13. `execution_plan.hpp/.cpp`：描述五类 Matrix 指令；
14. `matrix_engine_controller.hpp/.cpp`：按照计划产生控制；
15. `fsa.hpp/.cpp`：连接阶段一全部模块。

阶段一最容易出错的地方不是 FlashAttention 公式，而是：

- 把 `current` 和 `next` 混用；
- 数据与 valid 没有一起移动；
- 忽略相邻 PE 之间的 Pipe；
- 把数值转换与位视图转换混淆；
- Scratchpad/AccRAM 行宽或行列方向写反；
- Controller 同时在多个地方更新地址。

---

# 阶段二：DMA 与外围部分

阶段二只在阶段一文件接口稳定后开始。阶段二不修改 PE、CMP 和 SA 内部实现，只通过核心顶层预留的存储和指令接口连接。

## 16. DMA 公共接口

### 16.1 `include/fsa/dma.hpp`

#### 建议结构

```cpp
namespace fsa {

struct DMARequest {
    // 字段名完全对应 dma/DMARequest.scala。
    unsigned long long memAddr;
    int memStride;
    unsigned sramAddr;
    int sramStride;
    unsigned repeat;
    unsigned size;
    bool isLoad;

    unsigned semId;
    bool acquireValid;
    unsigned acquireSemValue;
    bool releaseValid;
    unsigned releaseSemValue;
};

struct RequestPartitionerState {
    DMARequest reqs[nMemPorts];
    bool valid;
};

struct DMARequestVector {
    DMARequest reqs[nMemPorts];
};

struct RequestPartitionerIO {
    DecoupledData<DMARequest> in;
    DecoupledData<DMARequestVector> out;
};

}  // namespace fsa
```

#### 当前静态检查

- Load/Store 共用的字段只定义一次；
- memory 地址和 SRAM 地址使用不同类型；
- stride 有符号；
- `repeat` 与 `size` 的单位写在注释中；
- `isLoad=true` 对应 LoadQueue/Scratchpad，`isLoad=false` 对应 StoreQueue/AccRAM；
- 不额外发明 `target_accumulator`：原 DMA 数据方向已经决定目标 SRAM。

#### 编写顺序

阶段二第一个文件。

---

## 17. Request Partitioner

### 17.1 `src/dma/request_partitioner.cpp`

#### 负责什么

当系统有多个内存端口时，把一个 DMA 请求的 repeat 次数分给不同端口。单端口版本也通过这个函数，以便以后扩展。

#### 建议函数

```cpp
void request_partitioner_step(
    const RequestPartitionerState& current,
    RequestPartitionerState& next,
    RequestPartitionerIO& io);

unsigned initialRepeatCnt(unsigned repeat);
unsigned remainingRepeatCnt(unsigned repeat);
unsigned repeatCnt(unsigned repeat, unsigned portIdx);
unsigned addrIncr(unsigned repeat, unsigned portIdx);
```

#### 当前静态检查

- 所有端口分到的 repeat 总数等于原 repeat；
- repeat 不能整除端口数时，余数分配规则明确；
- 每个端口的 memory/SRAM 起始地址按 stride 正确偏移；
- `nMemPorts == 1` 时保持原请求不变。

#### 编写顺序

在 `dma.hpp` 之后编写。

---

## 18. LoadQueue

### 18.1 `src/dma/lsq.cpp` 中的 LoadQueue 部分

#### 负责什么

把外部内存数据搬入 Scratchpad。

#### 建议函数

```cpp
struct LoadQueueState {
    bool entryValid[dmaLoadInflight];
    DMARequest entries[dmaLoadInflight];
    unsigned enqPtr;
    unsigned acqPtr;
    unsigned deqPtr;
    unsigned arPtr;
    unsigned rPtr;
    unsigned rBeatCnt;
};

struct LoadQueueIO {
    DecoupledData<DMARequest> req;
    DecoupledData<Semaphore> semAcquire;
    DecoupledData<Semaphore> semRelease;
    bool doSemRelease;
    bool busy;
    bool active;
    AXI4BundleAR ar;
    AXI4BundleR r;
    SRAMNarrowWrite<elem_t> spadWrite;
};

void reset_load_queue(LoadQueueState& state);

void load_queue_step(
    const LoadQueueState& current,
    LoadQueueState& next,
    LoadQueueIO& io);

void build_ar(...);
void consume_r(...);
void drive_spadWrite(...);
```

#### 内部职责

1. 接收 Load 请求；
2. 产生外部内存读取地址；
3. 接收一个宽数据 beat；
4. 将 beat 拆成若干 `elem_t`；
5. 产生 Scratchpad 窄口写请求；
6. 更新 beat、repeat 和地址状态；
7. 在全部搬运完成时产生完成标志。

#### 当前静态检查

- 一个宽 beat 拆成多少个元素写清楚；
- `size`、beat bytes、元素 bytes 的单位一致；
- 最后一个 beat 的判断明确；
- 只有请求有效时才更新地址；
- Load 只写 Scratchpad，不直接修改 PE。

#### 编写顺序

Request Partitioner 之后。

---

## 19. StoreQueue

### 19.1 `src/dma/lsq.cpp` 中的 StoreQueue 部分

#### 负责什么

把 Accumulator RAM 的数据搬到外部内存。

#### 建议函数

```cpp
struct StoreQueueState {
    bool entryValid[dmaStoreInflight];
    DMARequest entries[dmaStoreInflight];
    unsigned enqPtr;
    unsigned acqPtr;
    unsigned deqPtr;
    unsigned awPtr;
    unsigned rPtr;
    unsigned rBeatCnt;
    unsigned wBeatCnt;
};

struct StoreQueueIO {
    DecoupledData<DMARequest> req;
    DecoupledData<Semaphore> semAcquire;
    DecoupledData<Semaphore> semRelease;
    bool doSemRelease;
    bool busy;
    bool active;
    AXI4BundleAW aw;
    AXI4BundleW w;
    AXI4BundleB b;
    SRAMNarrowRead<acc_t> accRead;
};

void reset_store_queue(StoreQueueState& state);

void store_queue_step(
    const StoreQueueState& current,
    StoreQueueState& next,
    StoreQueueIO& io);

void build_aw(...);
void drive_accRead(...);
void build_w(...);
void consume_b(...);
```

#### 内部职责

1. 接收 Store 请求；
2. 逐个读取 AccRAM sub-bank；
3. 将多个窄数据拼成外部内存宽 beat；
4. 产生外部内存写请求；
5. 更新 beat、repeat 和地址；
6. 搬运完成后产生完成标志。

#### 当前静态检查

- Store 读取的是 AccRAM，不是 Scratchpad；
- AccRAM 一拍读延迟在接口中有位置；
- 打包顺序与内存布局说明一致；
- 写数据和写地址属于同一个请求；
- 最后一个 beat 和完成标志不会提前。

#### 编写顺序

LoadQueue 之后。

---

## 20. DMA Top

### 20.1 `src/dma/dma_top.cpp`

#### 负责什么

接收 DMA 指令，构造 `dmaReq`，调用 `partitioner`，并选择 `loadQueues` 或 `storeQueues`。

#### 建议函数

```cpp
struct DMAState {
    RequestPartitionerState partitioner;
    LoadQueueState loadQueues[nMemPorts];
    StoreQueueState storeQueues[nMemPorts];
};

struct DMAIO {
    DecoupledData<DMAInstruction> inst;
    DecoupledData<Semaphore> semaphoreAcquire[2];
    ValidData<Semaphore> semaphoreRelease[2];
    SRAMNarrowWrite<elem_t> spadWrite[nMemPorts];
    SRAMNarrowRead<acc_t> accRead[nMemPorts];
    bool busy;
    bool active;
};

void reset_dma_state(DMAState& state);

DMARequest build_dmaReq(const DMAInstruction& inst);

void dma_step(
    const DMAState& current,
    DMAState& next,
    DMAIO& io);

bool allLoadQueuesDone(...);
bool allStoreQueuesDone(...);
```

#### 当前静态检查

- Load 和 Store 不能同时驱动同一个接口；
- 多端口完成条件是所有参与端口均完成；
- `busy`、`active` 和 `done` 含义分开；
- DMA 不读取 MatrixEngineController 的内部状态；
- DMA 只通过定义好的存储端口访问核心存储。

#### 编写顺序

LoadQueue/StoreQueue 接口稳定后编写。

---

## 21. Decoder 文件

### 21.1 `src/system/decoder.cpp`

#### 负责什么

把输入的原始 32 位指令字组合成 Matrix、DMA 或 Fence 指令，并送到正确模块。

#### 建议函数

```cpp
struct InstructionMergerState {
    std::uint32_t buf[4];
    unsigned cnt;
};

struct DecoderIO {
    DecoupledData<std::uint32_t> in;
    DecoupledData<MatrixInstruction> outMx;
    DecoupledData<DMAInstruction> outDMA;
    DecoupledData<FenceInstruction> outFence;
};

InstTypes decode_instType(std::uint32_t firstWord);
MatrixInstruction decode_outMx(const std::uint32_t words[3]);
DMAInstruction decode_outDMA(const std::uint32_t words[4]);
FenceInstruction decode_outFence(std::uint32_t word);
```

如果首版 HLS 顶层直接接收已经解码好的结构体，可以先只保留这些函数声明和字段映射表，暂不实现原始位解析。

#### 当前静态检查

- Matrix 为 3 个 32 位字、DMA 为 4 个 32 位字；
- 每个字段的位范围有注释；
- 有符号 stride 的符号扩展规则清楚；
- Decoder 只解析，不执行指令。

#### 编写顺序

DMA 接口确定后编写。

---

## 22. Semaphores 文件

### 22.1 `src/system/semaphores.cpp`

#### 负责什么

处理 Matrix 和 DMA 之间的数据依赖。例如 DMA 尚未把 K 搬入 Scratchpad 时，Matrix 指令不能开始使用该数据。

#### 建议函数

```cpp
struct SemaphoresState {
    unsigned semaphores[N_SEMAPHORES];
    bool busy[N_SEMAPHORES];
};

struct SemaphoresIO {
    DecoupledData<Semaphore> acquire[3];
    ValidData<Semaphore> release[3];
};

void reset_semaphores(SemaphoresState& state);

bool canAcquire(
    const SemaphoresState& state,
    unsigned id,
    unsigned value);

void handle_acquire(...);
void handle_release(...);
```

#### 当前静态检查

- semaphore ID 有固定范围；
- acquire 与 release 的条件分开；
- 同一 semaphore 的 busy 与 value 含义不混淆；
- Matrix 和 DMA 只通过公开函数访问 semaphore 状态。

#### 编写顺序

Decoder 之后编写。

---

## 23. AXI4FSA Control 文件

### 23.1 `src/system/system_control.cpp`

#### 负责什么

管理 Matrix、DMA 和 Fence 的整体完成关系。这些逻辑在原工程中直接位于 `AXI4FSA.scala`，HLS 版本只是为了控制文件长度才拆到 `system_control.cpp`。

#### 建议函数

```cpp
bool fenceReady(
    const FenceInstruction& fence,
    bool mxDone,
    bool dmaDone);

bool set_done(
    bool rawInstQueueEmpty,
    bool mxDone,
    bool dmaDone,
    bool mxInflight,
    bool dmaInflight);

void update_perfCounters(...);  // 对应 AXI4FSA.scala 的 perfCounters
```

#### 当前静态检查

- Fence 等待 Matrix、DMA 的条件清楚；
- “没有新指令”与“所有执行已经完成”不是同一个条件；
- AXI4FSA control 不直接读写 PE、CMP 或 Accumulator 状态；
- 性能计数器与功能控制分开。

#### 编写顺序

Decoder、DMA 和 Matrix 核心接口都确定后编写。

---

## 24. AXI4FSA 顶层文件

### 24.1 `include/fsa/axi4_fsa.hpp` 与 `src/system/axi4_fsa.cpp`

#### 负责什么

连接 `decoder`、`semaphores`、`fsa` 和 `dma`，对应原工程最外层 `AXI4FSA`。

#### 建议状态

```cpp
enum class AXI4FSARunState {
    s_idle,
    s_active,
    s_done
};

struct DecoderState {
    InstructionMergerState mx;
    InstructionMergerState dma;
};

struct AXI4FSAState {
    FSAState fsa;
    DMAState dma;
    SemaphoresState semaphores;
    DecoderState decoder;
    AXI4FSARunState state;
    bool firstInstFire;
    unsigned enqInstCnt;
    unsigned deqInstCnt;

    // 名称对应 AXI4FSA.scala 中的性能计数器。
    unsigned perfCntExecTime;
    unsigned perfCntMxBubble;
    unsigned perfCntMxActive;
    unsigned perfCntDMAActive;
    unsigned perfCntRawInst;
    unsigned perfCntMxInst;
    unsigned perfCntDMAInst;
    unsigned perfCntFence;
};
```

#### 建议顶层函数

```cpp
void reset_axi4_fsa_state(AXI4FSAState& state);

void axi4_fsa_step(
    AXI4FSAState& state,
    AXI4FSAExternalPorts& ports);
```

#### `axi4_fsa_step()` 内部规划

1. Decoder 接收并分类指令；
2. Semaphores 判断 Matrix/DMA 指令能否启动；
3. `fsa` 执行一个逻辑步骤；
4. DMA 执行一个搬运步骤；
5. 处理 semaphore release；
6. AXI4FSA control 判断 Fence 和整体完成；
7. 统一提交下一状态。

#### 当前静态检查

- 顶层只连接模块，不复制模块内部逻辑；
- Matrix 与 DMA 的存储端口方向一致；
- 同一片上存储端口不能被多个模块同时无条件驱动；
- 指令、数据、状态三类接口在命名上可区分；
- 全系统仍遵守 current/next 一次性提交。

#### 编写顺序

阶段二最后编写。

---

## 25. 阶段二推荐编写顺序汇总

1. 在 `instruction.hpp` 中补全 DMA、Fence 和 semaphore 字段；
2. 在 `dma.hpp` 中定义 DMARequest、DMAState 和端口类型；
3. `request_partitioner.cpp`；
4. `lsq.cpp` 中的 `LoadQueue`；
5. `lsq.cpp` 中的 `StoreQueue`；
6. `dma_top.cpp`；
7. `decoder.cpp`；
8. `semaphores.cpp`；
9. `system_control.cpp`；
10. `axi4_fsa.hpp/.cpp`。

阶段二最容易出错的地方：

- 混淆外部内存地址和片上 SRAM 地址；
- 混淆 `size`、beat 数、字节数和元素数；
- stride 的正负号处理错误；
- Load/Store 的数据拆包顺序不一致；
- AccRAM 一拍读延迟没有体现在 Store 接口中；
- 只看某个端口完成，就错误地认为多端口 DMA 全部完成；
- Fence 只等待指令队列为空，却没有等待 Matrix/DMA 真正结束。

---

## 26. 当前静态检查总清单

当前没有测试环境时，可以对每个文件执行以下人工或 IDE 静态检查。

### 26.1 文件与依赖

- [ ] 每个 `.cpp` 都有对应头文件；
- [ ] 头文件使用 include guard 或 `#pragma once`；
- [ ] 头文件只包含真正需要的依赖；
- [ ] 不存在循环 include；
- [ ] 公共类型只定义一次；
- [ ] 函数声明与函数定义的参数完全一致；
- [ ] 所有内容位于统一的 `fsa` namespace。

### 26.2 HLS 友好性

- [ ] 不使用 `new`、`delete`、`malloc`、`free`；
- [ ] 不使用 `std::vector`、链表、map 等动态容器；
- [ ] 不使用递归；
- [ ] 不使用异常；
- [ ] 不使用虚函数和运行时多态；
- [ ] 循环上界在编译时可确定；
- [ ] 不存在依赖输入数据才退出的无限 `while`；
- [ ] 数组大小来自 `config.hpp`；
- [ ] 顶层接口没有含义不清楚的裸指针。

### 26.3 硬件周期语义

- [ ] 每个有状态模块都有 `current` 与 `next`；
- [ ] step 函数不修改 `current`；
- [ ] 所有状态在逻辑步骤末统一提交；
- [ ] 数据和 valid 同步传递；
- [ ] 相邻 PE/CMP 之间的 Pipe 被显式保存；
- [ ] SRAM 请求和响应不是同一拍；
- [ ] reset 函数为所有状态给出确定初值；
- [ ] 无效控制下状态保持不变。

### 26.4 接口一致性

- [ ] 行列顺序始终为 `[row][col]`；
- [ ] Scratchpad 行宽使用 `SA_ROWS`；
- [ ] AccRAM 行宽使用 `SA_COLS`；
- [ ] `elem_t` 与 `acc_t` 没有随意混用；
- [ ] 数值转换与位视图转换使用不同函数；
- [ ] 地址使用无符号类型，stride 使用有符号类型；
- [ ] 每个命令枚举与原 Scala 一一对应；
- [ ] 函数输入尽量使用 `const` 引用，输出使用非 const 引用。

### 26.5 模块职责边界

- [ ] PE 不判断自己正在执行 QK 还是 PV；
- [ ] CMP 不负责控制整个阵列；
- [ ] SA 不重复实现 PE/CMP 算术；
- [ ] Controller 不直接进行矩阵计算；
- [ ] SRAM 不负责计算 stride；
- [ ] DMA 不直接修改 PE 状态；
- [ ] Decoder 只解析指令；
- [ ] AXI4FSA 顶层只连接模块。

---

## 27. 建议同步维护的说明文件

### 27.1 `FSA-HLS/docs/module_mapping.md`

记录原 Scala 文件到 HLS 文件的对应关系：

```text
PE.scala                 → pe.hpp / pe.cpp
CMP.scala                → cmp.hpp / cmp.cpp
InputDelayer.scala       → delayer.hpp / delayer.cpp
SystolicArray.scala      → systolic_array.hpp / systolic_array.cpp
Accumulator.scala        → accumulator.hpp / accumulator.cpp
BankedSRAM.scala         → banked_sram.hpp / banked_sram.cpp
ExecutionPlan.scala      → execution_plan.hpp / execution_plan.cpp
MatrixEngineController   → matrix_engine_controller.hpp / matrix_engine_controller.cpp
FSA.scala                → fsa.hpp / fsa.cpp
DMA.scala / LSQ.scala    → dma.hpp / dma_top.cpp / lsq.cpp
AXI4FSA.scala            → axi4_fsa.hpp / axi4_fsa.cpp
```

### 27.2 `FSA-HLS/docs/interface_notes.md`

每个模块记录：

```text
模块名称：
输入：
输出：
内部状态：
一步做什么：
调用哪些函数：
被哪些模块调用：
与原 Scala 的对应位置：
仍不确定的问题：
```

### 27.3 `FSA-HLS/docs/call_graph.md`

维护当前调用关系：

```text
axi4_fsa_step
├── decode_instType / decode_outMx / decode_outDMA / decode_outFence
├── semaphore functions
├── fsa_step
│   ├── matrix_engine_controller_step
│   │   └── getMicroOperation
│   ├── banked_sram_step (spRAM / accRAM)
│   ├── input_delayer_step
│   ├── systolic_array_step
│   │   ├── cmp_step
│   │   └── pe_step
│   ├── output_delayer_step
│   └── accumulator_step
└── dma_step
    ├── request_partitioner_step
    ├── load_queue_step
    └── store_queue_step
```

### 27.4 `FSA-HLS/docs/static_checklist.md`

复制第 26 节的检查清单，并在每个文件检查完成后勾选。不要只写“已检查”，应在发现问题时记录文件、函数和原因。

---

## 28. 当前最合理的工作边界

当前只做静态检查时，建议完成到以下程度：

1. 使用已经建立的 `FSA-HLS/include/fsa/`、`FSA-HLS/src/core/` 和 `FSA-HLS/docs/`；
2. 完成 `config.hpp`、`types.hpp`、`control.hpp`、`instruction.hpp`；
3. 完成 `state.hpp` 的第一版；
4. 为阶段一所有模块建立 `.hpp/.cpp` 文件；
5. 在每个 `.hpp` 中写清结构体和函数声明；
6. 在每个 `.cpp` 中先写函数骨架和步骤注释；
7. 完成 `module_mapping.md`、`interface_notes.md` 和 `call_graph.md`；
8. 使用第 26 节清单检查接口和依赖；
9. 对仍不确定的地方写 `TODO(待对照 Scala)`，不要凭感觉补逻辑；
10. 暂时不建立 DMA 实现文件，阶段二先保留在本规划中。

当前静态阶段的重点不是代码行数，而是确保：

```text
每个文件为什么存在
每个结构体表示什么硬件状态
每个函数读取什么、修改什么、输出什么
每条数据从哪个函数流向哪个函数
后续实现时不会因为接口混乱反复推倒重写
```

完成上述内容后，再开始逐个填写 `arithmetic.cpp → pe.cpp → cmp.cpp → delayer.cpp` 的真实逻辑。
