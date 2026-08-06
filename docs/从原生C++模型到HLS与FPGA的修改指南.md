# 从原生 C++ 模型到 HLS 与 FPGA 的修改指南

## 1. 先说结论：现在的代码是什么

`FSA-HLS` 当前已经有一套可以用 `g++` 编译运行的逐拍 C++ 模型。它的主要价值是：

- 和 Chisel 对照功能；
- 用 `current/next` 表示寄存器在时钟沿统一更新；
- 用 testbench 检查 PE、CMP、Delayer、SA 和 Accumulator 的数据流；
- 作为以后 HLS C Simulation 的正确结果参考。

但是，“能被普通 C++ 编译”不等于“已经适合 HLS 综合”。目前还缺少三类内容：

1. **真正的硬件数据类型**：现在 `elem_t` 和 `acc_t` 都是 `float`，地址和计数器也普遍比实际需要更宽；
2. **HLS 顶层和综合约束**：还没有 top function、接口 pragma、循环展开和存储映射；
3. **FPGA 外围**：还没有 BRAM/URAM、AXI、DMA、寄存器接口、Vivado 顶层和主机程序。

因此，后续不要直接把整个目录丢进 HLS 工具点击综合。应把工程分成三层：

```text
当前原生C++逐拍模型
    ↓ 作为功能参考，继续保留
可综合HLS计算核
    ↓ 导出RTL IP
FPGA平台层（AXI、DMA、时钟复位、主机程序）
```

---

## 2. 哪些文件不需要综合

下面这些内容只在电脑上运行，不进入 FPGA：

```text
tests/
run_test.cmd
run_test.ps1
所有std::cout、assert和逐拍打印函数
Windows控制台UTF-8设置
```

例如 `tests/test_attn_score_2x2_trace.cpp` 中的 `windows.h`、`iostream` 和打印函数都只是 testbench。不要把它们复制进 HLS top。

以后应同时保留两套测试：

- `tests/`：使用 `g++` 的快速功能测试；
- `tb/hls/`：交给 HLS 工具执行 C Simulation 和 C/RTL Co-simulation 的测试。

---

## 3. 建议新增的目录和文件

不要马上改坏现在能运行的 C++ 模型。先增加 HLS 专用顶层和脚本：

```text
FSA-HLS/
├── include/fsa/
│   ├── 现有头文件...
│   ├── hls_types.hpp              # HLS精确位宽类型
│   └── fsa_core_top.hpp           # HLS计算核顶层端口
├── src/
│   ├── core/                      # 现有模块，逐步改成可综合代码
│   └── hls/
│       ├── fsa_core_top.cpp       # 阶段一计算核top function
│       └── fsa_system_top.cpp     # 阶段二带DMA的top function
├── tb/hls/
│   ├── test_fsa_core.cpp          # HLS C Simulation testbench
│   └── test_vectors/              # 输入和期望输出
├── hls/
│   ├── run_csim.tcl
│   ├── run_synth.tcl
│   └── run_cosim.tcl
└── fpga/
    ├── rtl/                       # 必要的Verilog包装
    ├── constraints/               # 时钟和引脚约束
    ├── scripts/                   # 创建Vivado工程和Block Design
    └── host/                      # PC/PS侧控制程序
```

使用 AMD Vitis HLS 时，pragma 和脚本可以直接采用 Vitis 写法；如果以后使用 Intel HLS，模块划分不变，但接口和 pragma 名称需要换成 Intel 对应语法。

---

## 4. `current/next/step` 到 HLS 后怎么处理

### 4.1 现在为什么需要 `current/next`

当前模型中：

```cpp
pe_step(current, next, io);
current = next;
```

表达的是：

```text
本拍所有模块只读取旧寄存器current
                   ↓
计算本拍组合逻辑和下一状态next
                   ↓
时钟沿到来，所有寄存器同时更新
```

这对普通 C++ 仿真非常重要，应继续保留。

### 4.2 不能直接把 `current` 和 `next` 都做成顶层端口

如果把下面的函数直接设成 HLS top：

```cpp
void systolic_array_step(
    const SystolicArrayState& current,
    SystolicArrayState& next,
    SystolicArrayIO& io);
```

