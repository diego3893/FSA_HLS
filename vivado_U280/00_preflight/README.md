# 阶段 0：环境检查

执行：

```bash
vivado -mode batch -source 00_preflight/check_environment.tcl
```

终端最后出现 `PREFLIGHT_PASS` 才算通过。详细结果写入
`reports/00_preflight.txt`。脚本先确认U280 Part，再创建内存工程并初始化IP Catalog；
因此只有`IP_CATALOG_CHECK=INITIALIZED`之后的`IPDEF_...`结果才有效。

`VIVADO_VERSION`和`REQUIRED_VIVADO_VERSION`都必须为`2020.2`。若版本不符，脚本在
创建任何工程前停止。`VIVADO_EXECUTABLE`和`XILINX_VIVADO`用于确认实际调用的是哪套
安装；两者必须指向同一个可用的2020.2目录。

若`TARGET_PART_COUNT=0`，查看`U280_PART_CANDIDATES`：

- `<none>`：当前Vivado安装没有Virtex UltraScale+ HBM/U280器件数据，或加载了错误安装目录的`settings64.sh`；
- 有候选但没有目标Part：核对器件字符串和已安装speed/package支持。

`BOARD_PART_SELECTED=<none>`表示U280 board files未安装；本工程有显式Part和XDC，脚本会
记为warning，但建议安装board files后再继续。若安装中只有其他U280 board-part修订号，
`BOARD_PART_SELECTED`会显示脚本实际选用的版本。
