# PE Raw FMA 时序修复候选：去除 sticky 掩码进位链

日期：2026-09-20。范围：第二阶段、hop=8 的 PE 内联 Raw FMA；不进入第三阶段。

## 依据与选择

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

## 修改

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

## 本地验证

- 独立 `pe_raw_fma_top`：30,000 组随机 MAC、24,480 组定向指数对齐向量、16 组 IEEE 定向向量、136 组 exp2 向量，全部通过。
- 对齐向量覆盖 FP32 全部有限指数、稀疏/稠密尾数、正负号、FP16 非规格化数，以及零、小于 27 和大于等于 27 的对齐位移；golden 使用独立 `std::fma`，不调用内部被测 helper。
- 4x2、4x4、8x4 完整 causal/non-causal attention 和 Accumulator PWL 本地 C++ 回归：通过。

本地没有 Vitis。本次没有新 C 综合或 RTL CoSim 结果，不把历史报告当作当前候选的性能证明。

## 用户下一次 Vitis 验收

入口保持 `./run_hls.sh fsa_stream`，检查：

- HLS 估算周期不超过 7.300 ns，消除相关时序警告；不是只看是否小于 10 ns。
- SA 主循环 II=1、hop=8 的真实依赖正确，Tile interval 不超过 134；latency 与 142--143 拍检查点比较。
- 单 4x4 SA、16 条 PE 11x11 乘法通路、4 CMP、4 Accumulator lane；顶层 DSP44、BRAM20，不以复制算术换时序。
- DMA Q/K/V II=1，Accumulator 10/1，Scratchpad/Delayer 不退化；记录 LUT/FF 差异。
- CSim 和 RTL CoSim 正确、无死锁；比较 1785/1317 cycles 检查点的端到端周期。

若仍不达标，根据新的关键路径选择下一处组合优化或显式流水重定时；不要在未确认 hop=8 反馈时序的情况下直接增加乘法流水拍。第二阶段完成前不开始第三阶段或 150/200 MHz 修改。
