# 第三阶段3A：独立FP32 Raw FMA

日期：2026-09-20。状态：独立候选本地验证完成，等待用户Vitis验收；没有接入正式Accumulator。

## 第二阶段验收依据

`build/fsa_stream_build/solution1`的15:19综合、15:23 CoSim包含27位CLZ修改，CSim/RTL CoSim通过，无时序或II违例警告。目标器件VU37P、10 ns周期、2.7 ns uncertainty不变。

| 指标 | 上次13:12 | 当前15:23 |
|---|---:|---:|
| 顶层HLS估算周期 | 7.337 ns | 7.300 ns |
| SA Tile估算周期 | 7.337 ns | 7.272 ns |
| SA主循环II / Tile latency / interval | 1 / 143 / 134 | 不变 |
| non-causal / causal / invalid周期 | 1785 / 1317 / 55 | 不变 |
| DSP / BRAM18K | 44 / 20 | 不变 |
| FF / LUT | 92204 / 128641 | 91252 / 125809 |

单SA、16条PE乘法通路、4 CMP、单4-lane Accumulator；DMA Q/K/V、Scratchpad和Delayer关键循环II1，Accumulator向量10拍/II1未退化。满足既定第二阶段HLS/RTL验收门槛，按用户授权进入3A。顶层预算余量为零，未做Vivado实现，不能声称板级时序通过。

## 3A实现与边界

- 新接口`include/fsa/stream/fp32_raw_fma.hpp`，独立实现与顶层在`src/stream/fp32_raw_fma*.cpp`。只做FP32×FP32+FP32，不包含PWL、reciprocal或缩放。
- 只有一个24×24尾数乘法表达，保留48位完整乘积。51位对齐域容纳完整乘积和3个低位，对阶用shift-right-jam；52位幅值加减、CLZ规格化，最后一次round-to-nearest-even。
- 临近相消时乘积和加数的对齐保留精确低位；远距对齐的舍弃位归约为sticky。不能将已验收的27位PE累加域直接照搬到FP32乘积，否则深度相消会丢失结果。
- 支持正常/非规格化输入输出、符号零、Inf和canonical quiet NaN。仅RNE，不提供异常标志或NaN payload传播。运算不会先把乘积舍入/溢出为FP32。
- 一个24×24乘法通常需要多于一个DSP，不能把DSP数等同于FMA实例数；实际DSP及RTL层次需综合核验。
- 独立顶层请求II1、latency 5..8；乘法DSP latency=2。这些是初始探索约束，不是测得性能，也不保证集成后的Accumulator更快。预期资源门槛为单FMA且DSP不超过现有每lane的5个；最终还须比较集成后的9拍lane/10拍向量。
- 正式`pe_raw_fma`、SA、CMP、Accumulator、DMA、Scratchpad、Delayer、hop8和`hls/fsa_stream/run_hls.tcl`未修改，已比较修改前后文件指纹。独立源文件不在正式HLS源清单中。

算法参考：[Berkeley SoftFloat融合乘加](https://github.com/ucb-bar/berkeley-softfloat-3/blob/master/source/s_mulAddF32.c)的完整乘积、对齐与最终舍入思路；使用项目自身AP位域实现，未复制该源文件。[AMD BIND_OP说明](https://docs.amd.com/r/2024.2-English/ug1399-vitis-hls/pragma-HLS-bind_op)用于指定乘法映射及延迟，不是对整个FMA时序的保证。

## 本地验证

独立测试共248016组：16个手算定向答案、4096个特殊值组合、100000随机三元组、100000相消邻域、43904指数/尾数边界；全部逐位通过，NaN要求canonical值，正负零不混同。覆盖融合残差、乘积先溢出但融合结果有限、下溢半值、最小非规格化、RNE奇偶进位等。

默认golden为18个32位字的精确整数网格，以2^-298为单位存储完整乘积及加数，最终一次舍入。其算法不复用DUT的51位截断/对阶逻辑。采用它是因为本机默认`std::fma`路径未通过3个手算自检，并非放宽精度。

另以`-O2 -march=native`编译测试参考部分进行交叉验证，确认对象文件包含`vfmadd132ss`：全部非手算用例的整数golden、原生FP32 FMA和DUT一致。DUT仍按默认选项编译，因为旧AP头在全量-O2时触发构造器未初始化告警；未关闭`-Werror`或修改第三方库。

可复现本地入口：

```powershell
.\run_fp32_raw_fma_test.ps1
.\run_fp32_raw_fma_test.ps1 -CheckHostFma
```

第二条仅是可选本机参考交叉验证，依赖编译器/CPU原生FMA；服务器CSim默认使用不依赖它的整数golden。4x2、4x4、8x4完整attention及Accumulator PWL本地回归也均通过，证明添加独立候选没有破坏现有构建，不代表已完成集成。

## 下一次手动验收

```bash
./run_hls.sh fp32_raw_fma
```

新`hls/fp32_raw_fma/run_hls.tcl`默认CSim+CSynth，CoSim/IP导出关闭，器件和时钟与正式核相同。检查：

1. CSim零错误，尤其是相消、非规格化、符号零和仅一次舍入。
2. achieved II=1、latency不超过本轮8拍目标、周期<=7.300 ns，无约束违例；记录实际值，不把pragma当结果。
3. 单一尾数乘法数据通路、DSP<=5，无复制FMA或额外浮点IP；记录LUT/FF代价。
4. 达标后由用户验收，才进入3B，替换四个Accumulator lane内的有效FMA并重新验证完整数据通路。当前综合看到的旧lane是`fmul+fadd`，新核是真正融合舍入；集成时必须核对数值差异及HLS浮点非规格化处理，不能假定与旧RTL逐位一致。
5. 3B通过后单独做3C CMP专用减法与无效输出清理；再通过后才做3D hop4。150/200 MHz仍后置。
