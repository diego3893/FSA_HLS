# 从 HLS IP到板上验证的工作流

## 阶段与验收门

### 1. 输入一致性

- 解压后的 IP包含 `component.xml`、`hdl/`、`constraints/`和 `xgui/`等导出内容。
- HLS源码、testbench和 IP来自同一版本。
- `component.xml`与导出 RTL端口一致。

### 2. 生成板级验证包

- 自动生成模块配置，不要求用户填写 VLNV、端口宽度或 probe宽度。
- 板级顶层连接 Clocking Wizard、测试控制器、HLS IP、VIO和 ILA。
- Verilog testbench绕过板级差分时钟缓冲，直接用 100 MHz仿真时钟测试控制器和 HLS IP。
- XDC只约束真实顶层物理端口。

### 3. 上传并创建工程

在完整包根目录执行：

```bash
vivado -mode batch \
  -source scripts/create_vivado_project.tcl \
  -tclargs config/project_config.tcl
```

脚本只负责创建工程、注册 IP、创建调试/时钟 IP、加入源码约束、设置顶层和生成 Output Products。

随后打开：

```bash
vivado vivado_project/<project-name>.xpr
```

### 4. Vivado行为仿真

- Simulation Top设置为生成的 `tb_<module>_board_control`。
- Runtime必须长于控制器最坏执行时间。模板默认 `20us`，但应根据事务数和 IP延迟调整。
- testbench必须自动输出 PASS/FAIL并带有限超时。
- 仿真失败时不要继续上板。

### 5. Elaborated Design

检查：

- HLS IP、Clocking Wizard、VIO、ILA和控制器实例存在。
- 所有端口宽度、方向和实例名匹配。
- 没有意外悬空的控制或数据端口。
- 综合顶层是板级顶层，而不是 testbench。

### 6. Synthesis与Implementation

分别判断：

- Synthesis完成且没有阻断错误。
- Implementation完成。
- Setup WNS `>= 0 ns`。
- Hold WHS `>= 0 ns`。
- TNS、THS为 0。
- 不存在关键未约束路径。
- DRC没有阻止 bitstream的错误。

不要把 HLS Estimated Clock当成板级时序结论。最终结论以 Vivado Implementation为准。

### 7. Bitstream与下载

- 生成同一次实现对应的 `.bit`和 `.ltx`。
- Hardware Manager识别目标 XCVU37P。
- Program Device时同时选择匹配的 `.bit`和 `.ltx`。
- 下载后按需要短按 SW2，再等待 Clocking Wizard锁定和内部复位释放。

### 8. VIO/ILA验证

- `run_test`初始为 0。
- 先让 ILA进入等待触发，再令 `run_test`执行 `0 -> 1`。
- 通过条件：`test_busy=0`、`test_done=1`、`test_pass=1`、`test_fail=0`。
- 再次测试前把 `run_test`拉回 0，然后重新产生上升沿。

## 结论分级

| 证据 | 可以得出的结论 |
|---|---|
| 静态检查完成 | 文件和接口在文本层面一致 |
| Vivado行为仿真 PASS | 板级控制器与 HLS RTL在仿真中功能正确 |
| Synthesis完成 | 工程可综合 |
| Implementation时序通过 | 该板级设计满足当前时钟约束 |
| Bitstream生成 | 配置文件可用于下载 |
| VIO固定向量 PASS | 当前固定样例在目标硬件上通过 |

固定向量 PASS不代表任意数据、所有浮点边界、峰值吞吐、DMA/HBM或完整系统已经通过。

