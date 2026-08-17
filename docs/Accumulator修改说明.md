# Accumulator 修改说明（2026-08-17）

本文记录针对以下四项缺口所做的修改：数值边界、四列完全并行、
reciprocal 15 个物理时钟，以及明确的 busy/result_valid。

源码中的本轮关键改动均带有 `MOD:` 注释，便于逐项检索：

```text
rg -n "MOD:" include/fsa src/core src/hls rtl tests
```

## 1. 协议与时序结构

没有把整个 Accumulator 直接改成 `ap_ctrl_none`。原因是原综合报告显示普通
FMA/exp2 路径一次事务需要 20～24 拍；仅添加 `PIPELINE II=1` 不能保证实际
II=1，若约束失败，free-running 接口又没有可靠 backpressure。

本轮采用的结构是：

```text
request_valid/request_ready
          │
          ▼
accumulator_protocol_wrapper
          │ ap_start/ap_ready
          ▼
accumulator_top (ap_ctrl_hs)
          │
          ├─ 普通命令：原事务式浮点路径
          └─ RECIPROCAL：单笔事务内部执行15个固定阶段
                         1初始化 + 13 ITER + 1舍入/写回
```

`accumulator_reciprocal_transaction()` 包含固定 15 次流水循环：

- step 0：锁存四列 `scale` 并初始化恢复除法状态；
- step 1～13：每列每拍产生两个商位；
- step 14：规格化、RNE 舍入，同时写回四列 `scale`。

函数使用 `LATENCY min=15 max=15` 和循环 `PIPELINE II=1`。这两项是综合
约束，不是实测结论。必须在新生成的顶层 csynth 报告和 RTL 波形中确认：

```text
ap_start && ap_ready 所在拍记为第1拍；
第15拍 ap_done=1，wrapper 的 result_valid=1；
result_valid 只保持1拍。
```

## 2. busy/result_valid 和 SRAM 写回语义

新增 `rtl/accumulator_protocol_wrapper.sv`：

```text
busy             = !ap_idle
result_valid     = ap_done && core_reciprocal_result
sram_write_valid = ap_done && core_sram_write_valid
```

三者含义严格分开：

- `busy`：HLS IP 正在执行一笔真实物理事务；
- `result_valid`：reciprocal 完成脉冲；
- `sram_write_valid`：只有 `ACC/ACC_SA` 的结果允许写回 Accumulator SRAM。

`SET_SCALE`、`EXP_S1`、`EXP_S2` 和 `RECIPROCAL` 只更新内部 `scale`，不能把
结果有效误当成 SRAM write enable。

原 Chisel 排程会把 RECIPROCAL 控制连续保持有效 15 拍。包装层增加
`reciprocal_request_seen` one-shot：第一笔 reciprocal 握手后，在
`request_valid` 拉低前禁止完成拍再次启动第二笔倒数。

## 3. 四列完全并行

以下循环加入 complete `UNROLL`：

- `reset_accumulator_state()` 的四列复位；
- `accumulator_step()` 的主四列普通计算；
- reciprocal transaction 的四列状态推进；
- 顶层输入/输出映射循环。

以下对象继续使用 complete `ARRAY_PARTITION`：

- `current.scale/current.reciprocal`；
- `next.scale/next.reciprocal`；
- `io.sa_in/io.sram_in/io.sram_out`；
- reciprocal transaction 的四列局部状态和结果。

源码结构已表达四列空间复制，但最终验收仍要检查：

- 原 Trip Count=4、II=1 的主列 pipeline 是否消失；
- RTL 是否确有四套 reciprocal 比较/减法数据通路；
- 普通路径是否生成四套所需 FP32 算术实例；
- DSP/LUT/FF 是否与空间复制相符；
- 是否存在资源共享或数组端口冲突警告。

## 4. 数值边界修改

`accExp2PWL()` 在浮点转整数前新增 IEEE-754 分类，避免 Inf/NaN 或巨大有限
数触发未定义的 float-to-int 行为：

- `-Inf -> +0`；
- `+Inf -> +Inf`；
- NaN 统一为 `0x7fc00000`；
- `x >= 128 -> +Inf`；
- `x <= -150 -> +0`（`2^-150` 为 RNE 到零的中点）。

测试新增：

- reciprocal 的零、正负 Inf、quiet/signaling NaN；
- 最小/最大次正规数、最小正常数、最大有限数；
- reciprocal 溢出阈值、RNE 和正常/次正规输出边界；
- reciprocal 结果按 FP32 位模式比较；
- ACC_SA 的融合舍入判别、溢出、次正规和半 ULP ties-to-even；
- exp2 八个分段及七个边界左右相邻 FP32 值；
- exp2 的 `-Inf/+Inf/NaN/-126/-127/-149/-150/128`。

## 5. 当前验证状态

已完成：

- 修改文件使用本地 g++ 严格警告编译；
- 使用仅限本地测试的标准数学 shim 后，`test_accumulator` 通过；
- 同一 shim 下 `test_accumulator_top` 通过，包括六组 reciprocal 位级边界；
- 同一 shim 下 `test_acc_exp2` 通过；
- 新增 wrapper RTL testbench，覆盖15拍脉冲、持续 valid one-shot、重新
  arm 和 busy 中复位场景。

由于本机没有 Vitis HLS/XSIM/iverilog，本轮尚未完成：

- 新源码的 Vitis C synthesis；
- C/RTL co-simulation；
- wrapper RTL testbench 实际运行；
- 顶层 `ap_start -> ap_done` 恰好15拍的物理波形确认；
- 四列算术实例数和新资源数据确认。

在装有 Vitis HLS 2024.2 的环境运行：

```bash
./run_hls.sh accumulator
```

新报告至少要核对：

```text
accumulator_top latency（RECIPROCAL分支）
accumulator_reciprocal_transaction latency / II
主列循环是否完全展开
FP32 FMA与reciprocal实例数
DSP / LUT / FF
ap_start、busy、ap_done、result_valid波形
连续RECIPROCAL valid是否只启动一次
```
