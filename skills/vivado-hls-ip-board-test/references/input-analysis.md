# 输入材料分析

## 1. 检查顺序

按以下顺序读取材料，不要只依据其中一种：

1. `component.xml`：确认 VLNV、IP版本、总线接口和端口元数据。
2. 导出的顶层 RTL：确认 Vivado真正看到的模块名、端口名、方向和位宽。
3. HLS顶层头文件与实现：理解结构体、数组、控制字段和复位语义。
4. HLS C++ testbench：提取事务顺序、空闲拍、valid、期望结果和误差规则。
5. HLS综合/协同仿真报告：记录协议、Latency、II和已完成的验证层级。

物理接口以导出 RTL为准，字段含义以 C++源码为准，测试意图以 testbench为准。三者不一致时停止生成并指出版本可能不匹配。

## 2. 从 `component.xml` 提取

至少记录：

- `vendor:library:name:version` 组成的 VLNV。
- HLS顶层名、IP显示名和导出工具版本。
- `ap_clk`、`ap_rst`/`ap_rst_n`及其极性。
- `ap_start/ap_done/ap_idle/ap_ready`等 block control端口。
- AXI4、AXI4-Lite、AXI4-Stream、BRAM或普通端口接口。
- 每个普通端口的方向和向量范围。

不要假定 `component.xml`所在目录名等于 VLNV中的 IP名。

## 3. 从导出 RTL提取

找到真正的顶层声明并制作端口表：

| 端口 | 方向 | 位宽 | 协议/用途 |
|---|---|---:|---|
| `ap_clk` | input | 1 | 工作时钟 |
| `ap_rst` | input | 1 | 示例：高有效同步复位 |
| 其他 |  |  | 从实际 RTL填写 |

检查结构体是否被 `AGGREGATE compact=bit`压成宽总线。如果是，位宽只能从实际 RTL确认；字段位排列还要结合 HLS源码、控制类型宽度和已有仿真结果核对。

## 4. 从 HLS testbench生成板上测试

把 C++ testbench拆成四部分：

1. 初始复位与状态清理。
2. 每笔输入事务及其有效条件。
3. 预期输出出现的事务或周期。
4. 金标准比较方法。

板上控制器必须有限、可综合、自检。常见替换方式：

| C++ testbench写法 | 板上控制器写法 |
|---|---|
| `std::vector` | 固定数组、case表或 ROM |
| 随机数 | 固定种子后离线固化，或选择确定性样例 |
| 文件输入 | 常量表，或明确改用 AXI/DMA |
| `std::cout` | `test_pass/test_fail`和 ILA probe |
| 浮点容差比较 | 固定格式的容差比较；精确可表示值可比较位模式 |
| 无限等待 | 状态机计数器和超时失败状态 |

金标准尽量独立于被测实现。位模式转换功能直接比较位模式；普通浮点计算根据类型设置合理误差。

## 5. 控制协议选择

### `ap_ctrl_hs`

- 先设置输入。
- 拉高并保持 `ap_start`。
- 在 `ap_ready`前保持所有输入稳定。
- 接受后撤销 `ap_start`。
- 在 `ap_done`采样输出。

### `ap_ctrl_none`

没有开始/完成握手。必须根据 `valid/ready`、固定延迟或流接口设计控制器，不能套用事务状态机。

### AXI4-Stream

遵守 `TVALID/TREADY`，在阻塞时保持 `TDATA/TLAST/TKEEP`稳定。板上测试通常需要 AXIS发生器和接收器，而不是一个聚合输入总线。

### AXI4或外部存储接口

先确认测试是否真的需要 DDR/HBM/DMA。如果只是验证计算核，优先使用小型片上 ROM/BRAM和有限事务；如果无法绕开外存，应先向用户确认测试范围。

## 6. 必须阻止的错误复用

- 不把 SA的端口宽度复制给 PE、CMP、Delayer或 Accumulator。
- 不把一个模块的枚举位宽或结构体排列复制给另一个模块。
- 不因为 C++调用能运行，就认为导出 RTL端口与当前源码一致。
- 不因为综合通过，就认为 testbench、时序或板上结果已经通过。

