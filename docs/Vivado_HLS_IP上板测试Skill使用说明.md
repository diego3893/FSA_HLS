# Vivado HLS IP上板测试 Skill使用说明

## 1. Skill的作用

`vivado-hls-ip-board-test`用于把一个已经导出的 Vitis HLS IP整理成可上传远端服务器的 Vivado板级测试包。

它从以下材料开始工作：

- 解压后的 HLS IP目录；
- HLS顶层和实现源码；
- HLS C++ testbench；
- 相关类型、控制信号和配置头文件；
- 目标模块与开发板信息。

它不负责重新实现 HLS算法，也不会在本地尝试运行远端的 Vivado。

## 2. Skill保存位置

仓库版本：

```text
FSA_HLS/skills/vivado-hls-ip-board-test/
```

本机安装版本：

```text
C:/Users/30130/.codex/skills/vivado-hls-ip-board-test/
```

仓库版本用于 Git共享和人工审查；本机版本用于让 Codex直接发现并调用。修改 Skill时应先修改仓库版本，验证通过后再同步到本机目录。

## 3. 如何调用

在对话中直接点名 Skill，并提供材料路径。例如：

```text
使用 $vivado-hls-ip-board-test，为Accumulator生成NM37上板测试包。

解压后的HLS IP：build/accumulator_build/solution1/impl/ip/
HLS顶层和实现：include/fsa/hls/accumulator_top.hpp、src/hls/accumulator_top.cpp、src/core/accumulator.cpp
HLS testbench：tests/hls/test_accumulator_top.cpp
开发板：NM37
```

也可以测试其他模块：

```text
使用 $vivado-hls-ip-board-test，为InputDelayer生成NM37上板测试包。
请读取它的component.xml、HLS源码和testbench后自动生成配置。
```

不需要手工告诉 Codex端口位宽、VLNV或 VIO/ILA probe宽度；Skill会从文件中读取。

## 4. 需要提供的材料

### 必需材料

1. 解压后的 HLS IP，其中应能找到 `component.xml`。
2. HLS顶层接口与实现。
3. 对应的 C++ testbench。
4. 目标模块名称。
5. 目标开发板。

### 建议同时提供

- 最新 HLS综合和协同仿真报告；
- 相关公共头文件；
- 已有的 Vivado wrapper、XDC或板上测试结果；
- 希望固化到 FPGA中的具体测试用例。

若 testbench有多组数据、随机数、文件输入、DMA或外存依赖，Codex可能需要你确认这次要固化哪组测试。

## 5. 生成结果

每个模块生成一个独立目录：

```text
<module>_board_test/
├── config/
│   └── project_config.tcl
├── ip_repo/
│   └── <解压后的HLS IP>
├── rtl/
│   ├── <module>_board_top.v
│   └── <module>_test_controller.v
├── sim/
│   └── tb_<module>_board_control.v
├── constraints/
│   └── nm37_board.xdc
├── scripts/
│   └── create_vivado_project.tcl
└── <module>_IP上板验证说明书.md
```

### `project_config.tcl`

该文件由 Skill自动生成，包含：

- 工程名和模块名；
- FPGA Part；
- HLS IP VLNV和实例名；
- 综合顶层和仿真顶层；
- Clocking Wizard、VIO和 ILA实例名；
- 输入/输出时钟频率；
- VIO与 ILA probe数量和宽度；
- 仿真运行时间。

正常情况下不需要手工修改。

### `create_vivado_project.tcl`

这是所有模块共用的工程创建脚本，负责：

1. 创建 Vivado工程；
2. 选择目标 Part；
3. 注册 `ip_repo/`；
4. 创建 HLS IP实例；
5. 创建并配置 Clocking Wizard、VIO和 ILA；
6. 添加 RTL、仿真文件和 XDC；
7. 设置综合顶层和仿真顶层；
8. 设置 XSim runtime；
9. 生成 IP Output Products；
10. 保存工程后退出。

它不会自动运行 Synthesis、Implementation、Generate Bitstream或 Program Device。

### 板级测试控制器

