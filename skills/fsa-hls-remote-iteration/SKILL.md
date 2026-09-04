---
name: fsa-hls-remote-iteration
description: Modify FSA_HLS locally, publish only the task changes, use a configured SSH Vitis or Vivado server to test the exact commit, maintain a per-invocation research log, and iterate until acceptance or a user-set iteration limit. Use when the user requests remote execution and authorizes the Git push and SSH test loop. Do not use for local-only edits or read-only build-report work.
---

# FSA HLS remote iteration

Drive an evidence-based edit/test loop for `FSA_HLS`. Treat the user's requested behavior, performance target, and validation level as the acceptance contract; do not silently weaken any of them to obtain a pass.

## Establish the run contract

Read every applicable `AGENTS.md`, then obtain or discover:

- the local repository root, target branch, and allowed files;
- an SSH config host alias, the absolute remote repository path, and any toolchain initialization command;
- concrete acceptance criteria, the affected HLS module, required test stages, and the local report path;
- an optional maximum iteration count. When omitted, continue until acceptance or a defined blocker.

Use an SSH host alias with key-based authentication. Every remote SSH command or session must invoke Bash and explicitly load `~/.bashrc` before any repository, Git, toolchain, or test command; do not rely on implicit login-shell behavior. Apply any additional user-specified toolchain initialization after `.bashrc` has loaded. Never request, store, print, or commit a password, private key, token, or license secret. If a missing branch, server path, hardware target, clock, interface, or test level would materially change the work, ask before mutating code or Git state.

For repository-specific commands, module names, artifacts, and test selection, read [references/fsa-hls-workflow.md](references/fsa-hls-workflow.md). For the required per-invocation record, read and follow [references/iteration-log-template.md](references/iteration-log-template.md).

## Use standing authorization for the invocation

Invoking this skill with the target repository, branch, SSH host/path, acceptance criteria, and test scope grants standing user authorization for the full in-scope iteration loop. Do not ask the user again before:

- reading or editing task-scoped local files and maintaining `docs/修改日志/`;
- running local builds and C/C++ tests;
- staging only reviewed task files, creating task commits, and normally pushing the agreed branch;
- connecting to the agreed SSH host, loading `~/.bashrc`, inspecting the remote repository, and running `git pull --ff-only`;
- running the agreed Vitis/Vivado commands, polling long jobs, and reading or copying their logs, reports, and generated evidence;
- deleting the verified remote repository root's `run_hls.sh` under the exact pull-conflict exception defined below;
- updating and publishing the invocation log and final task documentation.

Ask for user authorization only before directly deleting a file other than the exact preauthorized remote `run_hls.sh`. State the resolved path and reason before requesting that authorization. Do not broaden one approval to other files, directories, wildcards, recursive deletion, or cleanup. Expected replacement of ignored per-module build outputs performed internally by the already authorized repository `run_hls.sh` test flow does not require a separate conversational authorization prompt.

This standing authorization does not permit force push, history rewriting, reset, stash, checkout-based discarding, `git clean`, unrelated external writes, changes outside the task scope, or other destructive operations; keep the existing prohibitions. A question needed to resolve a missing design choice or ambiguous target is clarification, not an authorization prompt, and may still be necessary.

Skill instructions cannot suppress approval required by the Codex host, sandbox, operating system, SSH policy, or another higher-level security control. If the execution environment itself requires approval for an otherwise preauthorized action, use that required mechanism once and continue automatically after approval rather than asking a duplicate conversational question.

## Maintain the research log

Before the first source edit, create `docs/修改日志/` if needed and create one new Markdown file there for this invocation. Use `YYYY-MM-DD_HHMM_<任务简称>_修改日志.md`, choosing a concise filesystem-safe task name. Keep using that same file until this invocation ends. Start a new file for a later invocation unless the user explicitly asks to resume a named log.

