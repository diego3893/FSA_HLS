### K/V广播可能重新制造跨SLR长线

如果一个K/V流直接扇出到8个FSA，会产生：

- 高扇出数据网络；
- 跨SLR连线；
- 大量时序压力；
- 任一FSA反压可能阻塞全部FSA。

更合理的是：

```
HBM
 -> 每个SLR一个K/V缓冲
 -> SLR内部再广播给1～2个FSA
```

或者在不同HBM bank保存K/V副本。

所以多小阵列消除了阵列内部跨SLR连接，却可能把问题转移到K/V供数网络。

## 5. 当前源码存在的几个关键不兼容点

### 5.1 attention scale会算错

当前代码：

```
ATTENTION_SCALE_EXACT =
    log2(e) / sqrt(SA_ROWS)
```

如果直接把物理阵列配置为16×16，它会使用：

$1/\sqrt{16}=1/4$

但逻辑head dimension仍是128，正确值应为：

$1/\sqrt{128}\approx1/11.314$

前者会把logit放大约：

$\sqrt{128/16}=\sqrt8\approx2.828$

Softmax将明显变尖，结果错误。

必须独立定义：

```
PHYSICAL_ROWS = 16
HEAD_DIM = 128
ATTENTION_SCALE = log2(e)/sqrt(HEAD_DIM)
```

### 5.2 DMA会把每个token误认为只有16维

当前：

```
DMA_QKV_WORDS_PER_ROW = SA_ROWS / 4
DMA_O_WORDS_PER_ROW   = SA_ROWS / 2
```

若 `SA_ROWS=16`，DMA只会读取16维Q/K/V、写回16维O，而不是128维。

也必须改为基于逻辑 `HEAD_DIM`。

### 5.3 Scratchpad和Accumulator容量都绑定物理阵列

当前：

```
SPAD_ROWS = 2*SA_COLS + 4*SA_ROWS
ACC_ROWS  = 1 + SA_ROWS
```

Split-D后需要分别表达：

```
PHYSICAL_D_TILE = 16
LOGICAL_HEAD_DIM = 128
QUERY_TILE = 16
KEY_TILE = 16
D_TILE_COUNT = 8
```

不能继续只使用 `SA_ROWS/SA_COLS` 两个参数描述所有含义。

### 5.4 当前请求核只有一份静态状态

fsa_core_request_top.cpp 中有：

```
static FsaCoreDatapathState state;
static bool online_sequence_active;
```

并且限制：

```
#pragma HLS ALLOCATION function instances=advanceDatapath limit=1
```

因此简单循环调用8次不会自动得到8个独立FSA。需要：

- 显式建立 `state[8]`；
- 或把FSA做成带engine ID的模板实例；
- 对engine循环完全展开；
- 或创建8个独立dataflow process。

否则HLS很可能仍然复用一套硬件并顺序执行。

### 5.5 当前DMA只有一个64-bit AXI master

当前Q/K/V/O共享同一个 `gmem` bundle：

```
#pragma HLS INTERFACE m_axi ... bundle=gmem
```

64 bit、100 MHz的理论上限只有约0.8 GB/s，而且还没有DMA/计算重叠。它无法持续喂满8个16×16 FSA。

新架构至少需要：

- 多个HBM pseudo-channel；
- 更宽的数据口；
- K/V本地双缓冲；
- DMA和计算DATAFLOW重叠；
- 分层广播或多bank复制。

## 6. 最严重的现实问题：当前PE资源不能直接扩成8×16×16

当前综合报告中，4×4 SA阶段使用约280个DSP，其中16个PE都具有独立的浮点MAC和 `exp2PWL` 实例：fsa_dma综合报告.md。

粗略线性外推：

$280/16=17.5\text{ DSP/PE}$

一个16×16 FSA可能需要约：

$256\times17.5=4,480\text{ DSP}$

8个则约：

$8\times4,480=35,840\text{ DSP}$

目标器件只有约9,024个DSP。甚至只看报告中的每PE `exp2` 约7 DSP：

$2,048\times7=14,336\text{ DSP}$

也已经超过全器件容量。

这不是实际16×16综合结果，只是依据现有4×4实现的线性估算；但源码采用完全展开和每PE独立浮点算术，因而这个风险非常现实。

所以：

> 以当前FP16/FP32、每PE独立PWL指数的微架构，8个16×16 FSA大概率无法放入NM37。小阵列改善布线，但没有解决算术资源爆炸。

可能需要：

- 将exp2从“每PE一个”改成每行/每列共享；
- 用定点或混合精度PWL替代当前浮点运算；
- 用LUT/BRAM实现指数近似；
- 将部分非线性单元时分复用；
- 让MAC保持空间并行，但Softmax阶段使用较少的共享算术单元。

这样仍可以保留“Softmax在FSA引擎内部”，不一定要坚持“每个PE都有完整exp2硬件”。

## 7. 综合优劣表

| 维度           | 当前单FSA            | 8个16×16 Split-D             |
| -------------- | -------------------- | ---------------------------- |
| 控制复杂度     | 较低                 | 高，增加D循环与屏障          |
| 数学正确性实现 | 已实现               | 正确方法清楚，但尚未实现     |
| Score额外存储  | 不需要               | 每FSA约1 KiB FP32            |
| query并行      | 一个tile             | 8个tile并行                  |
| 跨FSA归约      | 无                   | 无，这是重要优点             |
| PV阵列利用率   | 矩形配置可能较低     | 16×16映射更均衡              |
| 阵列内部布线   | 长，可能跨SLR        | 局部、较易floorplan          |
| K/V供数        | 单路、简单但慢       | 广播复杂，可能成为瓶颈       |
| DMA流量        | 当前代码重复读取严重 | 广播成功时明显减少           |
| 状态存储       | 一组m/L/O            | 8组并行m/L/O                 |
| Tail和短序列   | 单核较容易利用       | 可能有FSA空闲                |
| DSP资源        | 当前4×4可综合        | 按当前PE直接扩展很可能超资源 |
| 现有验证程度   | C/RTL已有用例        | 仅规划                       |
| 最高潜在Fmax   | 受大阵列和长线限制   | 可能更高，但需post-route证明 |
| 实施改动量     | 当前已有             | 属于系统级重构               |

## 8. 我建议的实现顺序

不要直接从当前代码跳到8个FSA。更稳妥的路径是：

1. 先将配置拆成：

   ```
   HEAD_DIM=128
   PHYS_ROWS=16
   QUERY_TILE=16
   KEY_TILE=16
   D_TILES=8
   N_FSA=1
   ```

2. 实现单个16×16 Split-D：

   - FP32 Score accumulator；
   - 前7个D tile禁止CMP/exp；
   - 最后一个D tile才进入Softmax；
   - P保存在PE中并复用于8个V tile；
   - O RAM扩展到16×128；
   - scale使用逻辑d=128。

3. 用独立金标准验证：

   - 单个KV tile；
   - 两个及以上KV tile；
   - max发生变化时的 $\alpha O$；
   - causal；
   - 尾部D/key/query tile；
   - FP32与FP16部分Score误差对比。

4. 综合单个16×16，首先判断DSP是否可接受。若单核超过约1,000 DSP，8核基本没有空间留给DMA、缓存和互连。

5. 优化exp2和Accumulator资源后，再扩成2、4、8个FSA。

6. 最后实现分层K/V广播和HBM bank映射，而不是让8个核共享当前单一64-bit AXI口。