---
name: vitis-hls-build-report
description: Read a Vitis HLS build or solution directory and create or update a concise synthesis report from the actual C simulation, synthesis, co-simulation, interface, hierarchy, timing, latency, II, resource, warning, and IP-export artifacts. Use when the user supplies an HLS build directory and asks to 编写/新建 or 修改/更新 a 综合报告. Do not use for changing the HLS algorithm, rerunning synthesis, Vivado implementation, or FPGA board testing unless the user separately requests that work.
---

# Vitis HLS build report

Create evidence-based Markdown reports from existing Vitis HLS artifacts. Support arbitrary Vitis HLS projects while following the established Chinese report conventions when the target is `FSA_HLS`.

## Required choices

Obtain two inputs:

1. A build directory, solution directory, or a path inside one.
2. A mode: `编写` for a new report or `修改` for an existing report.

If the mode is missing, ask the user to choose before writing. Do not infer it from whether a likely report already exists.

For `编写`, use a user-specified output path. If none is given and the project has `docs/`, propose `docs/<模块名>综合报告.md`; if that file already exists, ask whether to switch to `修改`.

For `修改`, use a user-specified report when available. Otherwise match the module/build name to reports under `docs/`. Ask when more than one report is plausible.

## Workflow

1. Read every applicable `AGENTS.md` and preserve project-specific language, clock, device, interface, and reporting conventions.
2. Resolve the supplied directory to the relevant solution. Prefer the newest complete solution only when the choice is unambiguous. If several solutions are similarly plausible, show them and ask the user to select.
3. Run `scripts/inspect_hls_build.py <directory>` when Python is available. Treat its JSON as an inventory and arithmetic aid, not as the final interpretation.
4. Read the selected top-level `*_csynth.rpt` or XML, C simulation log, co-simulation report, transaction report, solution log, exported `component.xml`, and generated RTL needed for hierarchy checks.
5. Read the HLS Tcl flow, top source, relevant implementation sources, and actual testbench when they are available. Compare their timestamps with the build and mark the result potentially stale when correspondence cannot be established.
6. Follow [references/report-workflow.md](references/report-workflow.md) for evidence priority, calculations, parallelism checks, report structure, and mode-specific behavior.
7. Write only the report. Preserve useful content and structure in `修改` mode, replacing stale metrics and conclusions rather than appending contradictory snapshots.
8. Validate the finished report: referenced artifacts exist, current numbers agree with the build, old conclusions are removed, Markdown fences/tables are balanced, and validation scope is stated precisely.

## Non-negotiable distinctions

- C simulation pass, C synthesis completion, C/RTL co-simulation pass, IP export, Vivado implementation, and FPGA board validation are separate results.
- A local loop or submodule `II=1` does not prove top-level transaction `II=1`.
- Do not judge spatial parallelism from source `UNROLL`, a function name, top II, or DSP count alone. Cross-check synthesis hierarchy or RTL instances, operator/resource replication, loop scheduling, and inlining.
- If a function wrapper was inlined, say so and use replicated operators plus resource accounting rather than claiming named RTL instances that do not exist.
- Label derived values such as effective timing budget, timing margin, Fmax, percentages, and before/after deltas as calculations.
- Report missing or ambiguous evidence as `未执行`, `未发现`, or `无法确认`; never upgrade it to a pass.

## Safety and scope

- Default to read-only inspection of build artifacts and source files.
- Do not rerun Vitis HLS, Vivado, simulation, synthesis, implementation, bitstream generation, or board programming unless the user explicitly requests execution.
- Do not modify source code, testbenches, Tcl files, build products, clock constraints, target devices, or interfaces while preparing a report.
- Do not invoke a board-test workflow merely because an exported IP exists.

## FSA_HLS defaults

When the target repository is `FSA_HLS`:

- Write concise Chinese Markdown for a reader with undergraduate-level background.
- Put new reports under `docs/` and use `<模块名>综合报告.md` unless the user specifies otherwise.
- Include the latest-build comparison table in `修改` mode only when the old report contains directly comparable metrics.
- Explicitly check 100 MHz timing against target period and clock uncertainty without changing either setting.
- Use `./run_hls.sh <module>` only as a documented rerun command, not as authorization to execute it.

