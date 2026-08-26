# 阶段 3：XDMA官方例程

执行：

```bash
vivado -mode batch -source 03_xdma/create_example_project.tcl
```

脚本配置与原FSA相同的Gen3 x8、GTY Quad 227、PCIE4C_X1Y0、256-bit AXI、
32 MiB AXI-Lite aperture及复位相关基线，然后调用 `open_example_project`。

之后打开 `build/03_xdma_example/` 中生成的 XPR，按官方 example design流程生成比特流并
上板。验收必须同时检查 `lspci -vv` 的 `8GT/s, Width x8` 和 `/dev/xdma*`设备节点。

