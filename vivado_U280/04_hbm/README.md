# 阶段 4：HBM官方例程

执行：

```bash
vivado -mode batch -source 04_hbm/create_example_project.tcl
```

打开 `build/04_hbm_example/` 中生成的 XPR，生成比特流并下载。Hardware Manager中必须
看到 HBM初始化完成、CATTRIP为0、AXI_00 Traffic Generator读写完成且错误计数为0。

这个阶段只验证 HBM，不能用来证明 XDMA-HBM路径或FSA计算正确。

