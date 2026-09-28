# Split-D CoSim写响应卡死修改日志

## 本轮目标

- 复现并定位4×4/16配置下RTL CoSim长时间停在`0/6`的问题。
- 只修改导致CoSim无法结束的控制结构，不改变PE、Accumulator、softmax或PV算法。
- 验收保持单PE阵列、单Accumulator、DSP40、PV II1和10ns目标时钟。

## 初始状态

- 本地与远端分支均为`fsa_split_D`，提交均为`1b79e82bc0a523b9a8cec02ab41d237ac930d9e5`。
- 远端工作树已有用户生成的未跟踪`evidence/`、`logs/`和Vivado日志，本轮不修改这些文件。
- 新build的CSim和CSynth通过；RTL CoSim运行7小时20分后仍为`0/6`，由用户手动停止。

## 诊断证据

- 复用远端XSim快照运行100us后，`runController`停在状态12，等待`storeOutputTile`完成。
- `storeOutputTile`停在最终状态72，唯一退出条件是AXI写响应`BVALID=1`。
- AXI仿真内存模型显示：1个写地址请求和32个写数据拍均已接收、FIFO均已清空，但`BRESP_counter=0`，因此外层状态机永久等待。
- 这说明写响应在内层流水写回阶段已经被握手消费，外层函数又等待同一响应；问题位于两层输出写回控制，不在PE或Accumulator计算通路。

## 方案


- 将`storeOutputTile`的token外层循环和word内层流水循环展平为单一连续word循环。
- 让HLS从一个连续写循环生成AXI burst和单一响应控制，消除内外两层对写响应的重复等待。
- 保持输出地址顺序和数据打包格式不变。

## 资料

- AMD UG1399说明非内联子函数会成为独立RTL模块，并由工具自动决定子函数接口协议。
- AMD UG1399说明`ap_ctrl_hs`要求`ap_start`保持到`ap_ready`，完成和接收由`ap_done/ap_ready`握手表达。
- AMD UG1399的ALLOCATION说明共享函数会减少实例但可能增加控制和性能代价；本轮不改变现有共享计算单元。

## 验收结果

待完成。
