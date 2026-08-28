# FSA 系统级顶层模块编写指南

## 1. 文档目标

本文说明如何在当前 `FSA_HLS` 工程中新增一个系统级 HLS 顶层，把以下模块按原 Chisel `FSA.scala` 的数据通路连接起来：

- Scratchpad SRAM；
- InputDelayer；
- SystolicArray，其中已经包含 PE 阵列和 CMP 阵列；
- OutputDelayer；
- Accumulator；
- Accumulator SRAM。

本文暂时不实现：

- `MatrixEngineController`；
- `ExecutionPlan`；
- MatrixInstruction 解码；
- 双 FSM 指令重叠；
- DMA、AXI 和 semaphore。

所有逐逻辑拍控制信号、SRAM 测试数据和常量都由 testbench 直接提供。第一阶段目标是验证各计算模块、延迟网络和 SRAM 读改写时序能否作为一个整体正确工作。

> 本文给出的代码是推荐实现骨架。真正加入工程时应保持当前公共类型、接口命名和注释风格，并在 Vitis HLS 2024.2 中完成 C Simulation、C Synthesis 和 C/RTL Co-simulation。

---

## 2. Chisel 中的目标连接

`FSA-main/src/main/scala/fsa/FSA.scala` 中的核心连接可以概括为：

```text
                            cmp_ctrl / pe_ctrl
                                   │
                                   v
Scratchpad SRAM -> InputDelayer -> SystolicArray -> OutputDelayer
      ^                                  │                │
      │                                  │                v
  测试写入端口                         PE + CMP       Accumulator
                                                           │  ^
                                                           v  │
                                                     Accumulator SRAM
                                                       RMW读写回路
```

具体数据通路为：

```text
spRAM.fullRead.data
    -> InputDelayer.in.data
    -> SystolicArray.pe_data

SystolicArray.acc_out.bits
    -> OutputDelayer.in
    -> Accumulator.sa_in

accRAM.fullRead.data
    -> Accumulator.sram_in

Accumulator.sram_out
    -> accRAM.fullWrite.data
```

PE 和 CMP 不应在系统顶层中再次作为独立顶层调用。它们已经由：

```cpp
fsa::systolic_array_step(...)
```

统一实例化和连接。

---

## 3. 为什么不能直接调用已有的独立顶层

不建议在系统级顶层中直接写：

```cpp
sp_ram_top(...);
input_delayer_top(...);
systolic_array_top(...);
output_delayer_top(...);
accumulator_top(...);
acc_ram_top(...);
```

原因如下：

1. 每个现有顶层都有自己的 `static` 状态，系统级顶层无法统一控制状态提交顺序。
2. 每个现有顶层都声明了 `ap_ctrl_hs`，嵌套调用后会形成多层事务控制，而不是 Chisel 中同一逻辑拍内的连线。
3. 顶层接口 pragma 只应出现在真正对外的系统顶层，不能把多个已有 IP 顶层当作普通组合函数拼接。
4. SRAM 读响应、Accumulator 结果和 RMW 写回之间存在同拍组合连接及跨拍元数据对齐，必须在同一个系统状态机中表达。

正确方法是系统顶层统一保存所有状态，并直接调用核心 step 函数：

```cpp
fsa::sp_ram_step(...);
fsa::input_delayer_step(...);
fsa::systolic_array_step(...);
fsa::output_delayer_step(...);
fsa::accumulator_step(...);
fsa::acc_ram_step(...);
```

---

## 4. 本阶段采用的时间模型

### 4.1 一个顶层调用表示一个逻辑 step

系统级顶层仍建议使用：

```cpp
#pragma HLS INTERFACE ap_ctrl_hs port=return
```

testbench 每调用一次 `fsa_core_top()`，表示所有内部模块推进一个 Chisel 逻辑拍：

```text
读取 current 状态
    -> 计算本逻辑拍输出
    -> 生成 next 状态
    -> 统一提交 next
```

### 4.2 逻辑拍不等于一个物理时钟周期

当前独立模块的综合结果中：

- SystolicArray 顶层不是单周期组合逻辑；
- PE 浮点数据通路存在多拍流水；
- Accumulator 的 PWL 和 reciprocal 也存在多拍事务。

因此系统顶层的一次 `ap_start -> ap_done` 事务可能占用很多物理时钟周期。这里的“一个 step”只是软件和 Chisel 行为模型中的一次状态推进。

这个模型适合：

- C Simulation；
- C/RTL 行为一致性检查；
- SRAM、Delayer、SA 和 Accumulator 的端到端功能测试；
- 探索单体 HLS 的资源与时序。

它暂时不能证明系统已经实现“所有子模块每个物理时钟同时推进”的最终 RTL。最终若要严格复现 Chisel 的逐物理拍并行结构，需要进一步评估 `ap_ctrl_none`、显式 valid/ready、DATAFLOW，或在 Vivado 中连接多个独立 RTL/IP。

---

## 5. 建议新增的文件

```text
include/fsa/hls/fsa_core_top.hpp
src/hls/fsa_core_top.cpp
tests/hls/test_fsa_core_top.cpp
hls/fsa_core/run_hls.tcl
```

可选地修改：

```text
run_hls.sh
```

为它增加 `fsa_core` 入口。

本阶段不需要修改：

```text
FSA-main/
include/fsa/instruction.hpp
MatrixEngineController
ExecutionPlan
```

---

## 6. 系统级输入输出接口设计

### 6.1 Controller 输出的替代接口

虽然本阶段不实现 Controller，但 testbench 仍需直接提供 Controller 原本产生的逐拍信号：

```cpp
namespace fsa{

struct SpReadRequest{
    bool valid = false;
    bool is_constant = false;
    sram_address_t addr = 0;
    bool rev_sram_out = false;
    bool delay_sram_out = false;
    bool rev_delayer_out = false;
};

struct AccReadRequest{
    bool valid = false;
    bool is_constant = false;
    sram_address_t addr = 0;
    bool rmw = false;
};

}  // namespace fsa
```

