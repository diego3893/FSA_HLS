# 阶段 5：XDMA-HBM回环

创建并构建：

```bash
vivado -mode batch -source 05_xdma_hbm/create_project.tcl
vivado -mode batch \
  -source scripts/build_and_report.tcl \
  -tclargs build/05_xdma_hbm/fsa_u280_05_xdma_hbm.xpr
```

下载比特流并完成驱动绑定后：

```bash
bash 05_xdma_hbm/run_loopback.sh 0
```

终端最后出现 `XDMA_HBM_LOOPBACK_PASS`才算通过。脚本使用`cmp`逐字节比较写入和读回文件。