控制器把 C++ testbench中的确定性测试转换成硬件状态机：

- VIO产生 `run_test`；
- 控制器发送输入事务；
- 控制器遵守 HLS顶层控制协议；
- 控制器在硬件中比较输出和金标准；
- VIO显示 `test_busy/test_done/test_pass/test_fail`；
- ILA记录握手、输入、输出和事务编号。

不同模块的接口和 testbench不同，因此控制器由 Skill针对目标模块单独生成，不会直接复制 SA的端口或位排列。

## 6. 上传服务器并创建工程

把完整的 `<module>_board_test/`目录上传服务器。不要只上传 Tcl或 Verilog，因为脚本还需要 `config/`、`ip_repo/`和约束文件。

进入测试包根目录后执行：

```bash
vivado -mode batch \
  -source scripts/create_vivado_project.tcl \
  -tclargs config/project_config.tcl
```

脚本使用自身位置计算相对路径，因此上传后通常不需要修改路径。

创建成功后打开工程：

```bash
vivado vivado_project/<project-name>.xpr
```

服务器需要满足：

- 安装 Vivado 2024.2；
- 具有目标器件综合许可证；
- HLS IP与 Vivado版本兼容；
- 后续上板时能够连接对应 JTAG或远程 `hw_server`。

## 7. 打开工程后的手工流程

1. 运行 Behavioral Simulation，确认出现 PASS。
2. 打开 Elaborated Design，检查实例和端口。
3. 运行 Synthesis。
4. 运行 Implementation。
5. 检查 Timing Summary和 DRC。
6. Generate Bitstream。
7. 在 Hardware Manager中下载匹配的 `.bit`和 `.ltx`。
8. 用 VIO令 `run_test`执行 `0 -> 1`。
9. 查看 VIO和 ILA。

板上通过状态必须是：

```text
test_busy = 0
test_done = 1
test_pass = 1
test_fail = 0
```

## 8. NM37默认配置

当前 Skill内置的 NM37 VU37P配置为：

| 项目 | 配置 |
|---|---|
| Part | `xcvu37p_CIV-fsvh2892-2-e` |
| 时钟 | `CLK_100_DDR_P/N`，100 MHz |
| 时钟 P/N | `BH42/BJ42` |
| 时钟 IOSTANDARD | `DIFF_SSTL12` |
| 复位 | `BF2`，低有效 |
| 复位 IOSTANDARD | `LVCMOS18` |

Clocking Wizard会生成输入时钟约束，因此板级 XDC不重复写 `create_clock`。异步 `reset_n`会设置 false path，并采用异步断言、同步释放的内部复位结构。

未经允许，Skill不会通过放宽 10 ns周期或更换器件来消除时序违例。

## 9. 如何把远端结果交给 Codex

远端运行后，可以提供以下任一材料：

- Vivado Tcl Console日志；
- Behavioral Simulation输出；
- Timing Summary截图或 `.rpt`；
- Utilization、DRC和 Methodology报告；
- Hardware Manager/VIO/ILA截图；
- 生成的 Vivado工程或压缩包。

然后调用 Skill，例如：

```text
使用 $vivado-hls-ip-board-test检查这次Accumulator的Vivado日志，
更新上板验证报告并告诉我是否通过。
```

Skill生成的模块说明书会写明上传、创建工程、仿真、综合、实现、下载、VIO/ILA操作和验收标准，并用简短状态表记录：静态检查、行为仿真、综合、实现时序、bitstream、下载和板上测试。它不会把其中一个阶段通过误写成全部通过。

## 10. 当前限制

- Skill不能直接操作当前远端 Vivado服务器。
- Tcl模板尚需在远端 Vivado 2024.2中实际运行后才能确认所有 IP配置属性完全匹配该安装。
- 固定向量板上 PASS只证明当前模块、当前 IP版本和当前测试向量通过。
- 使用 AXI、DMA、DDR或 HBM的模块可能需要额外的软件或存储控制逻辑，不能只靠 VIO完成测试。
- 若 HLS顶层接口改变，必须重新生成整个模块测试包，不能只替换旧 IP。