HLS 很可能把整个 `current` 和 `next` 当成大量外部端口或存储器接口，而不是 SA 内部寄存器。

更合适的做法是在 HLS top 内保存静态状态：

```cpp
void fsa_core_step_top(
    bool reset,
    const CoreInput& input,
    CoreOutput& output){
    static CoreState current;
    CoreState next;

    if(reset){
        reset_core_state(current);
        output = CoreOutput{};
        return;
    }

    core_step(current, next, input, output);
    current = next;
}
```

这里：

- `current` 综合成 FPGA 内部寄存器或片上 RAM；
- `next` 是计算下一状态的临时变量；
- 外部只看见真正需要的输入、输出和复位端口。

第一版可以保留 `next = current`，先保证正确。完成综合后再查看报告，如果出现大量无用复制和多路选择器，再把状态更新改成“只写发生变化的字段”。

---

## 5. 按文件列出需要修改的内容

## 5.1 `config.hpp`

当前固定参数可以继续使用，但必须补充真正与硬件相关的参数：

```cpp
constexpr int ELEM_BITS = 16;
constexpr int ACC_BITS = 32;
constexpr int SPAD_ROW_ADDR_BITS = 5;
constexpr int SPAD_STRIDE_BITS = 5;
constexpr int CMP_CMD_BITS = 3;
constexpr int EXP2_COUNTER_BITS = 3;
constexpr int AXI_DATA_BITS = 512;       // 根据开发板决定
constexpr int AXI_ADDR_BITS = 64;        // 根据平台决定
constexpr int RECIPROCAL_LATENCY = ...; // 综合除法器后填写
```

当前的：

```cpp
constexpr int reciprocalLatency = -1;
```

不能进入最终 HLS 版本。它会参与无符号计数器计算，可能产生很大的错误值。必须在选择倒数实现后换成真实延迟。

## 5.2 `types.hpp`

这是必须最先修改的文件之一。

当前代码：

```cpp
using elem_t = float;
using acc_t = float;
using sram_address_t = std::uint32_t;
using memory_address_t = std::uint64_t;
```

建议的 HLS 方向：

```cpp
#include <ap_int.h>
#include <hls_half.h>

using elem_t = half;                    // 如果目标元素格式是IEEE FP16
using acc_t = float;                    // FP32累加
using sram_address_t = ap_uint<SPAD_ROW_ADDR_BITS>;
using sram_stride_t = ap_int<SPAD_STRIDE_BITS>;
using exp2_counter_t = ap_uint<EXP2_COUNTER_BITS>;
```

如果原 Chisel 使用的格式、舍入方式或异常处理与 `half` 不完全一致，就不能只看“都是 16 位”。应选择工具支持的可配置浮点类型，或者用 `ap_uint<16>` 保存原始位并自己实现转换。

需要特别注意：

- `std::uint8_t` 固定占 8 位，但某些命令实际只需要 3 位；
- C++ 结构体可能产生对齐填充，AXI 端口不能直接假设它会自动紧密打包；
- `std::array` 通常可以综合，但在 HLS 接口和 array partition 上，固定 C 数组往往更直观；
- 不要使用 `std::vector`、`new`、`malloc`、递归和异常。

建议先保留现有 `types.hpp` 供 `g++` 测试，再通过一个 `hls_types.hpp` 集中引入 HLS 类型。等 HLS C Simulation 稳定后，再统一类型，避免到处写 `#ifdef`。

## 5.3 `control.hpp`

PE 的 `bool` 控制位可以继续使用，但要检查接口打包。CMP 和 Accumulator 的枚举当前以 `std::uint8_t` 保存，会使用 8 位。

第一版可以保留 `enum class`，之后根据综合报告决定是否改成：

```cpp
using cmp_cmd_t = ap_uint<3>;
using accumulator_cmd_t = ap_uint<3>;
```

如果控制结构通过 AXI 或 stream 传递，需要使用 HLS 的结构体聚合/紧密打包约束，确保字段顺序和位宽与指令定义一致。

## 5.4 `state.hpp`

