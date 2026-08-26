# 阶段 2：时钟和复位

执行：

```bash
vivado -mode batch -source 02_clock_reset/create_project.tcl
vivado build/02_clock_reset/fsa_u280_02_clock_reset.xpr
```

打开工程后运行 Synthesis，再执行：

```tcl
open_run synth_1
report_clocks -file ../../reports/02_clock_reset/report_clocks.rpt
report_clock_interaction -file ../../reports/02_clock_reset/clock_interaction.rpt
report_io -file ../../reports/02_clock_reset/report_io.rpt
```

验收方法见主说明书阶段 2。该阶段只证明时钟、复位和管脚骨架，不证明PCIe或HBM工作。

