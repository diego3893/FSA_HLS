# 阶段4恢复：初始化与有限供数静态检查

检查日期2026-10-13；修改前11cd35a，候选0bfd6d2。此文是源级检查，不替代CSim/CSynth或CoSim。

## 1 首次读取前的定义

| 状态 | 定义位置 | 首次读取 | 本轮处理 |
|---|---|---|---|
| q | worker消费全部context/列/word，包括padding | QK | 删除多余整结构零初始化，保留全部装载 |
| k/v | 每key tile消费全部row/word，包括padding | QK/PV | 同上 |
| score | 每合法context/key tile清零 | QK反馈 | 保留每key清零 |
| score_valid | 每key依据query/key边界及causal计算 | max/softmax | 保留掩码计算 |
| probability_ready | 每key清false | PWL首命中 | 保留每key清零 |
| reg | 完整QK后的FP32→FP16量化；PWL屏障再写P | SUB_MAX及后续phase | 先写后读，无需batch预清零 |
| running_max | accMinimum | 首key max更新 | 移出Q装载，显式初始化 |
| running_sum | accZero | 首次ROW_SUM/L更新 | 显式初始化 |
| output_acc | accZero | 首次PV/O更新 | feature流水、列并行清零 |
| history_valid | false | 首key max/alpha处理 | 显式初始化 |
| alpha/tile_has_value | 每key控制器max/历史计算 | alpha处理/RS/PV | 先写后读，无需batch预清零 |
| pe/Acc结果与control环 | 前HOP拍的每个年龄槽先写 | cycle≥HOP且token.valid | 原年龄保护保留，无额外初始化 |

首key没有历史时，alpha为零，但RawFMA仍可能读取旧L/O；不能用乘零替代显式清零，尤其不可读未初始化值。未采用static上电初始化或跨batch历史复用。

## 2 存储与归约合同

- 保留Q/K/V/O的双口BRAM绑定，S/reg的LUTRAM绑定与现有坐标/DMA分bank。是否真正生成该RAM只能由最终RTL/报告验证。
- 原两个16×8worker、唯一PE/Acc调用点、ALLOCATION、phase排空、P完整/RS完整屏障和真实反馈依赖保持。
- QK feature顺序、ROW_SUM/PV key顺序、PWL首命中及FP16量化点保持；未增加context容量掩盖反馈延迟。
- 有限FIFO静态关系：distributor先供整批Q再供KV，worker消费全部Q后消费每key的完整KV，最后供O；gather等待O。初始化先于Q读取增加有限等待，但不新增worker对KV/O的逆向等待关系。静态等待图不等同RTL背压验证，本次CoSim未授权。

## 3 资料及证据边界

- [UG1399 2024.2：Initializing and Resetting Arrays](https://docs.amd.com/r/2024.2-English/ug1399-vitis-hls/Initializing-and-Resetting-Arrays)：局部数组初始化需要每次调用执行写入。此次优化删除首次读取前必定覆盖的预写，不改变每批历史重建。
- [UG1399 2024.2：Improving Synthesis Runtime and Capacity](https://docs.amd.com/r/2024.2-English/ug1399-vitis-hls/Improving-Synthesis-Runtime-and-Capacity)：分区、展开和内联会增加综合对象及优化搜索。该一般规律不能证明上轮build-ssdm具体根因。
- 官方完整golden不修改。本地数学16配置32/32、4配置52/52；16与旧stub的214560个输入/输出行全部一致。stub对官方的既有差异保留，最终功能门槛仍须官方32/32。
- 本轮标准入口新增可选7200秒工具墙钟预算；预算届满是待定异常/未完成，不能当作死锁、算术失败或资源结论。
- 本地6条编译warning与11cd35a日志按位置归一化后完全一致；未新增worker state字段未初始化告警。第三方ap_private构造告警保持，未修改第三方头或数值通路消除告警。没有新增告警不等同已证明无未定义行为，首次读写账本与完整golden仍需核对。