当前状态划分是正确方向，但 HLS 时需要决定每个数组综合成什么：

| 状态 | 建议硬件资源 |
|---|---|
| `PEState::reg` | 每个 PE 一个寄存器 |
| `CMPState::oldMax/newMax` | 每列寄存器 |
| `r/d/u_output_pipe` | 完全分割的寄存器阵列 |
| `cmp_d_output_pipe` | 每列寄存器 |
| `AccumulatorState::scale` | 每列寄存器 |
| Scratchpad | BRAM/URAM，多 bank |
| Accumulator RAM | BRAM/URAM，多 bank |

`SystolicArrayState` 中的数组必须允许同一拍并行访问所有 PE。如果 HLS 把它们推成单端口 RAM，整个 SA 就无法并行工作。因此需要在 HLS top 或模块内增加 array partition，例如：

```cpp
#pragma HLS ARRAY_PARTITION variable=state.mesh complete dim=0
#pragma HLS ARRAY_PARTITION variable=state.r_output_pipe complete dim=0
#pragma HLS ARRAY_PARTITION variable=state.u_output_pipe complete dim=0
#pragma HLS ARRAY_PARTITION variable=state.d_output_pipe complete dim=0
```

具体 pragma 能否直接作用于嵌套结构体字段，要以 HLS 报告为准；必要时把这些数组提升为 top 内的独立变量。

## 5.5 `arithmetic.cpp`

这是当前离“可综合且与 Chisel 一致”最远的文件。

| 当前写法 | 问题 | 建议修改 |
|---|---|---|
| `elem_t=float` | 没有体现 FP16 | 换成最终元素格式 |
| `a*b+c` | 不一定是融合乘加 | 选择 HLS FMA/IP，并确认舍入 |
| 普通强制类型转换 | 不能代替位视图转换 | 使用明确的位提取/位拼接 |
| `std::exp2` | 可能生成昂贵 IP，且不同于 Chisel PWL | 改成查找表和 PWL |
| `std::trunc/fabs/ldexp` | 可能产生额外浮点硬件 | 按浮点字段或定点范围实现 |
| `1/value` | 当前不是实际多拍单元 | 选择除法 IP、LUT 或近似迭代 |
| 运行时 `log2(exp(1))/sqrt(dk)` | 生成不必要硬件 | 对固定 `dk` 预计算常量 |
| 自动生成的 PWL 表 | 尚未与 Chisel 验证 | 用相同脚本/参数重新生成并逐项对比 |

其中三种“转换”不能混在一起：

```text
cvtAtoE：FP32数值转换成FP16，涉及舍入
viewEasA：把elem位模式放进acc通路，不做普通数值转换
viewAasE：从acc通路取回elem位模式，不做普通数值转换
```

当前 `viewEasA/viewAasE` 只是 C++ 强制类型转换，最终必须按 Chisel 位语义重写。

`attentionScale` 在阵列规模固定时应改成常量或小型查找表。例如 `SA_ROWS=4` 时，软件提前计算并写成准确常量，FPGA 不需要现场计算 `log2` 和 `sqrt`。

## 5.6 `pe.cpp` 和 `cmp.cpp`

这两个文件的控制和状态更新框架已经比较接近 HLS，可优先综合。

需要做的修改：

1. 给小函数增加 `INLINE`，避免每个 PE 变成带独立握手的层次；
2. 使用最终 `elem_t/acc_t`；
3. 检查无效输入时的 `bits` 是否会进入真实算术单元；
4. 对照 Chisel 验证 NaN、负无穷、相等值比较和舍入；
5. 不要在 PE 内使用动态循环或共享的可变全局变量。

示意：

```cpp
void pe_step(...){
#pragma HLS INLINE
    // 原有逻辑
}
```

`INLINE` 的作用是让 `systolic_array_step` 展开循环后，每个位置生成自己的 PE 组合逻辑。

## 5.7 `delayer.cpp`

Delayer 的循环次数是编译期常量，适合完全展开：

```cpp
LANE_LOOP:
for(int lane=0; lane<SA_ROWS; ++lane){
#pragma HLS UNROLL
    ...
}
```

