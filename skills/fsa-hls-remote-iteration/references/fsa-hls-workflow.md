# FSA_HLS project workflow

Use this reference only for work in the `FSA_HLS` repository. Re-read the repository's current `AGENTS.md` and scripts on every invocation; they override stale details here.

## Project boundaries

- Primary interfaces: `include/fsa/` and `include/fsa/hls/`.
- Implementations: `src/core/` and `src/hls/`.
- Host tests: `tests/`; HLS-top testbenches: `tests/hls/`.
- HLS flows: `hls/<module>/run_hls.tcl`, launched through repository-root `run_hls.sh`.
- Reports: `docs/`; generated `build/` and `hls/*/*_build/` directories are normally ignored.
- The current target is `xcvu37p_CIV-fsvh2892-2-e` at 100 MHz with `set_clock_uncertainty 2.7` unless the user explicitly changes it. The active module is `fsa_stream_split_d` with the parameterized `D×D` PE array; its default configuration is `PE_DIM=4`, `HEAD_DIM=16`, and the target configuration is `PE_DIM=16`, `HEAD_DIM=128`.

Do not modify the reference Chisel/FSA projects, array size, device, clock, uncertainty, external protocol, or numeric width merely to satisfy a test.

## Local checks

On Windows the repository entry point is `.\run_test.ps1 <test-name>` (it delegates to `run_stream_test.ps1`); there is also `.\run_stream_test.ps1` and `.\run_fp32_raw_fma_test.ps1`. Use a focused test first and `.\run_test.ps1 all` when the change has broad impact. Confirm the currently accepted test names from those scripts before relying on any name written here.

Before trusting any local result, check whether the affected sources can compile locally at all:

- The `stream` and `split_d` sources need Vitis headers such as `ap_int.h` that a Windows host normally lacks.
- If no working Linux environment is available (for example WSL refuses to start), local compilation is **not possible** and only structural checks remain: `git diff --check`, symbol uniqueness (`runPeArray`, `runAccumulatorColumns`, `loadElemTile`, `loadValueTile` and the result stages must each have exactly one definition), interface and Tcl review.
- State this limitation explicitly in the round log instead of implying a local pass. The remote Vitis C simulation then carries the compile check.

Parameterized changes must also be validated at the target configuration, for the current module `FSA_SPLIT_D_PE_DIM=16 FSA_SPLIT_D_HEAD_DIM=128`.

## Remote HLS entry points

From the remote repository root, run:

```bash
./run_hls.sh <module>
```

Supported names in the current `run_hls.sh` are `pe`, `pe_raw_fma`, `fp32_raw_fma`, `cmp`, `input_delayer`, `output_delayer`, `sa`, `delayer_sa`, `accumulator`, `accumulator_pipeline`, `fsa_core`, `fsa_core_execute`, `fsa_core_request`, `fsa_dma`, `fsa_stream`, `fsa_stream_split_d`, `fsa_streaming_v2`, `fsa_core_full`, and `sram` (`sram` maps to the `banked_sram` directory; `banked_sram` is accepted as an alias). `fsa_stream_request` and `fsa_streaming_dataflow` are **not** supported names. Re-read the live script before use, because this list has changed before.

The current split-D module takes its parameters only through Tcl environment checks:

```bash
FSA_SPLIT_D_PE_DIM=4 FSA_SPLIT_D_HEAD_DIM=16 ./run_hls.sh fsa_stream_split_d

FSA_SPLIT_D_PE_DIM=16 FSA_SPLIT_D_HEAD_DIM=128 \
./run_hls.sh fsa_stream_split_d
```

`FSA_MAX_SEQUENCE_LENGTH` is the third honored variable. `RUN_CSIM`, `RUN_COSIM`, and `EXPORT_IP` are **not** environment-overridable in this module: `hls/fsa_stream_split_d/run_hls.tcl` sets them directly (`RUN_CSIM 1`, `RUN_COSIM 1`, `EXPORT_IP 0`). The same is true of `fsa_stream`. Only inspect a module's Tcl to decide what it honors; never pass a variable that its Tcl ignores and then treat that as a scoped test.

Because co-simulation is on by default for this module, a round that should stop after C simulation and C synthesis must change the stage switches in the Tcl. That changes the tested artifact and must be included in the reviewed task diff and reported as part of the round's test scope. Scope the change to an environment-guarded temporary Tcl so that the repository default stays `RUN_COSIM 1`, and remove the temporary files after the run.

Regenerating a Vitis CSim testbench also needs Vitis's `ap_int.h` and `ap_fixed.h` for the testbench's include path. If those are not already present in the repository, extract them from the installed Vitis headers into a temporary path and remove that path afterwards; never commit a copy of vendor headers.

A run of `run_hls.sh` removes that module's previous `hls/<module>/<module>_build` directory and ZIP and replaces them with the new results, and fails with "HLS完成后没有找到构建目录" when `hls/<module>/build` does not exist. Only run it in the designated remote test clone, never in a directory holding the sole copy of valuable uncommitted build evidence.

## Safe Git and SSH sequence