`SpReadRequest` 对应 Chisel 的 `SpRead`，`AccReadRequest` 对应 Chisel 的 `AccRead`。地址步长和请求产生时刻全部由 testbench 决定。

### 6.2 常量由 testbench 直接提供

原 Chisel 顶层内部维护以下常量或计数器：

- `ONE`；
- `attentionScale`；
- 每段 exp2 PWL slope；
- Accumulator 侧的 `ZERO`；
- exp2 slope 计数器。

由于 Controller 和 ExecutionPlan 暂未迁移，本阶段可以让 testbench 在发出常量请求时直接给出本次常量值：

```cpp
elem_t sp_constant_value{};
acc_t acc_constant_value{};
```

系统顶层必须在请求拍锁存常量值，使它与下一拍 SRAM 响应时刻对齐。不能在响应拍重新读取 testbench 当前端口，因为 testbench 可能已经切换到下一条请求。

### 6.3 Scratchpad 测试写入端口

为了按真实 BankedSRAM 路径预装 Q、K、V，建议暴露现有窄写端口：

```cpp
bool spad_write_valid[nMemPorts]{};
sram_address_t spad_write_addr[nMemPorts]{};
sub_bank_index_t<SPAD_SUB_BANKS>
    spad_write_sub_bank[nMemPorts]{};
elem_t spad_write_data
    [nMemPorts][SA_ROWS/SPAD_SUB_BANKS]{};
```

不要在系统顶层新增直接访问 `state.sp_ram.banks` 的“后门”。否则测试虽然更容易写，但不能覆盖真实 SRAM 地址、sub-bank 和 ready 行为。

### 6.4 Accumulator SRAM 测试读出端口

建议暴露现有窄读端口：

```cpp
bool acc_dma_read_valid[nMemPorts]{};
sram_address_t acc_dma_read_addr[nMemPorts]{};
sub_bank_index_t<ACC_SUB_BANKS>
    acc_dma_read_sub_bank[nMemPorts]{};
```

输出提供：

```cpp
bool acc_dma_read_ready[nMemPorts]{};
bool acc_dma_response_valid[nMemPorts]{};
acc_t acc_dma_read_data
    [nMemPorts*ACC_DMA_READ_ELEMENTS_PER_PORT]{};
```

现有 `BankedSRAMIO` 没有单独的 response-valid，因此系统状态可以把实际握手的 `valid && ready` 延迟一拍，专门供 testbench 判断当前输出数据是否有效。

### 6.5 推荐的完整接口骨架

建议在 `include/fsa/hls/fsa_core_top.hpp` 中定义：

```cpp
#ifndef FSA_CORE_TOP_HPP
#define FSA_CORE_TOP_HPP

#include "fsa/banked_sram.hpp"
#include "fsa/control.hpp"
#include "fsa/delayer.hpp"
#include "fsa/types.hpp"

namespace fsa{

struct SpReadRequest{
    bool valid = false;
    bool is_constant = false;
    sram_address_t addr = 0;
    bool rev_sram_out = false;
    bool delay_sram_out = false;
    bool rev_delayer_out = false;
};

struct AccReadRequest{
    bool valid = false;
    bool is_constant = false;
    sram_address_t addr = 0;
    bool rmw = false;
};

struct FsaCoreTopInput{
    bool reset = false;

    SpReadRequest sp_read{};
    AccReadRequest acc_read{};

    ValidData<CmpControl> cmp_ctrl{};
    ValidData<PECtrl> pe_ctrl[SA_ROWS]{};
    ValidData<AccumulatorControl> acc_ctrl{};

    elem_t sp_constant_value{};
    acc_t acc_constant_value{};

    bool spad_write_valid[nMemPorts]{};
    sram_address_t spad_write_addr[nMemPorts]{};
    sub_bank_index_t<SPAD_SUB_BANKS>
        spad_write_sub_bank[nMemPorts]{};
    elem_t spad_write_data
        [nMemPorts][SA_ROWS/SPAD_SUB_BANKS]{};

    bool acc_dma_read_valid[nMemPorts]{};
    sram_address_t acc_dma_read_addr[nMemPorts]{};
    sub_bank_index_t<ACC_SUB_BANKS>
        acc_dma_read_sub_bank[nMemPorts]{};
};

struct FsaCoreTopOutput{
    bool sp_read_ready = false;
    bool acc_read_ready = false;
    bool acc_write_ready = false;

    bool spad_write_ready[nMemPorts]{};
    bool acc_dma_read_ready[nMemPorts]{};
    bool acc_dma_response_valid[nMemPorts]{};
    acc_t acc_dma_read_data
        [nMemPorts*ACC_DMA_READ_ELEMENTS_PER_PORT]{};

    // 以下字段主要用于联合测试失败时定位具体逻辑拍。
    ElemVector delayer_out{};
    AccVector aligned_sa_out{};
    AccVector accumulator_out{};
    bool acc_write_valid = false;
    sram_address_t acc_write_addr = 0;
};

}  // namespace fsa

void fsa_core_top(
    const fsa::FsaCoreTopInput& input,
    fsa::FsaCoreTopOutput& output
);

#endif  // FSA_CORE_TOP_HPP
```

调试输出会增加顶层端口位宽，但能显著降低集成初期的定位成本。功能稳定后，可将 `delayer_out`、`aligned_sa_out` 和 `accumulator_out` 放到条件编译或专用 debug top 中。

---

## 7. 系统级状态

### 7.1 模块状态

系统顶层至少需要保存：

```cpp
struct FsaCoreState{
    SpRAMState sp_ram{};
    ElemInputDelayerState input_delayer{};
    SystolicArrayState sa{};
    OutputDelayerState output_delayer{};
    AccumulatorState accumulator{};
    AccRAMState acc_ram{};

    // Scratchpad读请求到InputDelayer之间的一拍元数据。
    bool sp_response_valid = false;
    bool sp_response_is_constant = false;
    bool sp_rev_input = false;
    bool sp_delay_output = false;
    bool sp_rev_output = false;
    elem_t sp_constant_value{};

    // Accumulator SRAM响应选择。
    bool acc_response_valid = false;
    bool acc_response_is_constant = false;
    acc_t acc_constant_value{};

    // 上一拍acc_read产生的RMW写回控制。
    bool acc_write_valid = false;
    sram_address_t acc_write_addr = 0;

    // 给testbench使用的窄读响应有效标志。
    bool acc_dma_response_valid[nMemPorts]{};
};
```

