---
name: vivado-hls-ip-board-test
description: Generate and review a self-contained Vivado board-test package from an unpacked Vitis HLS IP, its HLS implementation sources, and its C++ testbench. Use when Codex needs to inspect component.xml or generated RTL, translate a deterministic HLS test into a synthesizable FPGA test controller, create module-specific Verilog/SystemVerilog, XDC, VIO/ILA configuration, a reusable Vivado 2024.2 project-creation Tcl script, and a board-validation report for NM37 or a comparable FPGA board. Also use to diagnose the resulting Vivado simulation, synthesis, timing, bitstream, Hardware Manager, VIO, or ILA results. Do not use for writing the HLS algorithm itself or for DMA/HBM software integration unless the user explicitly expands the scope.
---

# Vivado HLS IP board test

Create a reproducible package that the user can upload to a Vivado machine, run once to create a configured project, and then verify manually in Vivado and Hardware Manager.

## Required inputs

Obtain or locate:

1. The unpacked HLS IP repository containing `component.xml`.
2. The HLS top header and implementation.
3. The C++ HLS testbench and any types/control headers it depends on.
4. The target module and board. Default to the NM37 profile only when the user names NM37 or the current repository already establishes it.
5. Any existing Vivado wrapper, XDC, report, or known-good board test.

Ask only when a choice changes the hardware test materially, such as choosing among several test cases, testing an external AXI memory interface, or resolving an ambiguous reset/clock pin. Do not ask the user to transcribe information already present in the supplied files.

## Output contract

Generate one self-contained directory per target module:

```text
<module>_board_test/
├── config/project_config.tcl
├── ip_repo/<unpacked HLS IP>/
├── rtl/<module>_board_top.v
├── rtl/<module>_test_controller.v
├── sim/tb_<module>_board_control.v
├── constraints/<board>_board.xdc
├── scripts/create_vivado_project.tcl
└── <module>_IP上板验证说明书.md
```

Generate `project_config.tcl` automatically. Use paths relative to the package; do not require the user to edit absolute paths after upload. Keep the project-creation script module-independent and keep module-specific values in the config file and generated RTL.

Default to generation-only mode. Do not search for a Vivado installation, probe the PATH for Vivado tools, connect to a remote machine, or invoke `vivado`, `vitis_hls`, `hw_server`, synthesis, implementation, bitstream generation, or JTAG programming. The Tcl file is an output artifact for the user to run manually. Only execute a Vivado tool when the user explicitly asks for execution in the current request and the environment and permissions permit it.

## Workflow

### 1. Read project instructions

Read every applicable `AGENTS.md`. Preserve the repository's selected part, clock target, control protocol, data width, and naming conventions unless the user authorizes a change.

### 2. Inspect the actual IP interface

Read `component.xml`, the generated top-level RTL, the HLS top source/header, and the C++ testbench. Follow [references/input-analysis.md](references/input-analysis.md).

Record at least:

- VLNV, generated module name, Vivado/HLS version, and target part.
- Clock, reset, and block control protocol.
- Every scalar, aggregate, AXI, and AXI-Stream port with direction and width.
- Aggregate bit layout and enum/control widths.
- Transaction sequence, latency assumptions, expected valid timing, and golden outputs.

Treat generated RTL as the final authority for physical port names and widths. Treat source structures and the testbench as the authority for meaning. Never reuse SA's `input_r[121:0]` or `output_r[131:0]` layout for another module without verifying it.

### 3. Select a board-test strategy

Prefer a finite, deterministic, self-checking test:

- Translate one representative C++ testbench case into constant transactions and independent expected results.
- Use a synthesizable controller to drive the IP and compare outputs on board.
- Expose `run_test`, `test_busy`, `test_done`, `test_pass`, and `test_fail` through VIO.
- Probe block handshake, major input/output buses, transaction index, state, done, and fail through ILA.
- Send one transaction at a time unless the test explicitly measures throughput.

Do not translate host-only testbench features such as file I/O, dynamic allocation, unbounded loops, random generators, or console output into hardware. Replace them with finite constants, counters, ROM-style case tables, and status bits.

