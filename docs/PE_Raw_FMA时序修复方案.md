# PE Raw FMA 时序修复：直接前导零计数规格化

日期：2026-09-20。范围：第二阶段、hop=8 的 PE 内联 Raw FMA。15:23新build已验收通过，当前进入独立FP32候选3A，见`FP32_Raw_FMA第三阶段方案.md`。

## 当前候选：直接生成规格化移位量

13:12服务器build已验证exp2下溢舍入优化：CSim/RTL CoSim通过，SA II1、Tile143/134、1785/1317 cycles不变，20 BRAM、44 DSP、92204 FF、128641 LUT。估算周期7.337 ns，仍超7.300 ns预算37 ps。剩余关键路径在FMA加减后的幅值选择、零判定、最高位编码、`26-highest`减法、规格化移位与舍入。

本轮只把非零27位幅值的`highestBit27`加`26-highest`改为`low.countLeadingZeros()`，结果用`ap_uint<5>`保存。保留零值提前返回、bit27进位处理、指数调整、guard/round/sticky和最近偶数舍入；不改变其他两个最高位编码器、乘法器、流水约束、hop或外围。

依据与取舍：

- [SoftFloat规格化实现](https://github.com/ucb-bar/berkeley-softfloat-3/blob/master/source/s_normRoundPackToF32.c)采用前导零计数确定规格化移位及指数修正。这是算法参考，不是FPGA时序收益保证；未复制其源代码。
- [AMD位宽传播说明](https://docs.amd.com/r/en-US/ug1399-vitis-hls/Bit-Width-Propagation)解释精确位宽如何传播到硬件。本地`third_party/vitis_hls/include/etc/ap_int_base.h`的`countLeadingZeros()`在综合分支直接调用`__fpga_ctlz`；`utils/x_hls_float_utils.h`也用此方法规格化。因此本轮使用已有HLS接口，不新增手写优先编码树或查表资源。在线位宽页面为2026.1通用说明，2024.2实际结果仍由服务器验证。
- 对非零27位数，最高有效位为`h`时前导零数恰好为`26-h`，范围0..26；5位无符号数可完整表示。转换为`int`后再扣减指数，避免无符号减法改变负指数语义。
- 报告中旧减法为0.707 ns，但不能从7.337直接减去0.707作为预计结果；工具可能重新调度和映射。当前只确认源码去除了该串联表达。

新增4608组公开顶层MAC规格化边界：8组正常/非规格化FP16乘积，对FP32累加数位模式在乘积附近按二次幂及相邻偏移扫描，覆盖精确/深度相消、正负结果和同号进位。golden使用`std::fma`，修改前后均通过。连同原有测试共63836组全部通过；4x2、4x4、8x4完整causal/non-causal attention与Accumulator PWL本地回归也全部通过。

后续15:19综合/15:23 CoSim确认本候选通过：顶层7.300 ns、SA7.272 ns，无HLS时序/II违例，SA II1、Tile143/134、1785/1317 cycles保持；DSP44/BRAM20、FF91252/LUT125809。CSim与RTL CoSim通过。达到第二阶段既定门槛，但无Vivado实现证明；按用户条件授权开始独立3A，正式计算路径不再修改。

## 上一轮候选：exp2 缩放下溢舍入（已验证）

11:07 服务器 build 已验证上一轮对齐 sticky 优化：CSim/RTL CoSim通过，SA II1、Tile143/134、1785/1317 cycles；顶层20 BRAM、44 DSP、91418 FF、138625 LUT。估算周期从7.933改善至7.650 ns，但仍超7.300 ns预算0.350 ns。

关键路径转至`scaleFloatByPowerOfTwo`：指数加减后串联动态mask、halfway计算、余数比较与舍入。本轮只改此函数的下溢舍入表达，保留上一轮对齐sticky修改及全部流水/调度设置。

- 24位有效数拼接24个低位零，形成48位组合临时值；在确认移位量属于1..24后，将移位量窄化为5位。
- 右移后，高24位为商，bit23为guard，bit22..0归约非零为sticky；最近偶数舍入条件为`guard && (sticky || quotient[0])`。
- 对有效数`v = q*2^d+r`，低24位为`r*2^(24-d)`。guard对应余数是否达到半值，sticky区分恰好半值和大于半值，因此与原来的余数/halfway比较逐位等价。
- 保留正常数快速路径、溢出、零/特殊值旁路、移位超过24时带符号零，以及舍入后进位为最小正常数的处理；没有删掉FP32非规格化语义。
- 不增加源码流水级，不改hop=8、DSP乘法latency=1、CMP、Accumulator、DMA、SRAM或Delayer；实际II、latency、实例数和资源仍以新综合为准。

新增4596组公开顶层缩放边界测试：覆盖全部1..24移位、超过24位、各宽度半值附近与奇偶舍入、正常/非规格化交界、正负号、上溢，以及真实PWL FMA后再缩放。golden使用`std::fma`和`std::ldexp`；修改前与修改后均通过。

该轮独立Raw FMA原有30000随机MAC、24480对齐边界、16 IEEE定向、136 exp2，以及新增4596组测试均通过。4x2、4x4、8x4完整attention/Acc PWL本地回归通过；后续13:12 build的验收结果见本文开头。

## 上一轮依据与选择（已完成Vitis检查）

最近两个服务器检查点都通过 CSim 和 RTL CoSim，但均未达到 7.300 ns 的有效 HLS 预算：

| 检查点 | 估算周期 | SA II | Tile latency/interval | 顶层 DSP / LUT |
|---|---:|---:|---:|---:|
| 09-19 23:55，exp2 直接小数域 | 7.933 ns | 1 | 143/134 | 44 / 140033 |
| 09-20 00:27，乘前规格化 | 9.543 ns | 1 | 142/134 | 44 / 149057 |

乘前规格化把 exp2 取余、优先编码和规格化移位串到 DSP 输入前，方向失败。本次恢复 23:55 的未规格化尾数/最低位指数表示，再优化其乘后对齐路径。

23:55 关键路径包含 DSP 乘法、乘积最高位编码、指数对齐及 sticky 计算。旧 sticky 表达式为 `(value & ((1 << shift) - 1)) != 0`，动态掩码引入移位、减一进位链、按位与和非零归约。报告中的相关移位、减法、非零归约延迟分别为 0.740、0.844、0.844 ns；这些数值不是可以直接相加扣除的预计收益，改写后工具会重新调度。

调研结论：

- AMD 的 [LATENCY 文档](https://docs.amd.com/r/2024.2-English/ug1399-vitis-hls/pragma-HLS-latency)说明最小延迟约束可能补入空拍，不能据此保证某条组合路径在指定位置被切断。
- AMD 的 [BIND_OP 文档](https://docs.amd.com/r/2024.2-English/ug1399-vitis-hls/pragma-HLS-bind_op)允许指定单个运算的实现和延迟；它不是对整段组合链任意指定寄存器位置的接口。提高乘法 latency 还需重新验证 hop=8 的反馈调度，因此本次保留 DSP 乘法 latency=1。
- Berkeley [SoftFloat 的 shift-right-jam 实现](https://raw.githubusercontent.com/ucb-bar/berkeley-softfloat-3/master/source/s_shiftRightJam32.c)通过移位后检测丢弃位非零实现 sticky，不需要构造动态减一掩码。本次采用等价的扩展位向量表达，未移植其 C 实现。

## 上一轮修改

只改 `src/stream/pe_raw_fma.cpp` 中的 PE 算术表达：

1. 撤销 FP16/FP32 非规格化输入和 exp2 小数的乘前规格化，恢复乘后 `highestBit22` / `highestBit24`。
2. 将 27 位数拼接 27 个低位零形成 54 位临时组合值，再右移；高 27 位是商，低 27 位是否非零就是 sticky。移位量在边界检查后使用 5 位无符号值。

等价性：对 `1 <= d <= 26`，令 `v = q*2^d + r`。则

```text
(v * 2^27) >> d = q * 2^27 + r * 2^(27-d)
```

高 27 位恰好为 `q`，低 27 位非零当且仅当 `r != 0`，因此最终 `q | (r != 0)` 与原实现逐位相同。`d<=0` 返回原值，`d>=27` 返回非零判定，这两个边界保持不变。

54 位是组合表达的中间宽度，不是新增 54 拍或显式 54 位状态寄存器。实际 LUT/FF 和调度变化必须查看新综合结果。

未修改 SA 调度、hop=8、PE 乘法调用点、CMP、Accumulator、DMA、SRAM、Delayer、器件或时钟约束。没有新增算术单元或流水级的源码请求，也没有放宽依赖；实际硬件实例数仍需综合核验。

## 上一轮本地验证

- 独立 `pe_raw_fma_top`：30,000 组随机 MAC、24,480 组定向指数对齐向量、16 组 IEEE 定向向量、136 组 exp2 向量，全部通过。
- 对齐向量覆盖 FP32 全部有限指数、稀疏/稠密尾数、正负号、FP16 非规格化数，以及零、小于 27 和大于等于 27 的对齐位移；golden 使用独立 `std::fma`，不调用内部被测 helper。
- 4x2、4x4、8x4 完整 causal/non-causal attention 和 Accumulator PWL 本地 C++ 回归：通过。

本地没有 Vitis。sticky和exp2下溢修改分别由11:07和13:12服务器build验证；当前前导零计数候选不能沿用这些build作为性能证明。

## 第二阶段 Vitis 验收门槛（15:23已达到）

入口保持 `./run_hls.sh fsa_stream`，检查：

- HLS 估算周期不超过 7.300 ns，消除相关时序警告；不是只看是否小于 10 ns。
- SA 主循环 II=1、hop=8 的真实依赖正确，Tile interval 不超过 134；latency 与 142--143 拍检查点比较。
- 单 4x4 SA、16 条 PE 11x11 乘法通路、4 CMP、4 Accumulator lane；顶层 DSP44、BRAM20，不以复制算术换时序。
- DMA Q/K/V II=1，Accumulator 10/1，Scratchpad/Delayer 不退化；记录 LUT/FF 差异。
- CSim 和 RTL CoSim 正确、无死锁；比较 1785/1317 cycles 检查点的端到端周期。

上述门槛已由15:23 build达到。下一入口为`./run_hls.sh fp32_raw_fma`验证第三阶段3A；3A通过前不接入Accumulator，不修改CMP/hop4，150/200 MHz仍后置。
