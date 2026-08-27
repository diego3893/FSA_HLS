# `fsa_dma_top` U280 独立工程包

本目录用于在 Vivado/Vitis HLS 2020.2 中按顺序建立：

```text
U280 PCIe Gen3 x8 XDMA
    ├── AXI-Lite -> fsa_dma_top控制寄存器和板级状态寄存器
    └── AXI-MM ─┐
                ├── SmartConnect -> HBM AXI_00
fsa_dma_top ────┘
```

所有脚本都使用相对路径。应从本目录执行命令，不需要修改绝对路径。

## 目录

```text
00_preflight/       Vivado版本、器件和IP检查
01_hls_ip/          针对U280重新综合、协同仿真和导出HLS IP
02_clock_reset/     300/100/225 MHz时钟和PCIe复位骨架
03_xdma/            官方XDMA example project生成入口
04_hbm/             官方HBM example project生成入口
05_xdma_hbm/        XDMA到HBM的正式回环工程
06_fsa_system/      接入fsa_dma_top的最终工程
07_host/            Linux主机端到端测试程序
common/             共用RTL、XDC、仿真和Block Design Tcl
config/             唯一参数源
scripts/            共用仿真、构建和报告脚本
build/              脚本运行后生成的工程
reports/            脚本运行后生成的文本报告
ip_export/          HLS导出的压缩IP
ip_repo/            解压后的U280 HLS IP
```

## 必须按顺序执行

先加载管理员确认可用的 Vivado/Vitis HLS 2020.2 环境，并确认两个版本命令都显示
`2020.2`。不要使用内部仍引用不存在的`/tools/...`旧路径的搬迁安装。

```bash
vivado -version
vitis_hls -version
```

然后在工程包目录执行：

```bash
cd FSA_HLS/vivado_U280
mkdir -p reports

vivado -mode batch -source 00_preflight/check_environment.tcl
vitis_hls -f 01_hls_ip/run_hls_u280.tcl
python3 01_hls_ip/unpack_ip.py
vivado -mode batch -source scripts/run_status_sim.tcl
vivado -mode batch -source 02_clock_reset/create_project.tcl
vivado -mode batch -source 03_xdma/create_example_project.tcl
vivado -mode batch -source 04_hbm/create_example_project.tcl
vivado -mode batch -source 05_xdma_hbm/create_project.tcl
vivado -mode batch -source 06_fsa_system/create_project.tcl
```

所有创建工程、仿真、构建和下载Tcl都会拒绝非2020.2 Vivado。U280 board-part在不同
2020.2安装中可能是不同修订号；脚本优先使用`xilinx.com:au280:part0:1.1`，没有该版本时
自动选择当前安装中可用的`xilinx.com:au280:part0:*`，始终以
`xcu280-fsvh2892-2L-e`和本目录XDC为最终器件及管脚依据。

创建阶段 5 或阶段 6 工程后，按脚本最后打印的 XPR 路径构建，例如：

```bash
vivado -mode batch \
  -source scripts/build_and_report.tcl \
  -tclargs build/06_fsa_system/fsa_u280_06_fsa_system.xpr
```

完整操作、预期输出和失败定位见：

```text
../docs/FSA_HLS_U280_XDMA_HBM迁移实施方案.md
```

## 当前验证状态

| 层级 | 状态 |
|---|---|
| 文件生成与本地静态检查 | 已完成 |
| Vivado状态寄存器行为仿真 | 未执行 |
| U280 HLS重新导出 | 未执行 |
| XDMA example design | 未执行 |
| HBM example design | 未执行 |
| XDMA-HBM工程综合/实现 | 未执行 |
| 完整工程综合/实现 | 未执行 |
| U280板上端到端测试 | 未执行 |
