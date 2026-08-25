# HLS IP放置说明

此目录已经包含解压后的 `fsa_dma_top` HLS IP：

```text
ip_repo/
└── fsa_dma_top/
    ├── component.xml
    ├── hdl/
    ├── constraints/
    └── xgui/
```

预期 VLNV 为 `xilinx.com:hls:fsa_dma_top:1.0`。

当前实际文件为 `ip_repo/fsa_dma_top/component.xml`，同时保留了原始导出压缩包
`ip_repo/xilinx_com_hls_fsa_dma_top_1_0.zip`。工程脚本直接注册本目录。

注意：HLS内部RTL仍声明5组AXI USER信号，但Vivado打包后的 `fsa_dma_top_0` wrapper
隐藏了这些可选端口。板测连接应以Vivado实际生成的wrapper/stub为准，不能把内部RTL的
`AWUSER/WUSER/ARUSER/RUSER/BUSER`直接连接到IP实例。