### 7.2 为什么要额外保存元数据

SRAM 读数据晚一拍返回。只有数据延迟而控制不延迟，会产生以下错误：

```text
第 t 拍请求地址 A，rmw=true
第 t+1 拍请求地址 B，rmw=false

错误实现：
    A的数据与B的rmw/地址组合

正确实现：
    A的数据与A的rmw/地址组合
```

所以必须把以下字段与读请求一起延迟：

- response valid；
- `is_constant`；
- 常量值；
- InputDelayer 布局控制；
- `rmw`；
- RMW 写回地址。

---

## 8. 一个逻辑 step 的正确调用顺序

推荐顺序如下：

```text
1. 处理Scratchpad本拍读请求和测试写请求
2. 取得Scratchpad上一拍读响应
3. 根据延迟后的is_constant选择SRAM数据或常量
4. 推进InputDelayer
5. 推进SystolicArray（PE和CMP）
6. 推进OutputDelayer
7. 取得Accumulator SRAM上一拍读响应
8. 推进Accumulator
9. 用Accumulator本拍输出执行上一拍RMW请求的写回
10. 处理Accumulator SRAM本拍新读请求和测试窄读请求
11. 提交Delayer、SA、Accumulator的next状态
12. 锁存本拍请求元数据，供下一逻辑step使用
```

最关键的是第 7～10 步。Accumulator 必须先使用当前响应计算出 `sram_out`，然后同一个逻辑 step 内把它作为 `accRAM.fullWrite.data`。

---

## 9. 系统顶层实现示例

以下代码展示 `src/hls/fsa_core_top.cpp` 的核心结构。为了突出连接关系，省略了一部分重复的数组 pragma 和 reset 输出清零代码。

