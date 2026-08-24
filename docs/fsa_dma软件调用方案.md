# `fsa_dma_top` 软件调用方案

## 1. 当前功能

`fsa_dma_top` 是一次启动完成整个矩阵计算的系统顶层。软件提供Q、K、V、O在DDR中的
基地址、序列长度 `L` 和 causal 开关；硬件随后自行执行二维tile循环：

1. 每次读取 `SA_COLS` 行Q；
2. 对完整K/V矩阵循环 `ceil(L / SA_COLS)` 次；
3. 通过现有 `fsa_core_request_run`、Systolic Array和 `accumulator` 维护在线softmax；
4. 最后把完整O矩阵写回DDR；
5. 拉高HLS控制接口的 `ap_done`，并返回状态码。

当前默认参数为 `SA_ROWS=4`、`SA_COLS=4`，因此完成
`Q,K,V ∈ R^(L×4)` 到 `O ∈ R^(L×4)` 的计算。`L` 不要求是4的整数倍，最后一个tile
会自动处理不足4行的情况。

矩形配置 `SA_ROWS=128`、`SA_COLS=4` 表示：

- head dimension为128；
- 每个Q/K/V序列tile包含4个token；
- 外层K/V循环执行 `ceil(L/4)` 次；
- Q、K、V和O的矩阵形状变为 `L×128`。

当前矩形版本优先保证功能。现有SA数据通路高度仍为 `SA_ROWS`，所以每个4-token K/V
tile只占前4行，其余行用0填充并在CMP/softmax阶段屏蔽。该方法会执行多余计算，但不
影响结果；后续优化时可以再增加可变执行长度，跳过填充行。

## 2. DDR数据布局

四个矩阵使用相互独立的DDR区域，软件不得让输出O覆盖仍在读取的Q、K或V。

| 矩阵 | 数据类型 | 逻辑形状 | DDR布局 |
|---|---|---|---|
| Q | FP16 | `[L][SA_ROWS]` | row-major |
| K | FP16 | `[L][SA_ROWS]` | row-major |
| V | FP16 | `[L][SA_ROWS]` | row-major |
| O | FP32 | `[L][SA_ROWS]` | row-major |

AXI master数据宽度为64 bit：

- 一个beat打包4个FP16，lane 0位于最低16 bit；
- 一个beat打包2个FP32，lane 0位于最低32 bit；
- 每行Q/K/V占 `SA_ROWS/4` 个beat；
- 每行O占 `SA_ROWS/2` 个beat。

V不再要求由软件提前转置。硬件接口直接接收普通的 `[token][feature]` row-major V。

所需DDR容量为：

```text
Q_bytes = K_bytes = V_bytes = L * SA_ROWS * 2
O_bytes                         = L * SA_ROWS * 4
```

## 3. 顶层参数

HLS顶层等价于下面的C++接口：

```cpp
void fsa_dma_top(
    const uint64_t* q_addr,
    const uint64_t* k_addr,
    const uint64_t* v_addr,
    uint64_t* o_addr,
    uint32_t sequence_length,
    bool causal,
    uint8_t& status
);
```

这里的指针在导出IP后不是CPU直接调用的普通函数参数，而是由软件通过AXI4-Lite写入的
64-bit DDR物理地址。`sequence_length` 和 `causal` 同样是AXI4-Lite控制寄存器。

状态码如下：

| status | 含义 |
|---:|---|
| 0 | 计算成功，O已全部写回 |
| 1 | `L=0` 或 `L>MAX_SEQUENCE_LENGTH` |
| 2 | 内部core请求或读写协议错误 |

## 4. 推荐的软件API

上层软件可以封装成：

```cpp
bool sa_calc(
    const half* q,
    const half* k,
    const half* v,
    float* o,
    uint32_t L,
    bool causal
);
```

一次调用的推荐顺序是：