Write the requested outcome and measurable acceptance criteria before editing. Update the log immediately after every material event: planned round, actual file changes, local tests, pushed/tested commit, remote command and configuration, test result, evidence paths and metrics, diagnosis, and the next intended modification. Record failed and abandoned approaches; do not rewrite earlier rounds to make the path appear cleaner. Correct mistakes with a dated correction note.

Keep entries useful for later paper writing: explain the engineering hypothesis and why the evidence supports the next decision. Summarize large logs and link their paths rather than pasting excessive raw output. Do not record secrets or sensitive connection material.

Define an iteration by its complete analysis boundary:

- Iteration 1 starts immediately when the skill is invoked.
- Iteration N, for N greater than 1, starts only after the previous round's remote test data have been read and that round's analysis has closed with explicit lists of resolved and remaining problems.
- The active iteration includes all preflight, inspection, planning, local edits, local tests, Git publication, remote execution, retries, evidence collection, and analysis performed inside those boundaries.
- The iteration ends only after the current round's remote test data have been read and analyzed, and the log states what was resolved, what remains, whether the acceptance criteria were met, and what the next modification should be.

A remote command finishing is not by itself the end of an iteration. If no meaningful remote test data can be obtained because of an infrastructure blocker, mark the active round `未完成（阻塞）`; do not count it as a completed analysis loop. Documentation-only commits do not start a new iteration.

Accept an optional positive maximum iteration count from the user. Check the limit only after closing the current round. When the number of completed rounds reaches the maximum, do not start another round; finish the log with the latest remote evidence, resolved and remaining problems, and the exact next proposed modification.

## Preserve the starting state

Before editing, record the local branch, `HEAD`, remotes, staged diff, unstaged diff, and untracked files. Classify pre-existing changes separately from task changes. Preserve them even when they overlap; inspect the overlap and make the smallest compatible edit.

Never use `git add -A`, `git add .`, force push, history rewriting, `git reset --hard`, `git clean`, or checkout-based discarding. Stage only explicitly reviewed task paths with `git add -- <paths>`, including the invocation log as a task file, then inspect the staged diff before committing. Do not include pre-existing staged changes in a task commit unless the user explicitly places them in scope.

On the server, require the specified repository, expected remote URL, expected branch, and a clean tracked worktree before pulling. Generated ignored build artifacts may exist. If tracked remote changes are present, stop without stashing, resetting, cleaning, or overwriting them, except for the narrowly authorized `run_hls.sh` pull-conflict rule below.

If and only if `git pull --ff-only` is blocked by the remote repository root's `run_hls.sh`, the user preauthorizes deleting that exact remote file without another approval prompt. First verify that `git rev-parse --show-toplevel` equals the configured remote repository path and that the pull error plus `git status --porcelain -- run_hls.sh` identify `run_hls.sh` as the obstructing path. Resolve the deletion target to exactly `<verified-repository-root>/run_hls.sh`, record its status in the invocation log, delete only that file, and retry `git pull --ff-only`. Do not use a wildcard, recursive deletion, `git clean`, reset, stash, or checkout. If another path is involved or the retry remains blocked, stop and report the remaining conflict.

## Iterate to acceptance