```cpp
#include "fsa/hls/fsa_core_top.hpp"

#include "fsa/accumulator.hpp"
#include "fsa/banked_sram.hpp"
#include "fsa/delayer.hpp"
#include "fsa/systolic_array.hpp"

namespace{

struct FsaCoreState{
    fsa::SpRAMState sp_ram{};
    fsa::ElemInputDelayerState input_delayer{};
    fsa::SystolicArrayState sa{};
    fsa::OutputDelayerState output_delayer{};
    fsa::AccumulatorState accumulator{};
    fsa::AccRAMState acc_ram{};

    bool sp_response_valid = false;
    bool sp_response_is_constant = false;
    bool sp_rev_input = false;
    bool sp_delay_output = false;
    bool sp_rev_output = false;
    fsa::elem_t sp_constant_value{};

    bool acc_response_valid = false;
    bool acc_response_is_constant = false;
    fsa::acc_t acc_constant_value{};
    bool acc_write_valid = false;
    fsa::sram_address_t acc_write_addr = 0;

    bool acc_dma_response_valid[fsa::nMemPorts]{};
};

void resetCoreState(FsaCoreState& state){
    #pragma HLS INLINE

    fsa::reset_sp_ram_state(state.sp_ram);
    fsa::reset_input_delayer_state(state.input_delayer);
    fsa::reset_systolic_array_state(state.sa);
    fsa::reset_output_delayer_state(state.output_delayer);
    fsa::reset_accumulator_state(state.accumulator);
    fsa::reset_acc_ram_state(state.acc_ram);

    state.sp_response_valid = false;
    state.sp_response_is_constant = false;
    state.sp_rev_input = false;
    state.sp_delay_output = false;
    state.sp_rev_output = false;
    state.sp_constant_value = fsa::elem_t{};

    state.acc_response_valid = false;
    state.acc_response_is_constant = false;
    state.acc_constant_value = fsa::acc_t{};
    state.acc_write_valid = false;
    state.acc_write_addr = 0;

    for(int port=0; port<fsa::nMemPorts; ++port){
        #pragma HLS UNROLL
        state.acc_dma_response_valid[port] = false;
    }
}

}  // namespace

void fsa_core_top(
    const fsa::FsaCoreTopInput& input,
    fsa::FsaCoreTopOutput& output
){
    #pragma HLS INTERFACE ap_ctrl_hs port=return
    #pragma HLS AGGREGATE variable=input compact=bit
    #pragma HLS AGGREGATE variable=output compact=bit
    #pragma HLS INTERFACE ap_none port=input
    #pragma HLS INTERFACE ap_none port=output

    static FsaCoreState current{};

    // 系统顶层必须重新声明关键状态的分割约束。
    #pragma HLS ARRAY_PARTITION variable=current.sa.mesh complete dim=0
    #pragma HLS ARRAY_PARTITION variable=current.sa.cmp_array complete dim=1
    #pragma HLS ARRAY_PARTITION variable=current.accumulator.scale complete dim=1
    #pragma HLS ARRAY_PARTITION variable=current.accumulator.reciprocal complete dim=1
    // banks分割由内联的bankedSRAMStep统一声明，避免在系统顶层
    // 对嵌套结构体成员重复应用数组优化指令。

    if(input.reset){
        resetCoreState(current);
        output = fsa::FsaCoreTopOutput{};
        return;
    }

    // ------------------------------------------------------------
    // 1. Scratchpad：本拍请求，同时返回上一拍full-read数据。
    // ------------------------------------------------------------
    fsa::SpRAMIO sp_ram_io{};
    sp_ram_io.fullRead[0].valid =
        input.sp_read.valid && !input.sp_read.is_constant;
    sp_ram_io.fullRead[0].addr = input.sp_read.addr;
    sp_ram_io.fullRead[0].setFullMask();

    for(int port=0; port<fsa::nMemPorts; ++port){
        #pragma HLS UNROLL
        sp_ram_io.narrowWrite[port].valid =
            input.spad_write_valid[port];
        sp_ram_io.narrowWrite[port].addr =
            input.spad_write_addr[port];
        sp_ram_io.narrowWrite[port].subBankIdx =
            input.spad_write_sub_bank[port];

        for(int element=0;
                element<fsa::SA_ROWS/fsa::SPAD_SUB_BANKS;
                ++element){
            #pragma HLS UNROLL
            sp_ram_io.narrowWrite[port].data[element] =
                input.spad_write_data[port][element];
        }
    }

    fsa::sp_ram_step(current.sp_ram, sp_ram_io);

    output.sp_read_ready = input.sp_read.is_constant
        ? true
        : sp_ram_io.fullRead[0].ready;

    for(int port=0; port<fsa::nMemPorts; ++port){
        #pragma HLS UNROLL
        output.spad_write_ready[port] =
            sp_ram_io.narrowWrite[port].ready;
    }

    // ------------------------------------------------------------
    // 2. InputDelayer：消费上一拍Scratchpad请求的响应。
    // ------------------------------------------------------------
    fsa::InputDelayerIO input_delayer_io{};
    input_delayer_io.in.valid = current.sp_response_valid;
    input_delayer_io.in.bits.rev_input = current.sp_rev_input;
    input_delayer_io.in.bits.delay_output = current.sp_delay_output;
    input_delayer_io.in.bits.rev_output = current.sp_rev_output;

    for(int row=0; row<fsa::SA_ROWS; ++row){
        #pragma HLS UNROLL
        input_delayer_io.in.bits.data[row] =
            current.sp_response_is_constant
                ? current.sp_constant_value
                : sp_ram_io.fullRead[0].data[row];
    }

    fsa::ElemInputDelayerState next_input_delayer{};
    fsa::input_delayer_step(
        current.input_delayer,
        next_input_delayer,
        input_delayer_io
    );

    output.delayer_out = input_delayer_io.out;

    // ------------------------------------------------------------
    // 3. SystolicArray：PE和CMP在这里作为一个阵列推进。
    // ------------------------------------------------------------
    fsa::SystolicArrayIO sa_io{};
    sa_io.pe_data = input_delayer_io.out;
    sa_io.cmp_ctrl = input.cmp_ctrl;

    for(int row=0; row<fsa::SA_ROWS; ++row){
        #pragma HLS UNROLL
        sa_io.pe_ctrl[row] = input.pe_ctrl[row];
    }

    fsa::SystolicArrayState next_sa{};

    #pragma HLS ARRAY_PARTITION variable=next_sa.mesh complete dim=0
    #pragma HLS ARRAY_PARTITION variable=next_sa.cmp_array complete dim=1

    fsa::systolic_array_step(current.sa, next_sa, sa_io);

    // ------------------------------------------------------------
    // 4. OutputDelayer：与Chisel一致，只使用SA输出bits。
    // ------------------------------------------------------------
    fsa::OutputDelayerIO output_delayer_io{};
    for(int col=0; col<fsa::SA_COLS; ++col){
        #pragma HLS UNROLL
        output_delayer_io.in[col] = sa_io.acc_out[col].bits;
    }

    fsa::OutputDelayerState next_output_delayer{};
    fsa::output_delayer_step(
        current.output_delayer,
        next_output_delayer,
        output_delayer_io
    );

    output.aligned_sa_out = output_delayer_io.out;

    // ------------------------------------------------------------
    // 5. Accumulator：使用accRAM当前保存的上一拍full-read响应。
    // ------------------------------------------------------------
    fsa::AccumulatorIO accumulator_io{};
    accumulator_io.ctrl_in = input.acc_ctrl;

    for(int col=0; col<fsa::SA_COLS; ++col){
        #pragma HLS UNROLL
        accumulator_io.sa_in[col] = output_delayer_io.out[col];
        accumulator_io.sram_in[col] =
            current.acc_response_is_constant
                ? current.acc_constant_value
                : current.acc_ram.full_read_data[0][col];
    }

    fsa::AccumulatorState next_accumulator{};

    #pragma HLS ARRAY_PARTITION variable=next_accumulator.scale complete dim=1
    #pragma HLS ARRAY_PARTITION variable=next_accumulator.reciprocal complete dim=1

    fsa::accumulator_step(
        current.accumulator,
        next_accumulator,
        accumulator_io
    );

    output.accumulator_out = accumulator_io.sram_out;

    // ------------------------------------------------------------
    // 6. Accumulator SRAM：本拍新读请求 + 上一拍RMW写回。
    // ------------------------------------------------------------
    fsa::AccRAMIO acc_ram_io{};
    acc_ram_io.fullRead[0].valid =
        input.acc_read.valid && !input.acc_read.is_constant;
    acc_ram_io.fullRead[0].addr = input.acc_read.addr;
    acc_ram_io.fullRead[0].setFullMask();

    acc_ram_io.fullWrite[0].valid = current.acc_write_valid;
    acc_ram_io.fullWrite[0].addr = current.acc_write_addr;
    acc_ram_io.fullWrite[0].setFullMask();

    for(int col=0; col<fsa::SA_COLS; ++col){
        #pragma HLS UNROLL
        acc_ram_io.fullWrite[0].data[col] =
            accumulator_io.sram_out[col];
    }

    for(int port=0; port<fsa::nMemPorts; ++port){
        #pragma HLS UNROLL
        acc_ram_io.narrowRead[port].valid =
            input.acc_dma_read_valid[port];
        acc_ram_io.narrowRead[port].addr =
            input.acc_dma_read_addr[port];
        acc_ram_io.narrowRead[port].subBankIdx =
            input.acc_dma_read_sub_bank[port];
    }

    fsa::acc_ram_step(current.acc_ram, acc_ram_io);

    output.acc_read_ready = input.acc_read.is_constant
        ? true
        : acc_ram_io.fullRead[0].ready;
    output.acc_write_ready = acc_ram_io.fullWrite[0].ready;
    output.acc_write_valid = current.acc_write_valid;
    output.acc_write_addr = current.acc_write_addr;

    for(int port=0; port<fsa::nMemPorts; ++port){
        #pragma HLS UNROLL
        output.acc_dma_read_ready[port] =
            acc_ram_io.narrowRead[port].ready;
        output.acc_dma_response_valid[port] =
            current.acc_dma_response_valid[port];

        for(int element=0;
                element<fsa::SA_COLS/fsa::ACC_SUB_BANKS;
                ++element){
            #pragma HLS UNROLL
            output.acc_dma_read_data[
                accDmaReadDataIndex(port, element)] =
                acc_ram_io.narrowRead[port].data[element];
        }
    }

    // ------------------------------------------------------------
    // 7. 统一提交非SRAM模块的下一状态。
    // SRAM step已经原地提交其同步读响应和bank写入。
    // ------------------------------------------------------------
    current.input_delayer = next_input_delayer;
    current.sa = next_sa;
    current.output_delayer = next_output_delayer;
    current.accumulator = next_accumulator;

    // ------------------------------------------------------------
    // 8. 锁存本拍请求元数据，供下一逻辑step使用。
    // ------------------------------------------------------------
    current.sp_response_valid =
        input.sp_read.valid && output.sp_read_ready;
    current.sp_response_is_constant = input.sp_read.is_constant;
    current.sp_rev_input = input.sp_read.rev_sram_out;
    current.sp_delay_output = input.sp_read.delay_sram_out;
    current.sp_rev_output = input.sp_read.rev_delayer_out;
    current.sp_constant_value = input.sp_constant_value;

    current.acc_response_valid =
        input.acc_read.valid && output.acc_read_ready;
    current.acc_response_is_constant = input.acc_read.is_constant;
    current.acc_constant_value = input.acc_constant_value;

    current.acc_write_valid =
        current.acc_response_valid && input.acc_read.rmw;
    current.acc_write_addr = input.acc_read.addr;

    for(int port=0; port<fsa::nMemPorts; ++port){
        #pragma HLS UNROLL
        current.acc_dma_response_valid[port] =
            input.acc_dma_read_valid[port] &&
            output.acc_dma_read_ready[port];
    }
}
```