1. 在DDR中为Q、K、V和O分配物理连续缓冲区；
2. 按row-major格式把Q、K、V写入DDR；
3. 非一致性系统上，flush Q/K/V对应的CPU cache；
4. 通过AXI4-Lite写入 `q/k/v/o` 四个64-bit物理地址；
5. 写入 `sequence_length=L` 和 `causal`；
6. 写HLS控制寄存器中的 `ap_start=1`；
7. 轮询 `ap_done`，或者等待IP中断；
8. 读取 `status`，必须为0才接受结果；
9. 非一致性系统上，invalidate O对应的CPU cache；
10. 从O缓冲区读取完整的 `L×SA_ROWS` FP32矩阵。

伪代码如下：

```cpp
bool sa_calc(...){
    copy_qkv_to_ddr();
    flush_qkv_cache();

    fsa_set_q(q_phys);
    fsa_set_k(k_phys);
    fsa_set_v(v_phys);
    fsa_set_o(o_phys);
    fsa_set_sequence_length(L);
    fsa_set_causal(causal);
    fsa_start();

    wait_for_done_or_interrupt();
    if(fsa_get_status()!=0){
        return false;
    }

    invalidate_o_cache();
    copy_o_from_ddr();
    return true;
}
```

寄存器偏移应以本次HLS导出的驱动头文件或 `component.xml` 为准，不建议在软件中手写
固定偏移，因为增加 `sequence_length`、修改端口名或重新导出IP后偏移可能改变。

## 5. 启动和完成信号放在哪里

当前方案不在DDR的 `0x8000` 放start，也不在 `0x8004` 放done。启动、完成和中断使用
HLS自动生成的AXI4-Lite控制接口：

```text
CPU写AXI-Lite ap_start
        ↓
fsa_dma_top内部FSM运行
        ↓
DMA读取Q/K/V → core计算 → DMA写回O
        ↓
ap_done=1，可选interrupt=1，status可读
```

这样控制访问和大数据访问分开，硬件不需要持续轮询DDR，也不会把普通DDR地址误当成
寄存器。若以后需要任务队列，可以在DDR中增加descriptor ring，但那属于新的命令处理
模块，不是当前第一版必需功能。

## 6. `L` 是否写回DDR

不写回。这里有两个容易混淆的量：

- `sequence_length`：软件传入的矩阵行数，通过AXI4-Lite寄存器送给硬件；
- 在线softmax的行归一化量：仅在core内部保存，用于最后归一化O。

当前前向计算只向DDR写回O。若未来支持训练反向传播，可以另加可选的logsumexp输出
地址，但不应与当前的序列长度参数混用。

## 7. 修改阵列形状

默认参数位于 `include/fsa/config.hpp`。也可以在运行HLS时通过环境变量覆盖，而不修改
源码。例如生成head dimension为128、序列tile为4的矩形版本：

```bash
FSA_SA_ROWS=128 FSA_SA_COLS=4 \
FSA_MAX_SEQUENCE_LENGTH=4096 ./run_hls.sh fsa_dma
```

参数含义固定为：

```text
SA_ROWS = head dimension
SA_COLS = 每次处理的query/key token数
```

当前系统顶层约束为 `SA_COLS <= SA_ROWS`、`SA_ROWS <= 128`、
`MAX_SEQUENCE_LENGTH <= 4096`。请求核读回Accumulator RAM时还要求
`ACC_SUB_BANKS <= nMemPorts`；在当前64-bit beat、FP32 Acc和4个端口下等价于
`SA_COLS <= 8`。此外，`SA_ROWS` 必须满足FP16/FP32数据能完整打包为64-bit beat。
修改参数后必须重新执行C仿真和综合；4×4构建的报告不能用于证明128×4构建的资源、
时序或功能。

## 8. 当前测试平台原理

`tests/hls/test_fsa_dma_top.cpp` 使用 `L=2×SA_COLS+1`，故意让序列长度不能被tile大小
整除。测试执行以下检查：

1. 生成确定性的完整Q、K、V矩阵；
2. 按真实64-bit DDR格式打包FP16；
3. 只调用一次 `fsa_dma_top`；
4. 用普通C++独立计算完整softmax参考结果；
5. 比较DDR中完整 `L×SA_ROWS` FP32 O矩阵；
6. 使用canary检查有效O区域之后没有被越界写入。

因此该测试覆盖的是“一次start、多个Q tile、多个K/V tile、最后一个不完整tile、完整O
写回”，不再只是原来的单个4×4 tile测试。
