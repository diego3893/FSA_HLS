# 阶段 6：完整 XDMA+HBM+FSA 工程

确保 `ip_repo/`中已经有阶段1导出的U280 IP，然后执行：

```bash
vivado -mode batch -source 06_fsa_system/create_project.tcl
vivado -mode batch \
  -source scripts/build_and_report.tcl \
  -tclargs build/06_fsa_system/fsa_u280_06_fsa_system.xpr
```

`build_and_report.tcl`会运行综合、实现、bitstream，并把时序、资源、DRC和CDC报告写入
`reports/fsa_u280_06_fsa_system/`。只有命令打印`BUILD_PASS`才进入上板步骤。