### 9.1 上述示例中必须特别复核的一行

示例末尾依次写了：

```cpp
current.acc_response_valid =
    input.acc_read.valid && output.acc_read_ready;

current.acc_write_valid =
    current.acc_response_valid && input.acc_read.rmw;
```

普通 C++ 赋值会立即更新 `current.acc_response_valid`，因此这里得到的是本拍请求是否被接收，逻辑上可以工作。为了让“current 只读、next 只写”的硬件含义更明确，正式实现更推荐先使用局部变量：

```cpp
const bool acc_request_accepted =
    input.acc_read.valid && output.acc_read_ready;

current.acc_response_valid = acc_request_accepted;
current.acc_write_valid =
    acc_request_accepted && input.acc_read.rmw;
```

同理，所有需要同时生成多个 next 字段的控制都建议先计算局部 `accepted`、`fire` 或 `next_*`，再统一赋值。

---

## 10. 复位语义

当前 `reset_sp_ram_state()` 和 `reset_acc_ram_state()` 主要复位同步读响应寄存器，不会把整个 SRAM bank 内容逐项清零。

因此 testbench 必须遵循：

```text
reset
    -> 显式写入本测试会读取的Scratchpad行
    -> 通过Accumulator的RMW路径初始化需要使用的accRAM行
    -> 再开始计算
```

不要假设 reset 后 SRAM 全部为零。若测试需要零初值，应使用 Accumulator 常量 ZERO 和 RMW 写回，或者通过专门的测试初始化阶段写入，而不是依赖未定义的上电内容。

---

## 11. Testbench 编写方法

### 11.1 tick 函数

建议让 testbench 中所有状态推进都经过同一个函数：

```cpp
fsa::FsaCoreTopOutput tick(
    const fsa::FsaCoreTopInput& input
){
    fsa::FsaCoreTopOutput output{};
    fsa_core_top(input, output);
    return output;
}
```

这样每次 `tick()` 就是一拍完整系统状态推进，不能在同一逻辑拍分别调用各子模块顶层。

### 11.2 复位

```cpp
void resetCore(){
    fsa::FsaCoreTopInput input{};
    input.reset = true;
    tick(input);
}
```

### 11.3 预装Scratchpad

以端口 0 写入一行的示意代码：

```cpp
void writeSpadRow(
    const int address,
    const fsa::elem_t row[fsa::SA_ROWS]
){
    for(int sub_bank=0;
            sub_bank<fsa::SPAD_SUB_BANKS;
            ++sub_bank){
        fsa::FsaCoreTopInput input{};
        input.spad_write_valid[0] = true;
        input.spad_write_addr[0] = address;
        input.spad_write_sub_bank[0] = sub_bank;

        for(int element=0;
                element<fsa::SA_ROWS/fsa::SPAD_SUB_BANKS;
                ++element){
            const int row_element =
                sub_bank*(fsa::SA_ROWS/fsa::SPAD_SUB_BANKS)+element;
            input.spad_write_data[0][element] = row[row_element];
        }

        const fsa::FsaCoreTopOutput output = tick(input);
        expect(output.spad_write_ready[0],
               "Scratchpad write was not accepted");
    }
}
```

即使当前配置下 `SPAD_SUB_BANKS` 可能等于 1，也建议保留通用循环，避免以后修改 `SA_ROWS` 或 `beatBytes` 时测试失效。

### 11.4 发出Scratchpad读请求

```cpp
fsa::FsaCoreTopInput input{};
input.sp_read.valid = true;
input.sp_read.addr = k_address;
input.sp_read.rev_sram_out = true;
input.sp_read.delay_sram_out = true;
input.sp_read.rev_delayer_out = true;

const fsa::FsaCoreTopOutput request_output = tick(input);
expect(request_output.sp_read_ready,
       "Scratchpad read was not accepted");

// 下一次tick中，上一拍读出的数据进入InputDelayer。
tick(fsa::FsaCoreTopInput{});
```

