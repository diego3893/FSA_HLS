# HLS IP放置说明

此目录应包含解压后的 `fsa_dma_top` HLS IP，例如：

```text
ip_repo/
└── fsa_dma_top/
    ├── component.xml
    ├── hdl/
    ├── constraints/
    └── xgui/
```

预期 VLNV 为 `xilinx.com:hls:fsa_dma_top:1.0`。

当前工作区的 `build/fsa_dma_build/solution1/impl/` 缺少报告中提到的
`export.zip` 和 `component.xml`，所以这里没有伪造或复制不完整的 IP。请在 Vitis HLS
重新执行 `export_design -format ip_catalog -rtl verilog`，或从原始构建服务器取回本次
导出的 `export.zip`，解压到上述位置后再运行工程创建脚本。

不要只复制 `solution1/impl/verilog/`：该目录是实现中间文件，不等价于带
`component.xml`、XGUI和依赖元数据的完整 Vivado IP repository。

