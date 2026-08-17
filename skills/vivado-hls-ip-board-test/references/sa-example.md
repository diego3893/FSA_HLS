# 4x4 Systolic Array示例

仅在目标确实是当前 FSA-HLS的 `systolic_array_top`时使用本页。每次仍要重新读取实际 `component.xml`和导出 RTL。

## 已知接口示例

```verilog
module systolic_array_top (
    input  wire         ap_clk,
    input  wire         ap_rst,
    input  wire         ap_start,
    output wire         ap_done,
    output wire         ap_idle,
    output wire         ap_ready,
    input  wire [121:0] input_r,
    output wire [131:0] output_r
);
```

这是 `ap_ctrl_hs`事务接口。控制器必须在 `ap_ready`前保持 `ap_start`和 `input_r`。

## 固定测试

```text
Q = [1 2 3 4]    K = [1 0 2 1]
    [2 1 0 1]        [0 1 1 2]
    [1 0 1 0]        [2 1 0 1]
    [3 2 1 0]        [1 2 1 0]

S = Q * K^T
  = [11 13 8 8]   rowmax=13
    [ 3  3 6 4]   rowmax=6
    [ 3  1 2 2]   rowmax=3
    [ 5  3 8 8]   rowmax=8
```

测试事务包含：逻辑状态复位、倒序装载 Q、冲刷流水线、错拍送入 K、CMP UPDATE、PROP_MAX和向下流出。不要把普通矩阵乘 testbench简化成不符合真实控制传播的单次组合调用。

## 输出示例

当前聚合输出的每列为一个 `valid + 32位data`：

| 列 | valid | data |
|---|---:|---:|
| 0 | `[0]` | `[32:1]` |
| 1 | `[33]` | `[65:34]` |
| 2 | `[66]` | `[98:67]` |
| 3 | `[99]` | `[131:100]` |

S元素使用低16位 FP16位模式；rowmax示例输出为普通 FP32负值位模式。修改类型、数组大小或 aggregate方式后必须重查。

## 已完成的板上结果示例

已观察到：

```text
test_busy = 0
test_done = 1
test_fail = 0
test_pass = 1
```

这只证明当前固定向量和当前 IP版本通过，不自动证明其他模块或其他数据通过。

