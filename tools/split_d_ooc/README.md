# Split-D的OOC物理对照

用于已经通过正式CSim/CSynth/CoSim并导出IP的4×4/head16 Split-D；不生成bitstream，不代表完整系统或板测。

## 1 固定边界

VU37P、100MHz，setup uncertainty为2.7ns。全部AXI/AXI-Lite输入（包括复位）视为同一时钟域的同步接口，input/output max delay为2ns、min delay为0ns；没有false path。Hold由实际时钟模型分析，未额外施加2.7ns hold uncertainty。外部器件的真实延迟、异步复位释放和HBM集成需要后续系统约束验证。

时钟源假定为器件库存中实际存在的`BUFGCE_X0Y48`（SLR0、X4Y2），通过`HD.CLK_SRC`让工具估计OOC时钟延迟/偏斜。它不是后续完整系统的真实时钟位置；系统集成时必须替换为实际位置。用户已确认本次先验收内部与通信，边界hold留完整系统；min0ns约束和全部失败记录保留，不能称完整OOC时序通过。初次未指定该属性的auto结果仅为探索基线，不能冒称完整物理验收。

## 2 自动布局

在服务器仓库根目录执行（先显式加载`~/.bashrc`）：

```bash
vivado -mode batch -source tools/split_d_ooc/run.tcl -tclargs \
  hls/fsa_stream_split_d/fsa_stream_split_d_build/solution1/impl/verilog \
  build/split_d_ooc_b3e4957/auto auto
```

脚本读取正式导出的RTL及HLS生成的floating-point IP配置Tcl，保留层次以辨认两个worker；直接OOC综合整个模块，不连接小RAM测试载体。输出路径必须不存在，避免覆盖证据。输出synthesis/routed checkpoint、setup/hold、DRC、路由、拥塞、扇出及全部primitive位置。IP ZIP和component.xml的SHA与被测版本须另行归档。

本次旧auto在补齐时钟假设前已生成；`auto_context`模式打开同级`auto/synthesized.dcp`，设置当前时钟假设后重新实现，不重做综合。对照使用auto_context，旧auto仅作探索。新目录必须不存在。

## 3 区域对照

读取自动布局的实际primitive位置、关键路径和资源分布后，生成并审查同目录下`regions.tcl`。通过`regions_hard`模式（`regions`为兼容模式；旧提交的失败soft结果仍保留）从同一个auto/synthesized.dcp开始，保留相同约束与实现directive；所有区域必须解释其实际资源来源。不预先强制两个worker跨SLR。区域实现目录与auto同级，例如`build/split_d_ooc_b3e4957/regions_hard`。

`make_regions.py <primitive_locations.tsv> <regions.tcl> <plan.json>`从完整worker实例的实际SLICE/DSP/RAMB占用生成边界并记录SHA。本次两个hard区域的边界可以重叠，IS_SOFT=false、CONTAIN_ROUTING=false；它们约束模块位置，不代表互斥保留资源或限制所有布线。Vivado2024.2实测设置CONTAIN_ROUTING会重置IS_SOFT，必须先设置CONTAIN_ROUTING，再设置IS_SOFT，并核对实现前后XDC。DSP/RAMB的tile对齐范围以最终XDC为准，位置核查仍保留原始seed边界并限制扩展幅度。

`lock_buffers.tcl`在route后对placer已选择的BUFG site显式固定LOC，消除HDOOC-4，保持控制接线。`finalize_buffers.tcl <routed.dcp> <new dir>`对已有自动布局仅固定同样元数据，前后时序和层次资源必须一致，不执行opt/place/route。两个Accumulator的ap_ce BUFG属于高扇出控制缓冲，不是新算法时钟。

只有真实长连接成为瓶颈时才修改通信流水，并重新执行正式HLS全流程及新的OOC对照。若瓶颈在块内算术，记录该规模下通信插级缺乏依据，不为制造收益而插级。

## 4 报告复核

`inspect.tcl <routed.dcp> <new diagnostic dir>`只读检查内部setup/hold、全部负hold endpoint及时钟源、blackbox，不改变约束或网表。诊断目录放在实现目录下的diagnostic（重试为diagnostic_retry），完整归档并解压后执行：

```bash
python tools/split_d_ooc/analyze.py <report directory> --output <summary.json>
```

脚本抽取setup/hold/pulse、12项check_timing、route和DRC，同时记录原始报告SHA。它成功运行只代表读取成功，JSON中的gate才表示验收状态。即使退出码0，负WHS仍使`full_ooc_gate=false`；本次用户范围的`internal_ooc_gate`另行检查内部setup/hold、全部负hold起点、route、约束覆盖、模型、DRC及DSP。区域候选另加`--regions-plan <plan.json>`，必须实际hard且全部受约束primitive在实际合法范围内；soft失败候选不能因内部时序为正而通过。通信正确性/实际并行还需正式CoSim与profiling证据。时钟源假设及未执行的完整系统验收须人工核对。
