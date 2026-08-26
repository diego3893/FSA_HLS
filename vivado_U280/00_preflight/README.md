# 阶段 0：环境检查

执行：

```bash
vivado -mode batch -source 00_preflight/check_environment.tcl
```

终端最后出现 `PREFLIGHT_PASS` 才算通过。详细结果写入
`reports/00_preflight.txt`。若 `BOARD_PART_COUNT=0`，安装 U280 board files；若任一
`IPDEF_...=0`，先补齐 Vivado 对应 IP/器件支持。

