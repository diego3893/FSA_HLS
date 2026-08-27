# 阶段 1：重新导出 U280 HLS IP

先运行`vitis_hls -version`并确认是2020.2。本阶段必须重新导出2020.2 IP，不能把2024.2
导出的IP直接放入最终2020.2工程。

执行：

```bash
vitis_hls -f 01_hls_ip/run_hls_u280.tcl
python3 01_hls_ip/unpack_ip.py
```

第一条命令依次运行 C simulation、C synthesis、RTL co-simulation 和 IP export。
第二条命令把导出内容放入 `ip_repo/fsa_dma_top/`。必须确认：

- C simulation日志出现 `[PASS] test_fsa_dma_top`；
- `solution1/syn/report/fsa_dma_top_csynth.rpt` 中目标时钟为 10 ns；
- `solution1/sim/report/fsa_dma_top_cosim.rpt` 的 Verilog结果为 PASS；
- `python3`命令最后打印 `IP_UNPACK_PASS=...component.xml`；
- 新IP的 ReleaseNotes目标器件是 `xcu280`，不是`xcvu37p_CIV`。