The skill invocation is standing authorization for task-scoped local edits/tests, normal commit and push to the agreed branch, SSH connection, `.bashrc` loading, remote preflight and fast-forward pull, agreed Vitis/Vivado execution, result collection, log/report updates, and the exact `run_hls.sh` deletion exception. Do not pause for repeated conversational authorization during those actions. Ask only before directly deleting another file. Higher-level host or sandbox approval prompts remain authoritative and cannot be disabled by this workflow.

Before publishing, capture a baseline and stage named paths only:

```text
git status --short --branch
git diff --cached
git diff
git add -- <reviewed-task-paths>
git diff --cached
git commit -m <task-specific-message>
git push <remote> <local-branch>:<remote-branch>
```

Use a configured host alias and non-interactive authentication. Every SSH command must explicitly invoke Bash and source `~/.bashrc` before any repository or tool command; never assume a non-interactive or login shell loaded it automatically. Compose remote commands so that the repository path is a separately quoted fixed input, `.bashrc` loading is checked, the shell then uses `set -euo pipefail`, and every repository command runs inside the verified repository. A typical logical sequence is:

```text
ssh -o BatchMode=yes <host-alias> "bash -lc 'source ~/.bashrc || exit $?; set -euo pipefail; <preflight command>'"
ssh -o BatchMode=yes <host-alias> "bash -lc 'source ~/.bashrc || exit $?; set -euo pipefail; <pull and exact-HEAD verification command>'"
ssh -o BatchMode=yes <host-alias> "bash -lc 'source ~/.bashrc || exit $?; set -euo pipefail; <additional toolchain initialization and test command>'"
```

The preflight must check `pwd`, `git rev-parse --show-toplevel`, remote URL, branch, `HEAD`, and `git status --porcelain`. Use `git pull --ff-only`; never resolve a remote merge automatically. Compare `git rev-parse HEAD` on both machines before accepting test evidence.

The user has preauthorized one exact pull-conflict recovery: when the verified repository root's `run_hls.sh` is the sole file identified by the pull failure and its scoped Git status, delete exactly `<verified-repository-root>/run_hls.sh` and retry `git pull --ff-only` without requesting approval. Log the original error, scoped status, resolved deletion path, deletion, and retry result. This authorization does not cover any other file or any recursive cleanup. If the deleted file was tracked and the pull remains blocked, stop; do not add an unrequested restore, reset, checkout, stash, or clean operation.

Prefer an SSH config entry and agent/key authentication over command-line connection secrets. Do not disable host-key checking. If first-use trust is required, present the server fingerprint to the user for verification rather than accepting it blindly.

## Test ladder

Choose only the stages required to prove the request, in increasing cost:

1. focused local C++ test;
2. affected local top-level and regression tests;
3. remote Vitis C simulation;
4. remote C synthesis and report inspection;
5. C/RTL co-simulation for RTL behavior, DATAFLOW stalls, or deadlock risk;
6. IP export only when a downstream Vivado or delivery task needs it;
7. Vivado synthesis/implementation and timing when HLS estimates are insufficient;
8. bitstream/programming/on-board testing only when explicitly requested and authorized.

For stream work, cover applicable single/multiple KV tiles, initialize/finalize combinations, causal and cross-tile causal cases, `active_keys` boundaries, non-full tiles, reset, consecutive requests, numerical golden outputs, and legacy boundary comparison. An HLS testbench must call the corresponding top function.

## Evidence and report paths

After `run_hls.sh <module>`, locate the actual generated directory rather than assuming an old copied path. It is normally:

```text
hls/<subdir>/<module>_build/solution1/
```

Inspect, when present:

- `csim/report/*_csim.log`;
- `syn/report/*_csynth.rpt` and `*_csynth.xml`;
- submodule and loop reports under `syn/report/`;
- `solution1.log`;
- `sim/report/*_cosim.rpt`, RTL logs, and transaction reports;
- generated RTL and exported `component.xml` or IP ZIP;
- Vivado utilization/timing reports for implementation claims.

For a synthesis report, follow the repository's `vitis-hls-build-report` skill when available. Mark a build as potentially stale unless its tested commit and source/Tcl/testbench correspondence can be established.

## Co-simulation wall-clock contract

The repository-root `PROJECT_CONTEXT.md` holds the currently effective one; follow it when it is stricter. As recorded there for the `PE_DIM=16`, `HEAD_DIM=128` configuration on the NM37 server with Vitis HLS 2024.2 and the current 7-transaction testbench:

- measure from `## run all` until all transactions finish, RTL simulation exits, and the C post-check completes; exclude C simulation, C synthesis, Verilog compile, and `xelab`;
- 15 to 45 minutes is the normal window, 45 to 60 minutes is a warning window, and past 60 minutes the round is a co-simulation timeout and fails performance/verifiability; do not keep waiting merely because some transactions finished;
- no transaction or intra-transaction progress for 20 minutes is an early timeout: save the last progress and logs, then stop;
- a timeout is not a numerical failure or a deadlock. Report completed transactions, last progress, whether a deadlock report exists, whether the C post-check ran, and mark the data as unverified.

The 4×4 configuration completes the same flow in minutes, so this contract does not gate it. Recalibrate and get user confirmation before changing the threshold for a different server, tool version, or testbench.