PE 和 CMP 控制应当按照被测试的调度，在正确的逻辑 step 同时填入下一次 `tick()` 的输入。不能只发 SRAM 请求而忘记一拍响应延迟。

### 11.5 Accumulator SRAM 的RMW测试

假设地址 0 保存 L：

```cpp
// 第t拍：发起读地址0，并声明返回后需要写回同一地址。
fsa::FsaCoreTopInput read_input{};
read_input.acc_read.valid = true;
read_input.acc_read.addr = 0;
read_input.acc_read.rmw = true;
tick(read_input);

// 第t+1拍：旧L到达Accumulator，同时发出ACC_SA命令。
fsa::FsaCoreTopInput compute_input{};
fsa::AccumulatorControl acc_ctrl{};
acc_ctrl.cmd = fsa::AccumulatorCmd::ACC_SA;
compute_input.acc_ctrl = fsa::make_valid(acc_ctrl);

const fsa::FsaCoreTopOutput compute_output = tick(compute_input);

expect(compute_output.acc_write_valid,
       "RMW writeback was not generated");
expect((int)compute_output.acc_write_addr.to_uint()==0,
       "RMW wrote the wrong row");
```

如果 SA 的对齐输出还没有在第 `t+1` 拍到达，就必须调整 testbench 控制计划，而不能在系统顶层内部偷偷延迟 `acc_ctrl`。顶层只负责复现 Chisel 连线；具体哪一拍发什么命令属于 Controller/ExecutionPlan 或本阶段 testbench 的职责。

### 11.6 第一版端到端测试建议

建议从现有 `tests/hls/test_delayer_sa.cpp` 演进，而不是从空白文件重新发明 SA 控制序列：

1. 把 Q、K 写入系统顶层内部 Scratchpad；
2. 用 `sp_read` 代替 testbench 直接向 InputDelayer 填数据；
3. 保留已经验证过的 `PECtrl`、`CmpControl` 周期；
4. 让 SA 输出自然进入 OutputDelayer；
5. 在一个结果对齐周期发起 accRAM 常量 ZERO 请求和 `ACC_SA`；
6. 使用 RMW 写入 accRAM；
7. 通过 accRAM 窄读端口读出结果；
8. 与独立计算的 `Q*K^T` 或行累加金标准比较。

第一版测试不必立即覆盖完整 FlashAttention。建议按以下顺序逐步增加：

```text
阶段A：Scratchpad -> InputDelayer -> SA -> OutputDelayer
阶段B：阶段A + Accumulator ACC_SA + accRAM RMW
阶段C：增加EXP_S1和EXP_S2
阶段D：增加SET_SCALE和RECIPROCAL
阶段E：完整L/O更新及O/L归一化
```

---

## 12. 测试必须打印的逐拍 trace

系统集成错误大多是错一拍，而不是算术公式本身错误。建议每拍打印：

```text
cycle
sp_read.valid / addr / is_constant / ready
delayer_out
cmp_ctrl
pe_ctrl摘要
sa.acc_out
aligned_sa_out
acc_read.valid / addr / rmw / ready
acc_ctrl
accumulator.sram_in
accumulator.sram_out
acc_write_valid / acc_write_addr
acc_dma_response_valid / data
```

失败信息应包含周期和地址，例如：

```text
[FAIL] cycle 23: acc write addr=2, expected=1
```

不要只在最后打印“结果错误”，否则无法区分是 Delayer、SA、Accumulator 还是 SRAM 对齐问题。

---

## 13. HLS Tcl 示例

建议新增 `hls/fsa_core/run_hls.tcl`：

```tcl
set RUN_CSIM  1
set RUN_COSIM 1
set EXPORT_IP 1

set SCRIPT_DIR [file dirname [file normalize [info script]]]
set PROJECT_ROOT [file normalize [file join $SCRIPT_DIR "../.."]]
set HLS_PROJECT_DIR [file join $SCRIPT_DIR "build"]

open_project -reset $HLS_PROJECT_DIR
set_top fsa_core_top

set CFLAGS "-std=c++14 -I[file join $PROJECT_ROOT include]"

add_files [file join $PROJECT_ROOT "src/hls/fsa_core_top.cpp"] \
    -cflags $CFLAGS

add_files [file join $PROJECT_ROOT "src/core/banked_sram.cpp"] \
    -cflags $CFLAGS
add_files [file join $PROJECT_ROOT "src/core/delayer.cpp"] \
    -cflags $CFLAGS
add_files [file join $PROJECT_ROOT "src/core/systolic_array.cpp"] \
    -cflags $CFLAGS
add_files [file join $PROJECT_ROOT "src/core/pe.cpp"] \
    -cflags $CFLAGS
add_files [file join $PROJECT_ROOT "src/core/cmp.cpp"] \
    -cflags $CFLAGS
add_files [file join $PROJECT_ROOT "src/core/accumulator.cpp"] \
    -cflags $CFLAGS
add_files [file join $PROJECT_ROOT "src/core/arithmetic.cpp"] \
    -cflags $CFLAGS

if {$RUN_CSIM || $RUN_COSIM} {
    add_files -tb \
        [file join $PROJECT_ROOT "tests/hls/test_fsa_core_top.cpp"] \
        -cflags $CFLAGS
}

open_solution -reset "solution1" -flow_target vivado

set_part {xcvu37p_CIV-fsvh2892-2-e}
create_clock -period 10 -name default

if {$RUN_CSIM} {
    csim_design
}

csynth_design

if {$RUN_COSIM} {
    cosim_design -rtl verilog
}

if {$EXPORT_IP} {
    export_design -format ip_catalog -rtl verilog
}

exit
```

第一版不要为了让综合更快而跳过 C Simulation。系统级接口一旦存在一拍错位，直接进入 RTL 综合只会让定位更困难。

---

## 14. `run_hls.sh` 修改示例

在模块选择分支中加入：

