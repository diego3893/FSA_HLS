# Vitis HLS report workflow

Use this reference after selecting the build solution and report mode.

## 1. Evidence priority

Prefer generated artifacts over recollection or filenames:

| Question | Primary evidence | Supporting evidence |
|---|---|---|
| Tool, top, part, clock | top `*_csynth.xml` or `.rpt` | solution log, Tcl |
| C simulation | `csim/report/*_csim.log` | solution log |
| Synthesis | top `*_csynth.rpt` | `csynth.rpt`, solution log |
| Co-simulation | `sim/report/*_cosim.rpt` | transaction report, xsim log |
| Interface | top csynth interface table | `component.xml`, top RTL |
| Latency and II | top and relevant submodule csynth reports | transaction report |
| Resources | top csynth report | submodule reports, RTL hierarchy |
| Parallelism | instance/operator tables and generated RTL | loop schedule, pragmas, resource accounting |
| Warnings | solution log | individual tool logs |
| IP export | exported archive and `component.xml` | export log |
| Vivado timing | implementation timing reports | Vivado log |
| Board result | explicit board-test evidence | Hardware Manager/VIO/ILA captures |

Never treat HLS IP packaging as Vivado synthesis or implementation.

## 2. Select the correct build

Accept a build root, a solution directory, or a file inside a solution.

- A normal solution contains `syn/report`, and may contain `csim/report`, `sim/report`, and `impl/`.
- Select the newest complete solution when one candidate clearly dominates in completeness and timestamp.
- Do not silently combine csim, csynth, cosim, or export artifacts from different solutions.
- Record the artifact time range in the report.
- Read the HLS Tcl to identify the top function, sources, testbench, target part, clock, and enabled flow stages.
- Compare source/test/Tcl modification times with the build. When files are newer or remote paths cannot be mapped reliably, state that version consistency is uncertain.

## 3. Required calculations

Use report values without rounding them prematurely.

```text
effective_budget_ns = target_period_ns - uncertainty_ns
timing_margin_ns = effective_budget_ns - estimated_period_ns
estimated_fmax_mhz = 1000 / estimated_period_ns
resource_percent = used / available * 100
delta = current - previous
```

Clarify that HLS estimated period is not routed WNS. A positive HLS margin does not prove final implementation timing.

For co-simulation, report min/average/max latency and interval plus total execution cycles when present. Inspect the transaction report before describing a “typical” transaction; reset or exceptional transactions may skew averages.

## 4. Parallelism and inlining

Use at least two independent forms of evidence, preferably three:

1. Synthesis instance/operator hierarchy.
2. Generated RTL module instantiations or replicated operators.
3. DSP/LUT/FF accounting.
4. Loop trip count, unroll status, pipeline II, and scheduling.
5. Source pragmas and compile-time call sites.

Interpret carefully:

- `UNROLL` in source is intent until the report confirms its effect.
- A function may be inlined, so zero named instances does not imply zero hardware copies.
- A preserved function may be shared among multiple call sites, so one named module does not prove full spatial replication.
- `II=1` on a submodule means that submodule can accept new work each cycle; it does not imply the non-pipelined `ap_ctrl_hs` top accepts a new transaction each cycle.
- Resource totals alone are insufficient when unrelated operators use the same resource type. Reconcile them with the instance/operator table.

When reporting an inlined PE or lane, prefer wording such as `16套PE MAC等效运算资源` and show the resource equation. Do not claim `16个peMacUnit RTL实例` unless the RTL or hierarchy actually contains them.

## 5. Functional coverage

Read the actual testbench and list only behavior it checks through the synthesized top interface:

- reset and state initialization;
- valid/ready and block-level handshake;
- representative data paths and commands;
- independent golden-result comparisons;
- state retention, pipeline delay, and empty cycles;
- error count and return status.

If another, broader test exists but the Tcl does not add it, explicitly say it was not executed. A passing narrow test does not validate the full algorithm.

## 6. Interface review

Record every top data port with direction, width, and protocol. Highlight:

- aggregate input/output splitting;
- an apparent output reference becoming input/output ports because the function reads it;
- missing data-valid signals on `ap_none` outputs;
- block control protocol and how consumers know output validity;
- AXI or AXI-Stream depth, burst, and handshake settings when present.

Use generated RTL or `component.xml` as the authority for physical port names and widths. Use source types for semantic meaning.

## 7. Warning triage

Group repeated warnings by code and explain consequences. Prioritize:

1. possible out-of-bounds or undefined behavior;
2. unmet clock or pipeline constraints;
3. ignored partition/reshape/bind directives;
4. interface or protocol mismatches;
5. function duplication and resource-sharing surprises;
6. variable-index QoR warnings;
7. initialization/reset dependence;
8. simulation environment warnings.

Do not call a warning harmless merely because current csim or cosim passes. State when the tested vectors do not cover the warned path.

## 8. Report structure

Use the smallest structure that covers the available evidence. For a complete FSA_HLS build, prefer:

1. 综合配置
2. 流程结果
3. 功能验证范围
4. 时序与吞吐
5. 关键子模块、循环与并行性
6. 资源与存储映射
7. 接口
8. 警告与风险
9. 当前合格性结论
10. 后续工作
11. 结果文件

The conclusion should be a short qualification table followed by one paragraph. Keep recommendations tied to observed evidence.

## 9. Mode behavior

### 编写

- Create a new report from the current build.
- If the inferred output already exists, stop and ask whether to use `修改`.
- Do not copy a different module's numbers or interface as a template.

### 修改

- Read the entire existing report before editing.
- Preserve still-valid explanations, test coverage, and project context.
- Replace old timestamps, metrics, interfaces, instance counts, warnings, conclusions, and artifact lists.
- Search the finished report for stale numbers and statements.
- Add a compact previous/current table only when the old and new values are directly comparable and their provenance is clear.
- If the build is unchanged, avoid cosmetic rewriting; report that no data update was required.

## 10. Final validation

Before returning:

- confirm every referenced artifact exists;
- confirm the selected top matches the report title/module;
- check clock arithmetic and resource percentages;
- reconcile parallelism wording with hierarchy and RTL;
- ensure csim/cosim/IP/Vivado/board claims are separated;
- ensure test coverage does not exceed the Tcl-selected testbench;
- ensure Markdown headings, tables, and code fences are balanced;
- provide a clickable absolute path to the finished report.

