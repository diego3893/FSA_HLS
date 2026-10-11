# Split-D ROW_SUM时序候选修改

## 1 范围与基线

- 用户要求：“先改时序吧，这一版不测，后面一起测”。本次只修改计算源码并做语法/静态核对，不运行testbench、CSim、CSynth、CoSim、IP导出、Vivado或板测。
- 起始提交：`5fda5a4a356af0be05ac607604b06a38b03f77e1`；最近实际测量源码为`71025ffadbe4e566432eaeed51bfc2b1bab2c9e9`。
- 修改文件：`src/stream/split_d/split_d_compute.cpp`，函数`runPeRowSum`。保持阵列形状、共享层级、接口、精度、时钟/uncertainty和全部pragma。
- 4×4已验收IP/bitstream及16×16测量报告、原始证据保持历史基线，不用于证明本候选已通过。

## 2 问题与修改

71025ff的两个16×8worker在ROW_SUM均报告7.390ns，超过7.300ns有效预算。原始日志`docs/evidence/split_d_71025ff_16x16/codex_split_d_71025ff_16x16_short_csim_csynth_20261011.log`的`HLS 200-1016`列出路径：

```text
row_sum load(0.000ns) → operand_c select(0.227ns) → runPeArray call(7.163ns)
```

原先`operand_c[r][col] = r==row ? row_sum[col] : accZero()`；改为所有行广播`row_sum[col]`。只提交`pe_result[row][col].out_accType`，因此未选中行无需将累加输入置零。此修改消除源代码中按动态row控制的PE输入选择器。

广播会改变输入扇出；HLS可能重新调度和绑定，其他路径也可能成为最坏路径。因此不能把7.390−0.227ns当作新估算周期，不能宣称时序已经达标或II已经改善。

## 3 静态等价性依据

1. 当前选中行`r==row`的`operand_a`、`operand_b=elemOne()`、`operand_c=row_sum[col]`和`exp2_mode=false`在修改前后相同。
2. `runPeArray`以const PE输入逐坐标调用`peMacUnit`，该算术链没有跨行状态更新；其他行结果被丢弃，不进入后续状态或输出。
3. `row_sum`仍仅由当前行FP32结果写回，row=0..D−1的逐行FMA和舍入顺序不变，真实反馈保留。
4. 未新增MAC、状态数组或流水级，未修改QK/PWL/PV、测试台、配置宏或HLS入口。共享函数同时影响4×4与16×16，后续两种配置均需回归。

## 4 本地检查

计算翻译单元在4×4/head16和16×16/head128均完成`g++ -fsyntax-only`，exit0。未生成或执行测试程序。

```powershell
& 'D:/MinGW/bin/g++.exe' -std=c++14 -fsyntax-only -Wno-unknown-pragmas -Wno-unused-parameter -Iinclude -isystem third_party/vitis_hls/include -DFSA_LOCAL_MATH_STUBS -DHLS_NO_XIL_FPO_LIB -DFSA_SPLIT_D_PE_DIM=4 -DFSA_SPLIT_D_HEAD_DIM=16 src/stream/split_d/split_d_compute.cpp
& 'D:/MinGW/bin/g++.exe' -std=c++14 -fsyntax-only -Wno-unknown-pragmas -Wno-unused-parameter -Iinclude -isystem third_party/vitis_hls/include -DFSA_LOCAL_MATH_STUBS -DHLS_NO_XIL_FPO_LIB -DFSA_SPLIT_D_PE_DIM=16 -DFSA_SPLIT_D_HEAD_DIM=128 src/stream/split_d/split_d_compute.cpp
```

普通编译器忽略HLS pragma，此检查不证明数值、综合、RTL或时序。

## 5 后续合并验收

待用户启动测试后，使用同一新提交和标准入口回归两种配置；CoSim/IP/实现范围以届时明确授权为准。

- 功能：保留官方数学参考、现有位模式基线、边界/非法长度及canary判据；不得放宽误差。
- 时序：核对两块ROW_SUM的输入选择器和新的关键路径；保持10ns时钟、2.7ns uncertainty，顶层估算周期目标≤7.300ns。
- 调度：检查QK/ROW_SUM真实反馈和PWL/PV的实际II，不预设删除选择器能解决II10/10/5/5。
- 实例/资源：核对总D×D PE和D条Accumulator，排除共享层级变化或额外算术复制，记录广播扇出对LUT/FF及面积的影响。

当前结论：源码候选完成，仅语法和静态核对；硬件改善与完整功能验收均待后续。