For `ap_ctrl_hs`, hold all input data and `ap_start` stable until `ap_ready`; capture output when `ap_done` is asserted. For other protocols, generate a protocol-specific controller instead of forcing the `ap_ctrl_hs` template.

### 4. Generate the package

Copy and fill the templates under `assets/templates/`:

- `create_vivado_project.tcl`: copy unchanged unless the Vivado flow itself differs.
- `project_config.tcl`: replace every `@...@` placeholder from inspected files.
- `nm37_board.xdc`: use only for NM37; otherwise generate a verified board XDC.
- `board_top_ap_ctrl_hs.v`: adapt declarations and instances to the IP.
- `test_controller_ap_ctrl_hs_compact.v`: use only for a compact aggregate `ap_ctrl_hs` interface.
- `tb_board_control.v`: complete all module-specific declarations and instances.
- `board_test_guide.md`: explain how to create, open, simulate, build, program, run, and accept this module's test; keep a short status table and use `未执行` where appropriate.

Make instance names in the generated RTL exactly match `project_config.tcl`. Configure VIO and ILA widths from the generated RTL, not from memory.

### 5. Apply the board profile

For NM37, read [references/nm37-profile.md](references/nm37-profile.md) completely. Preserve the 100 MHz target. Use the verified VU37P pins `BH42/BJ42` for the differential clock and `BF2` for active-low reset. Do not copy stale `BM43/BM42` text from older notes.

Use direct external reset assertion and synchronous release. Do not combine `reset_n` and Clocking Wizard `locked` through a LUT that directly drives asynchronous reset pins. Mark the asynchronous board reset as a false path. Do not add a second `create_clock` when Clocking Wizard already supplies the input-clock constraint.

### 6. Verify locally without Vivado

Perform all checks that do not require the remote tool:

- Confirm no template placeholder remains.
- Confirm referenced files exist and relative paths resolve.
- Confirm top names, instance names, ports, directions, and widths agree across RTL, testbench, config, and `component.xml`.
- Confirm the test controller holds `ap_start` and inputs through acceptance.
- Confirm reset polarity and XDC port names match the board top.
- Confirm VIO/ILA probe counts and widths match their instances.
- Confirm the testbench has a finite timeout and a runtime long enough to reach PASS/FAIL.
- Keep generated text in UTF-8 and source/Tcl files in LF form.

Clearly state that Vivado syntax, simulation, synthesis, timing, and hardware behavior remain unverified until run remotely.

### 7. Produce the validation guide

Follow [references/report-and-troubleshooting.md](references/report-and-troubleshooting.md). Write a user-facing module guide containing upload instructions, the exact Tcl command, Vivado GUI steps, VIO/ILA usage, acceptance criteria, and troubleshooting. Also separate these conclusions:

1. Package generated and statically checked.
2. Vivado behavioral simulation passed.
3. Synthesis passed.
4. Implementation and timing passed.
5. Bitstream generated.
6. Device programmed.
7. On-board fixed-vector test passed.

Never use one conclusion as proof of another. A successful board result requires `test_done=1`, `test_pass=1`, and `test_fail=0`; normally `test_busy=0` after completion.

## Remote usage to include in handoff

The generated package should run without absolute-path edits:

```bash
vivado -mode batch \
  -source scripts/create_vivado_project.tcl \
  -tclargs config/project_config.tcl
```

Then open:

```bash
vivado vivado_project/<project-name>.xpr
```

The user performs simulation, synthesis, implementation, bitstream generation, Hardware Manager programming, and VIO/ILA observation on the remote Vivado machine.

## References

- Read [references/input-analysis.md](references/input-analysis.md) when deriving interfaces and translating the C++ testbench.
- Read [references/nm37-profile.md](references/nm37-profile.md) for NM37 clock, reset, part, XDC, and board-specific warnings.
- Read [references/workflow.md](references/workflow.md) for the complete Vivado-to-hardware sequence and acceptance gates.
- Read [references/report-and-troubleshooting.md](references/report-and-troubleshooting.md) when interpreting logs, reports, VIO, or ILA.
- Read [references/sa-example.md](references/sa-example.md) only when generating or diagnosing the current 4x4 SA example.
