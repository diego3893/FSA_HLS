# Split-D的OOC物理对照

用于已经通过正式CSim/CSynth/CoSim并导出IP的4×4/head16 Split-D；不生成bitstream，不代表完整系统或板测。

## 1 固定边界

VU37P、100MHz，setup uncertainty为2.7ns。全部AXI/AXI-Lite输入（包括复位）视为同一时钟域的同步接口，input/output max delay为2ns、min delay为0ns；没有false path。Hold由实际时钟模型分析，未额外施加2.7ns hold uncertainty。外部器件的真实延迟、异步复位释放和HBM集成需要后续系统约束验证。

时钟源假定为器件库存中实际存在的`BUFGCE_X0Y48`（SLR0、X4Y2），通过`HD.CLK_SRC`让工具估计OOC时钟延迟/偏斜。它不是后续完整系统的真实时钟位置；系统集成时必须替换为实际位置。初次未指定该属性的auto结果仅为探索基线，不能冒称完整物理验收。

## 2 自动布局

在服务器仓库根目录执行（先显式加载`~/.bashrc`）：

```bash
vivado -mode batch -source tools/split_d_ooc/run.tcl -tclargs \
  hls/fsa_stream_split_d/fsa_stream_split_d_build/solution1/impl/verilog \
  build/split_d_ooc_b3e4957/auto auto
```

脚本读取正式导出的RTL及HLS生成的floating-point IP配置Tcl，保留层次以辨认两个worker；直接OOC综合整个模块，不连接小RAM测试载体。输出路径必须不存在，避免覆盖证据。输出synthesis/routed checkpoint、setup/hold、DRC、路由、拥塞、扇出及全部primitive位置。IP ZIP和component.xml的SHA与被测版本须另行归档。

## 3 区域对照

读取自动布局的实际primitive位置、关键路径和资源分布后，生成并审查同目录下`regions.tcl`。通过`regions`模式从同一个auto/synthesized.dcp开始，保留相同约束与实现directive；所有区域必须解释其实际资源来源。不预先强制两个worker跨SLR。区域实现目录与auto同级，例如`build/split_d_ooc_b3e4957/regions`。

只有真实长连接成为瓶颈时才修改通信流水，并重新执行正式HLS全流程及新的OOC对照。若瓶颈在块内算术，记录该规模下通信插级缺乏依据，不为制造收益而插级。