1. Convert the request into observable acceptance checks, create the invocation log, and choose the smallest test ladder that can prove the checks. Record separate functional, synthesis, timing, co-simulation, implementation, and board gates when applicable.
2. For the first round, mark its start at skill entry. For each later round, start it only after the prior round's remote evidence and analysis closure are complete. Open the numbered log entry with the inherited remaining problems, current evidence, hypothesis, and planned change.
3. Inspect the affected implementation, public interfaces, callers, tests, and HLS Tcl. Make a focused local change and add or strengthen a test that would fail without it. Record the actual changed files and intent.
4. Run the quickest relevant local C++ tests first. Run upper-level regressions when the change touches public arithmetic, tokens, PE/CMP behavior, stream scheduling, DMA behavior, or parameterized code. Record commands, exit codes, and conclusions.
5. Review the task diff. Commit only task files and the current log, then push only the agreed branch. Never force push. Record the pushed commit hash as the round's tested commit.
6. Connect non-interactively with SSH, invoke Bash, and explicitly source `~/.bashrc`. In the remote repository, check branch and cleanliness, run `git pull --ff-only`, apply the exact-file `run_hls.sh` deletion exception only when its verified conditions hold, and verify remote `HEAD` equals the pushed tested commit before testing.
7. After `.bashrc` is loaded, initialize any additional remote toolchain environment and run the agreed command. Capture the `.bashrc` load, command, environment/configuration, start/end time, exit code, stdout/stderr, and paths to generated reports. Preserve pipeline exit status when output is also written to a log.
8. Read all remote data needed for the agreed validation level. Immediately update the same numbered log entry with results, evidence, diagnosis, acceptance status, resolved problems, remaining problems, and the next modification. This completed analysis closes the iteration. Read the actual failing log or report and identify the narrowest supported cause. Do not respond by relaxing golden outputs, tolerances, device, clock, uncertainty, array size, interface, or required validation stage unless the user changes the acceptance contract.
9. After closing the round, check acceptance and the maximum iteration count. If acceptance is incomplete and another round is allowed, return to step 2. If every gate passes, the maximum has been reached, or a blocker prevents the active round from closing, finalize the log and write or update any separate requested report from the available remote evidence.

Keep the log durable in Git. The implementation commit sent for remote testing contains the log through that round's plan, changes, and local results. After remote testing, update the log immediately. Carry that update into the next implementation commit; when the invocation ends, create and push a final documentation-only commit if the last remote result is not yet committed and Git push remains authorized. Record the exact tested code commit separately from the final log commit, and do not claim the documentation-only commit was HLS-tested.

Do not impose an arbitrary iteration count when the user has not set one. Honor a user-set maximum exactly by counting only rounds closed through remote-data analysis. Stop and finalize the log after the round that reaches the limit, or report a blocker when the same authentication, network, license, disk, queue, or toolchain failure recurs three times without new evidence; when the remote repository is dirty; when required authorization is absent; or when a new design decision is needed from the user. If a blocker prevents remote-data analysis, distinguish completed rounds from the active incomplete round.

## Interpret results precisely

Keep these claims separate: local C++ test pass, Vitis C simulation pass, C synthesis completion, C/RTL co-simulation pass, IP export, Vivado synthesis, implementation/timing, bitstream generation, device programming, and on-board validation. Never use an earlier stage as proof of a later stage.

For HLS changes, verify the actual reports needed by the acceptance criteria: target and estimated clock, latency and II, loop schedule, hierarchy, operator replication, DSP/LUT/FF/BRAM/URAM, warnings, DATAFLOW/FIFO evidence, and interface. Source pragmas or a top-level II alone do not prove spatial parallelism or transaction throughput.

Long-running remote jobs may be left in a clearly named server-side log or job scheduler only when the user-authorized environment supports it. Poll without starting duplicate builds, keep the user informed, and always associate results with the tested commit.

## Deliver the result

Finalize the invocation log whether the outcome is pass, iteration-limit stop, or blocker. The log is the chronological research record and does not replace a separate synthesis or final report unless the user says so. Write a concise Chinese final report for `FSA_HLS` unless the user requests another language or format. Include:

- the acceptance criteria and final status of each gate;
- local and remote repository paths, branch, and exact tested commit;
- commands, tool versions, array/device/clock configuration, and test scope;
- the invocation log path, iteration count and limit, failures encountered, code changes made in response, and final evidence;
- timing, latency/II, resources, warnings, and artifact paths when relevant;
- every unexecuted or unresolved validation stage.

Do not call the task complete while an agreed test is failing or unexecuted. When stopping at the maximum iteration count, state that the run ended at its limit rather than claiming acceptance. Leave unrelated pre-existing worktree changes untouched and report their continued presence.