```bash
case "$MODULE" in
    pe|cmp|input_delayer|output_delayer|sa|delayer_sa|accumulator|fsa_core)
        ;;
```

然后运行：

```bash
./run_hls.sh fsa_core
```

预期输出目录为：

```text
hls/fsa_core/fsa_core_build/
hls/fsa_core/fsa_core_build.zip
```

---

## 15. HLS pragma 放置原则

### 15.1 系统顶层要重新声明关键数组分割

原独立顶层中的 pragma 不会因为调用核心 step 而自动复制到新系统顶层。至少要复核：

- SA 的 `mesh` 完全分割；
- CMP 数组和控制 pipe 分割；
- PE 上下左右数据 pipe 分割；
- Accumulator 的 `scale` 和 reciprocal 状态分割；
- Scratchpad 和 accRAM 的 bank/sub-bank 维度分割；
- SRAM 行内数据 reshape；
- 顶层输入输出数组分割。

建议直接对照以下现有文件逐项迁移 pragma：

```text
src/hls/systolic_array_top.cpp
src/hls/accumulator_top.cpp
src/hls/banked_sram_top.cpp
```

### 15.2 用内部stage wrapper保留SA资源边界

独立 `systolic_array_top` 使用了顶层 `PIPELINE II=16`，该约束参与形成当前 16 个独立 `peMacUnit`。如果系统顶层直接内联 `systolic_array_step()`，又不提供相应的调度约束，HLS 可能重新跨 PE 共享算术资源。

系统集成时可以增加一个没有外部接口 pragma、但保留函数层次的内部 stage wrapper：

```cpp
void fsa_core_sa_stage(
    const fsa::SystolicArrayState& current,
    fsa::SystolicArrayState& next,
    fsa::SystolicArrayIO& io
){
    #pragma HLS INLINE off
    #pragma HLS PIPELINE II=16

    #pragma HLS ARRAY_PARTITION variable=current.mesh complete dim=0
    #pragma HLS ARRAY_PARTITION variable=next.mesh complete dim=0
    #pragma HLS ARRAY_PARTITION variable=io.pe_ctrl complete dim=1
    #pragma HLS ARRAY_PARTITION variable=io.pe_data complete dim=1
    #pragma HLS ARRAY_PARTITION variable=io.acc_out complete dim=1

    fsa::systolic_array_step(current, next, io);
}
```

系统顶层调用：

```cpp
fsa_core_sa_stage(current.sa, next_sa, sa_io);
```

这不是再次调用一个独立的 `ap_ctrl_hs` 顶层，而是在同一个 HLS 工程内保留明确的 SA 子模块层次和流水约束。

是否需要类似 wrapper 必须以新系统综合报告为准：

- 如果仍有 16 个独立 `peMacUnit`，现有结构已经足够；
- 如果只剩少量共享 MAC，必须恢复 SA stage 边界或增加更明确的实例化约束；
- 不应只因为 C Simulation 正确就认为空间结构正确。

Accumulator 当前已经通过独立 lane 层次和 `FUNCTION_INSTANTIATE` 强制四列实现，系统综合后仍需检查四个 lane 是否保留。

### 15.3 第一版不要盲目添加DATAFLOW

系统中存在：

- SA 跨拍状态；
- Delayer 跨拍状态；
- Accumulator reciprocal 状态；
- accRAM 读改写反馈；
- 同一 `accRAM` 的读、写和测试窄读仲裁。

这些结构不满足最简单的无反馈 producer-consumer DATAFLOW 模型。第一版应先保证普通 `ap_ctrl_hs` 下功能正确，再根据综合报告确定是否拆分 dataflow region。

### 15.4 第一版不要强行把系统顶层PIPELINE到II=1

独立 SA 当前目标 II 不是 1，Accumulator 也有多种变延迟路径。直接在系统顶层添加：

```cpp
#pragma HLS PIPELINE II=1
```

可能导致：

- 大量额外资源复制；
- 无法满足状态依赖；
- 时序严重违例；
- 调度器给出难以解释的 II violation。

第一版先不设置顶层 PIPELINE，记录自然综合结果。功能通过后再根据目标吞吐决定架构，而不是先写一个无法兑现的 II。

---

## 16. SRAM冲突和ready处理

Chisel 顶层假设内部计算访问不会被反压，并通过断言检查 `valid -> ready`。本阶段 testbench 应做同样检查：

```cpp
if(input.sp_read.valid){
    expect(output.sp_read_ready,
           "sp_read was backpressured");
}

if(input.acc_read.valid){
    expect(output.acc_read_ready,
           "acc_read was backpressured");
}

if(output.acc_write_valid){
    expect(output.acc_write_ready,
           "acc RMW write was backpressured");
}
```

测试阶段不要自动重试计算控制信号，因为 PE/CMP/Accumulator 控制已经按拍推进。若请求被拒绝却只重试 SRAM 请求，数据和控制会进一步错位。出现 backpressure 应直接让测试失败并修正冲突计划。

---

## 17. C Simulation验收标准

系统级 C Simulation 至少应证明：

1. reset 后所有计算状态和响应 valid 清零；
2. Scratchpad 窄写后能够通过整行读端口读回；
3. Scratchpad 读数据确实晚一个逻辑 step 进入 InputDelayer；
4. InputDelayer 的反转和阶梯延迟与现有独立测试一致；
5. SA 的 4×4 PE 和 CMP 结果与现有测试一致；
6. OutputDelayer 能重新对齐四列结果；
7. accRAM 读数据晚一个逻辑 step 到达 Accumulator；
8. `rmw=true` 时写回上一拍请求地址；
9. `rmw=false` 时不得写入 accRAM；
10. accRAM 窄读能读出刚才写回的结果；
11. 常量请求不访问 SRAM，但仍保持与 SRAM 响应相同的一拍延迟；
12. 连续访问不同 accRAM 行时不会写错地址；
13. 所有 `valid -> ready` 假设均满足；
14. 最终结果与独立软件金标准一致。

---