内部寄存器数组也需要完全分割，否则 HLS 可能把不同 lane 放进同一个 RAM，导致一拍不能全部读写。

当前 `out_delay_pipe[rows][rows]` 只使用下三角。第一版可以保持方阵，依靠综合器删除未使用部分；资源不理想时再改成更紧凑的三角寄存器表示。

## 5.8 `systolic_array.cpp`

这是最关键的并行化位置。

当前代码：

```cpp
for(int row=0; row<SA_ROWS; ++row){
    for(int col=0; col<SA_COLS; ++col){
        pe_step(...);
    }
}
```

普通 C++ 的意思是依次调用；如果 HLS 不展开，可能只生成一个 PE 并重复使用 `SA_ROWS*SA_COLS` 次。要得到真正的二维阵列，必须完全展开：

```cpp
ROW_LOOP:
for(int row=0; row<SA_ROWS; ++row){
#pragma HLS UNROLL
    COL_LOOP:
    for(int col=0; col<SA_COLS; ++col){
#pragma HLS UNROLL
        pe_step(...);
    }
}
```

CMP 列循环同样要完全展开。

这里通常使用 `UNROLL`，不是在 PE 网格内部使用 `DATAFLOW`：

- `UNROLL`：复制出多个 PE/CMP，形成空间并行硬件；
- `PIPELINE`：让循环不同迭代按固定 II 重叠执行；
- `DATAFLOW`：让较大的生产者/消费者函数通过 stream/FIFO 并行运行。

只有当 InputDelayer、SA、OutputDelayer、Accumulator 被改成持续处理 stream 的独立任务后，才考虑在它们之间使用 `DATAFLOW`。

## 5.9 `accumulator.cpp`

列循环应完全展开，使每列有独立计算路径：

```cpp
ACC_COL_LOOP:
for(int col=0; col<SA_COLS; ++col){
#pragma HLS UNROLL
    ...
}
```

当前 reciprocal 只是“等待若干拍，最后一拍才执行一次除法”的 dummy。它没有表示一个真正持续工作的多周期除法器。必须选择一种实现：

1. 直接让 HLS 生成浮点除法器，并接受工具报告的 latency/II；
2. 使用厂商 reciprocal IP；
3. 使用 LUT 给初值，再用 Newton-Raphson 迭代；
4. 如果输入范围有限，使用分段近似。

选择后要删除假的 busy 倒计时，或者让 busy/valid 真正连接到多周期运算单元的启动和完成信号。

## 5.10 `instruction.hpp`

当前文件暂时不要作为第一批综合目标。需要先完成以下检查：

- 每个字段是否与 Chisel 位宽完全一致；
- C++ 结构体内存布局不能直接当作指令编码；
- 解码时应从 `ap_uint<指令总位宽>` 使用位切片得到字段；
- signed stride 必须做符号扩展；
- testbench 应使用已知机器码验证解码结果。

也就是说，不能用 `reinterpret_cast<MatrixInstruction*>` 直接解释 AXI 收到的字节。

---

## 6. 还没有编写、但阶段一必须补上的模块

当前已经有 PE、CMP、Delayer、SA 和 Accumulator，但一个可运行计算核还缺少：

### 6.1 `banked_sram.hpp/.cpp`

建议物理组织：

```cpp
elem_t spad[SA_ROWS][SPAD_ROWS];
acc_t accumulator_ram[SA_COLS][ACC_ROWS];
```

第一维表示 bank，并完全分割；每个 bank 内部绑定到 BRAM/URAM：

```cpp
#pragma HLS ARRAY_PARTITION variable=spad complete dim=1
#pragma HLS BIND_STORAGE variable=spad type=ram_2p impl=bram
```

必须明确：

- 整行读如何同时访问所有 bank；
- DMA 窄写如何选择一个 bank；
- 同拍读写冲突如何处理；
- HLS RAM 的读延迟是否为一拍；
- 地址和 sub-bank 编号如何从 DMA 地址得到。

### 6.2 `execution_plan.hpp/.cpp`

不要在 FPGA 中动态创建 `std::vector` 控制计划。应把计划变成：

