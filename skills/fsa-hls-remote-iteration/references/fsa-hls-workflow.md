# FSA_HLS project workflow

Use this reference only for work in the `FSA_HLS` repository. Re-read the repository's current `AGENTS.md` and scripts on every invocation; they override stale details here.

## Project boundaries

- Primary interfaces: `include/fsa/` and `include/fsa/hls/`.
- Implementations: `src/core/` and `src/hls/`.
- Host tests: `tests/`; HLS-top testbenches: `tests/hls/`.
- HLS flows: `hls/<module>/run_hls.tcl`, launched through repository-root `run_hls.sh`.
- Reports: `docs/`; generated `build/` and `hls/*/*_build/` directories are normally ignored.
- The current target is `xcvu37p_CIV-fsvh2892-2-e` at 100 MHz unless the user explicitly changes it.

Do not modify the reference Chisel/FSA projects, array size, device, clock, uncertainty, external protocol, or numeric width merely to satisfy a test.

## Local checks

On Windows, use the repository entry point:

```powershell
.\run_test.ps1 <test-name>
```

Use a focused test first. For stream-path changes, the usual upper-level regression includes:

```powershell
.\run_test.ps1 test_fsa_stream_request_top
.\run_test.ps1 test_fsa_stream_vs_legacy
.\run_test.ps1 test_fsa_dma_top
```

Use `.\run_test.ps1 all` when the change has broad impact. The default fast configuration is 4x4. Parameterized changes also require at least one compile or run at the requested target configuration, normally 128x4 when that remains the project target.

## Remote HLS entry points

From the remote repository root, run:

```bash
./run_hls.sh <module>
```

Supported names are currently `pe`, `cmp`, `input_delayer`, `output_delayer`, `sa`, `delayer_sa`, `accumulator`, `accumulator_pipeline`, `fsa_core`, `fsa_core_execute`, `fsa_core_request`, `fsa_stream_request`, `fsa_dma`, `fsa_core_full`, and `sram`. `sram` maps to the `banked_sram` directory. Confirm this list from the live script before use.

`fsa_stream_request` and `fsa_dma` currently accept these environment overrides through Tcl:

```bash
RUN_CSIM=1 RUN_COSIM=0 EXPORT_IP=0 \
FSA_SA_ROWS=4 FSA_SA_COLS=4 ./run_hls.sh fsa_stream_request

RUN_CSIM=1 RUN_COSIM=0 EXPORT_IP=0 \
FSA_SA_ROWS=4 FSA_SA_COLS=4 FSA_MAX_SEQUENCE_LENGTH=4096 \
./run_hls.sh fsa_dma
```

Do not assume other modules honor these variables; inspect their current Tcl first. Some older flows enable co-simulation and IP export directly in Tcl. Changing those switches changes test scope and must be included in the reviewed task diff.

The wrapper replaces that module's prior generated `*_build` directory and ZIP after a successful run. Run it only in the designated remote test clone, never in a directory containing the sole copy of valuable uncommitted build evidence.

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