## 18. C Synthesis后必须检查的项目

综合成功不等于结构正确。必须检查：

### 18.1 模块层次

- 系统顶层中是否包含 Scratchpad 和 accRAM 存储；
- 是否包含完整 InputDelayer 和 OutputDelayer；
- 是否仍有 16 个独立 PE 运算单元；
- 是否仍有 4 个 CMP 状态/计算通路；
- Accumulator 是否仍有 4 个独立 lane；
- 是否出现意外的跨列或跨 PE 资源共享。

### 18.2 资源

记录：

```text
BRAM_18K
DSP
FF
LUT
URAM
```

整体资源应大致接近各模块资源之和，再加系统级多路器、控制和延迟寄存器。如果资源远小于独立模块之和，应优先检查 HLS 是否重新共享了本应独立的算术单元。

### 18.3 性能

记录：

```text
Estimated Clock Period
Latency min/max
Interval min/max
Pipeline类型
```

系统顶层很可能是变延迟、非流水事务。不要继续沿用独立模块报告中的固定 latency，也不要假设一次逻辑 step 等于一个物理时钟周期。

### 18.4 接口

检查：

- `ap_ctrl_hs` 是否保留；
- packed input/output 位宽是否合理；
- 调试端口是否造成不可接受的顶层位宽；
- 输出采样是否与 `ap_done` 对齐。

---

## 19. C/RTL Co-simulation验收标准

Co-simulation 应复用 C Simulation 的完整逐拍调用序列，并检查：

- Verilog `Pass`；
- 每次事务输出与 C 模型一致；
- reset 后的第一笔 SRAM 响应无脏 valid；
- 连续 RMW 的地址和数据不串行；
- reciprocal 期间外部输入变化不会污染在途状态；
- 四列 Accumulator 结果互不串扰；
- 最终 accRAM 窄读数据与金标准一致。

Co-simulation 的事务 latency 反映物理 HLS 实现，不应拿它直接替换 testbench 中的逻辑 cycle 编号。testbench 的逻辑 cycle 是函数调用次数；Co-sim latency 是每次函数调用内部消耗的 RTL 时钟数。

---

## 20. 常见错误

### 20.1 把当前acc_read地址直接用于写回

错误：

```cpp
acc_ram_io.fullWrite[0].addr = input.acc_read.addr;
```

正确：

```cpp
acc_ram_io.fullWrite[0].addr = current.acc_write_addr;
```

### 20.2 在响应拍读取当前常量输入

错误：

```cpp
accumulator_io.sram_in[col] = input.acc_constant_value;
```

正确：

```cpp
accumulator_io.sram_in[col] = current.acc_constant_value;
```

### 20.3 用SA输出valid控制OutputDelayer输入是否清零

原 Chisel 顶层直接连接 `sa.io.acc_out.map(_.bits)`。如果系统顶层自行按 valid 清零，会改变原数据通路语义和已有控制计划。第一版应保持与 Chisel 一致，只传递 bits。

### 20.4 把PE和CMP再次单独实例化

`systolic_array_step()` 已经包含 PE 和 CMP。再次调用 `pe_top` 或 `cmp_top` 会生成第二套不相干状态。

### 20.5 reset时假设SRAM内容清零

现有 SRAM reset 主要清读响应寄存器。测试所需的 SRAM 行必须显式初始化。

### 20.6 为了低延迟直接把所有函数INLINE

过度 inline 可能让 HLS 跨 PE、跨列或跨模块共享/重排算术资源，破坏期望的空间结构。必须结合实例层次和资源报告判断，而不是只看代码展开形式。

### 20.7 只验证最终结果

最终结果错误时，如果没有逐拍 trace，无法定位到底是：

- SRAM 请求早一拍或晚一拍；
- InputDelayer 布局错误；
- PE/CMP 控制错误；
- OutputDelayer 对齐错误；
- Accumulator 命令错误；
- RMW 写错地址。

系统测试必须保留阶段性观测点。

---

## 21. 推荐实施顺序

### 第一步：只接数据通路，不做完整算法

完成系统接口、状态、reset、Scratchpad 和 accRAM 端口映射。

### 第二步：迁移现有Delayer+SA测试

用系统顶层取代三个独立顶层调用，但暂不启用 Accumulator RMW。

### 第三步：加入Accumulator常量ZERO和单行RMW

验证：

```text
sa_out + 0 -> accRAM[address]
```

### 第四步：连续更新L和O行

验证连续地址、stride由 testbench 产生时的读写对齐。

### 第五步：加入EXP和reciprocal

依次覆盖：

```text
EXP_S1
EXP_S2
ACC_SA
SET_SCALE
RECIPROCAL
ACC
```

### 第六步：完成综合和Co-sim

只有 C Simulation 的逐拍 trace 全部正确后，再运行完整 HLS 流程。

---

## 22. 本阶段完成标准

满足以下条件后，可以认为“无 Controller/ExecutionPlan 的系统级顶层”完成：

- testbench 只调用一个 `fsa_core_top()`；
- SRAM、Delayer、SA、Accumulator 均使用正式核心实现；
- PE/CMP 通过 SystolicArray 集成，没有第二套重复状态；
- Scratchpad 和 accRAM 均通过正式 BankedSRAM 端口访问；
- 所有 SRAM 数据、地址、常量选择和 RMW 元数据按一拍延迟对齐；
- C Simulation 端到端通过；
- C Synthesis 完成且实例数量符合预期；
- 100 MHz HLS 估算时序结果已记录；
- Verilog C/RTL Co-simulation 通过；
- IP 已成功导出；
- 报告明确区分逻辑 step 和物理事务 latency；
- 尚未实现 Controller/ExecutionPlan 的限制已写入报告。

完成这一阶段后，再迁移 Controller 和 ExecutionPlan 时，只需把当前由 testbench 提供的 `sp_read`、`acc_read`、`cmp_ctrl`、`pe_ctrl` 和 `acc_ctrl` 替换为内部控制器输出，核心数据通路和 SRAM 对齐逻辑不应重新设计。