- 固定 ROM 表；或
- 根据计数器和范围比较直接生成控制信号。

第一版推荐 ROM/查表，因为最容易和 Chisel 的每拍控制表逐项比较。

### 6.3 `matrix_engine_controller.hpp/.cpp`

至少需要保存：

- 当前 Matrix 指令；
- `computeTimer`、`accumTimer`；
- scratchpad 和 accumulator 地址；
- busy/done；
- semaphore release 信息。

控制器每拍输出 PE、CMP、Accumulator、SRAM 的控制。它完成后，才有真正的阶段一 `fsa_core_top`。

### 6.4 `fsa_core_top.cpp`

阶段一顶层建议只暴露：

```text
Matrix指令输入
Scratchpad写入口（测试或以后DMA使用）
Accumulator读出口
busy/done
reset
```

不要在阶段一立即加入完整外部内存 AXI master。先让计算核只访问片上存储，DMA 留到阶段二。

---

## 7. HLS pragma 应该放在哪里

建议从少到多添加，不要第一天就到处写 pragma。

| 位置 | 第一批建议 | 目的 |
|---|---|---|
| `pe_step/cmp_step` | `INLINE` | 允许SA展开形成独立单元 |
| Delayer lane/stage循环 | `UNROLL` | 同拍移动所有延迟通道 |
| SA PE/CMP循环 | `UNROLL` | 生成真实二维阵列 |
| Accumulator列循环 | `UNROLL` | 每列并行计算 |
| SA和Accumulator状态数组 | `ARRAY_PARTITION complete` | 避免单RAM端口限制并行 |
| Scratchpad/Accumulator RAM | `BIND_STORAGE` | 映射到BRAM/URAM |
| 处理连续周期的顶层循环 | `PIPELINE II=1` | 尝试做到每拍接受一次操作 |
| 独立stream任务之间 | `DATAFLOW` | 让任务通过FIFO并行 |

添加 pragma 后必须看综合报告。`#pragma HLS PIPELINE II=1` 只是目标，不保证工具一定达到 II=1。报告会说明是 RAM 端口、数据依赖还是浮点运算延迟阻止了 II=1。

---

## 8. 推荐的迁移顺序

### 第一步：冻结当前原生 C++ 参考结果

- 保证 `run_test.cmd all` 全部通过；
- 保存 PE、CMP、Delayer 和 2x2 QK 的期望结果；
- 不删除 `current/next` 版本；
- 给每个已知 TODO 建立清单。

### 第二步：建立 HLS 工具最小工程

- 只综合一个整数加法或最简单的 `pe_step` 包装；
- 确认头文件路径、器件型号、目标时钟和脚本可以工作；
- 建立 `run_csim.tcl/run_synth.tcl`，避免每次手工点击 GUI。

### 第三步：替换精确位宽和算术

- 先替换地址、计数器和命令位宽；
- 再替换 `elem_t`；
- 分别验证 MAC、转换、比较、PWL exp2、reciprocal；
- 算术没有验证前，不要用完整 FlashAttention 结果掩盖局部误差。

### 第四步：分别综合小模块

按以下顺序：

```text
PE
CMP
InputDelayer
OutputDelayer
Accumulator（暂时不含reciprocal）
reciprocal
SystolicArray
```

每个模块检查：

- C Simulation 是否通过；
- RTL latency 是多少；
- II 是否符合控制计划；
- DSP、LUT、FF、BRAM 使用量；
- 是否出现意外的 AXI/memory 端口；
- 是否出现一个 PE 被循环复用，而不是生成 `SA_ROWS*SA_COLS` 个 PE。

### 第五步：加入片上存储和控制器

- 实现 banked SRAM；
- 把 ExecutionPlan 变成 ROM/控制生成逻辑；
- 实现 MatrixEngineController；
- 建立阶段一 `fsa_core_top`；
- 用小矩阵完成从“写入 scratchpad”到“读出 accumulator RAM”的闭环。

### 第六步：加入阶段二 DMA 和 AXI

- 指令解码；
- semaphore/fence；
- DMA request partitioner；
- LoadQueue/StoreQueue；
- AXI master；
- 系统顶层和中断/状态寄存器。

阶段二开始后，不应重新改变 PE/CMP 的算法接口；外围只通过阶段一预留端口连接。

---

## 9. 从 HLS IP 到 FPGA 还要做什么

HLS 综合结束只会得到 RTL IP，还不是可下载的比特流。后面还需要：

1. 在 Vivado/Quartus 中创建 FPGA 工程；
2. 加入 HLS 导出的 IP；
3. 连接时钟和复位；
4. 连接 AXI-Lite 控制接口；
5. 连接 AXI master 到 DDR 控制器；
6. 连接 BRAM/URAM 或让 HLS IP 内部推断；
7. 添加地址映射；
8. 添加时钟、引脚等约束；
9. 运行综合、实现和时序检查；
10. 生成 bitstream；
11. 编写主机程序发送指令、搬入 Q/K/V、启动计算并读回结果；
12. 必要时加入 ILA/SignalTap 观察 valid、地址、状态机和错误标志。

阶段一调试时，建议先用 AXI-Lite 或简单 BRAM 接口写入很小的测试数据，不要一开始就同时调试 DMA、计算核和主机驱动。

---

## 10. 功能验证应该分成四层

### 层一：普通 C++ 单元测试

继续使用现在的：

```powershell
.\run_test.cmd all
```

它速度最快，用于发现公式、状态更新和拍数错误。

### 层二：HLS C Simulation

使用 HLS 编译器编译真正的 top function。输入和预期结果尽量复用普通 C++ 测试向量。

### 层三：C/RTL Co-simulation

HLS 工具把综合后的 RTL 和 C testbench 一起运行。这里主要检查：

- RTL 输出数值；
- latency；
- valid/ready；
- reset；
- 连续输入时的 II。

### 层四：上板验证

先使用固定 2x2 或 4x4 小矩阵，再使用随机数据。每次同时保存：

- 主机输入；
- 软件参考结果；
- FPGA 输出；
- HLS/Vivado 报告；
- ILA 波形或关键状态寄存器。

出现错误时按下面顺序缩小范围：

```text
主机/DDR数据是否正确
    ↓
DMA地址和stride是否正确
    ↓
scratchpad内容是否正确
    ↓
InputDelayer时序是否正确
    ↓
PE/CMP/OutputDelayer是否正确
    ↓
Accumulator和写回是否正确
```

---

## 11. 当前最需要先解决的硬伤

在第一次正式 HLS 综合前，至少处理下面的问题：

- [ ] `elem_t` 不再用 FP32 `float` 冒充 FP16；
- [ ] `cvtAtoE/viewEasA/viewAasE` 与 Chisel 位语义一致；
- [ ] PWL slope/intercept 与原项目逐项核对；
- [ ] `accExp2PWL()` 不再直接调用 `std::exp2()` 占位；
- [ ] `attentionScale()` 改成预计算常量或 LUT；
- [ ] reciprocal 不再使用假的 busy 倒计时；
- [ ] `reciprocalLatency` 不再等于 `-1`；
- [ ] SA 的 PE/CMP 循环完全展开；
- [ ] SA 数据 Pipe 和 PE 状态数组完全 partition；
- [ ] Accumulator 列循环完全展开；
- [ ] banked SRAM 明确映射到 BRAM/URAM；
- [ ] 建立不暴露 `current/next` 的 HLS top；
- [ ] 建立 HLS C Simulation 和综合脚本；
- [ ] 查看综合报告，而不是只看“综合成功”。

---

## 12. 一个实用的判断标准

每完成一个模块，都问下面四个问题：

1. **功能对不对？** 与 Chisel/普通 C++ 的数值和拍数一致吗？
2. **硬件是不是我想要的结构？** SA 是否真的生成多个 PE，存储是否真的进 BRAM？
3. **速度够不够？** latency、II 和目标时钟是多少？
4. **资源能不能接受？** DSP、LUT、FF、BRAM/URAM 是否超过器件预算？

只有四个问题都回答清楚，才能说一个模块已经从“C++ 能运行”迁移成“FPGA 上可用的 HLS 硬件”。
