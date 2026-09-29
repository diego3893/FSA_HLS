---
name: fsa-hls-remote-iteration
description: Modify FSA_HLS locally, publish only the task changes, use a configured SSH Vitis or Vivado server to test the exact commit, maintain a per-invocation research log, and iterate until acceptance or a user-set iteration limit. Use when the user requests remote execution and authorizes the Git push and SSH test loop. Do not use for local-only edits or read-only build-report work.
---

# FSA HLS remote iteration

Drive an evidence-based edit/test loop for `FSA_HLS`. Treat the user's requested behavior, performance target, and validation level as the acceptance contract; do not silently weaken any of them to obtain a pass.

## 一轮迭代的固定流程

一轮的边界是**一次"push → 远端测一次"**：从本地提交并推送开始，到读完该 commit 的远端测试数据并给出结论为止。同一 commit 的一次远端运行算一轮；不同的被测 commit 属于不同的轮。

1. **推送本地 commit**：把本轮任务文件（含本轮日志）精确暂存、提交并推送到约定分支。
2. **远端同步**：SSH 并显式加载 `~/.bashrc`，确认分支与 tracked 工作树，`git pull --ff-only`，核对远端 `HEAD` 等于刚推送的 commit。
3. **远端用 Vitis 测试**：加载工具链后执行约定命令，捕获 `.bashrc` 加载、命令、环境/参数、起止时间、退出码与产物路径。
4. **远端读结果**：读齐该验收级别需要的报告、日志、RTL 与资源数据，记录实测指标和警告。
5. **判断是否合格**：逐项对照验收标准；按下面的"结论与迭代控制"决定终止、阻塞还是继续。
6. **不合格则联网搜索解决方案**：用 `web_search`/`web_fetch` 找外部依据（工具文档、厂商建议、同类问题），把检索到的结论、来源链接和它与本地证据的关系写进本轮日志。
7. **本地更改并做本地验证**：依据上一步的证据做聚焦修改，做本地可执行的验证并记录。
8. **回到第 1 步**：该修改成为下一轮的被测 commit。

迭代在以下任一条件成立时结束，并立即按"交付结果"回报：验收全部通过；达到用户设定的最大轮数；出现阻塞；或需要用户作出新的设计决定。

被省略但必须遵守的细节如下文各节：运行合同、站立授权、每轮日志、项目上下文、起始状态保护、测试阶梯、结论与迭代控制、结果判读、交付结果。

## Establish the run contract

Read every applicable `AGENTS.md`, then obtain or discover:

- the local repository root, target branch, and allowed files;
- an SSH config host alias, the absolute remote repository path, and any toolchain initialization command;
- concrete acceptance criteria, the affected HLS module, required test stages, and the local report path;
- an optional maximum iteration count. When omitted, continue until acceptance or a defined blocker.

Use an SSH host alias with key-based authentication. Every remote SSH command or session must invoke Bash and explicitly load `~/.bashrc` before any repository, Git, toolchain, or test command; do not rely on implicit login-shell behavior. Apply any additional user-specified toolchain initialization after `.bashrc` has loaded. Never request, store, print, or commit a password, private key, token, or license secret. If a missing branch, server path, hardware target, clock, interface, or test level would materially change the work, ask before mutating code or Git state.

For repository-specific commands, module names, parameters, artifacts, and test selection, read [references/fsa-hls-workflow.md](references/fsa-hls-workflow.md). For the required per-invocation record, read and follow [references/iteration-log-template.md](references/iteration-log-template.md).

## Use standing authorization for the invocation

Invoking this skill with the target repository, branch, SSH host/path, acceptance criteria, and test scope grants standing user authorization for the full in-scope iteration loop. Do not ask the user again before:

- reading or editing task-scoped local files and maintaining `docs/修改日志/`;
- running local builds and C/C++ tests;
- staging only reviewed task files, creating task commits, and normally pushing the agreed branch;
- connecting to the agreed SSH host, loading `~/.bashrc`, inspecting the remote repository, and running `git pull --ff-only`;
- running the agreed Vitis/Vivado commands, polling long jobs, and reading or copying their logs, reports, and generated evidence;
- searching the web for a solution after a round fails acceptance, and reading the retrieved pages;
- deleting the verified remote repository root's `run_hls.sh` under the exact pull-conflict exception defined below;
- updating and publishing the invocation log and final task documentation.

Ask for user authorization only before directly deleting a file other than the exact preauthorized remote `run_hls.sh`. State the resolved path and reason before requesting that authorization. Do not broaden one approval to other files, directories, wildcards, recursive deletion, or cleanup. Expected replacement of ignored per-module build outputs performed internally by the already authorized repository `run_hls.sh` test flow does not require a separate conversational authorization prompt.

This standing authorization does not permit force push, history rewriting, reset, stash, checkout-based discarding, `git clean`, unrelated external writes, changes outside the task scope, or other destructive operations; keep the existing prohibitions. A question needed to resolve a missing design choice or ambiguous target is clarification, not an authorization prompt, and may still be necessary.

Skill instructions cannot suppress approval required by the DSH host, sandbox, operating system, SSH policy, or another higher-level security control. If the execution environment itself requires approval for an otherwise preauthorized action, use that required mechanism once and continue automatically after approval rather than asking a duplicate conversational question.

## Maintain the research log

Before the first source edit, create `docs/修改日志/` if needed and create one new Markdown file there for this invocation. Use `YYYY-MM-DD_HHMM_<任务简称>_修改日志.md`, choosing a concise filesystem-safe task name. Keep using that same file until this invocation ends. Start a new file for a later invocation unless the user explicitly asks to resume a named log.

Write the requested outcome and measurable acceptance criteria before editing. Update the log immediately after every material event: the round's planned change, actual file changes, local validation, pushed/tested commit, remote command and configuration, test result, evidence paths and metrics, diagnosis, external search findings with sources, and the next intended modification. Record failed and abandoned approaches; do not rewrite earlier rounds to make the path appear cleaner. Correct mistakes with a dated correction note.

Keep entries useful for later paper writing: explain the engineering hypothesis and why the evidence supports the next decision. Summarize large logs and link their paths rather than pasting excessive raw output. Do not record secrets or sensitive connection material.

Each round owns one `### 第N轮` section. Iteration 1 starts immediately when the skill is invoked, before preflight or source inspection. Iteration N, for N greater than 1, starts only after the previous round's remote test data have been read and its analysis has closed with explicit lists of resolved and remaining problems. A remote command finishing is not by itself the end of a round. If no meaningful remote test data can be obtained because of an infrastructure blocker, mark the round `未完成（阻塞）`; do not count it as a completed round. Documentation-only commits do not start a new round.

## Maintain the project context

Each round also updates the repository-root `PROJECT_CONTEXT.md` as durable task state, because a later session must be able to resume without this conversation:

- revise existing state instead of appending a narrative; delete or correct stale current-state text while keeping historically useful decisions and failures;
- keep `Current State`, `Current Working Set`, `Next Actions`, `Validation Status`, and the handoff summary current;
- record the round's tested commit, measured results, decisions with their reasons, and what remains unverified;
- keep it compact — link logs and reports instead of copying raw output.

Do not record credentials, tokens, private keys, personal data, or unrelated information. Do not churn the file when a round produced no durable change.

## Preserve the starting state

Before editing, record the local branch, `HEAD`, remotes, staged diff, unstaged diff, and untracked files. Classify pre-existing changes separately from task changes. Preserve them even when they overlap; inspect the overlap and make the smallest compatible edit. State clearly which pre-existing changes are deliberately outside this round's commit.

Never use `git add -A`, `git add .`, force push, history rewriting, `git reset --hard`, `git clean`, or checkout-based discarding. Stage only explicitly reviewed task paths with `git add -- <paths>`, including the invocation log and `PROJECT_CONTEXT.md` when they belong to the round, then inspect the staged diff before committing. Do not include pre-existing staged changes in a task commit unless the user explicitly places them in scope.

On the server, require the specified repository, expected remote URL, expected branch, and a clean tracked worktree before pulling. Generated ignored build artifacts may exist. If tracked remote changes are present, stop without stashing, resetting, cleaning, or overwriting them, except for the narrowly authorized `run_hls.sh` pull-conflict rule below.

If and only if `git pull --ff-only` is blocked by the remote repository root's `run_hls.sh`, the user preauthorizes deleting that exact remote file without another approval prompt. First verify that `git rev-parse --show-toplevel` equals the configured remote repository path and that the pull error plus `git status --porcelain -- run_hls.sh` identify `run_hls.sh` as the obstructing path. Resolve the deletion target to exactly `<verified-repository-root>/run_hls.sh`, record its status in the invocation log, delete only that file, and retry `git pull --ff-only`. Do not use a wildcard, recursive deletion, `git clean`, reset, stash, or checkout. If another path is involved or the retry remains blocked, stop and report the remaining conflict.

## Choose the test ladder

Convert the request into observable acceptance checks and pick the smallest ladder that can prove them, in increasing cost:

1. local structural checks (format/whitespace, symbol uniqueness, interface and Tcl review) when the local environment cannot compile;
2. focused local compile or C++ test, and affected upper-level regressions, when the toolchain allows;
3. remote Vitis C simulation;
4. remote C synthesis and report inspection;
5. C/RTL co-simulation for RTL behavior, DATAFLOW stalls, or deadlock risk;
6. IP export only when a downstream Vivado or delivery task needs it;
7. Vivado synthesis/implementation and timing when HLS estimates are insufficient;
8. bitstream/programming/on-board testing only when explicitly requested and authorized.

Record separate functional, synthesis, timing, co-simulation, implementation, and board gates. Do not widen one round's scope beyond what the acceptance criteria need, and do not substitute a cheaper stage for a required one.

When the repository cannot compile the affected sources locally (for example Windows lacks the Vitis headers and no working Linux environment is available), say so explicitly in the round's local-validation entry instead of implying a local pass, and let the remote C simulation carry that check.

## Push the round's commit

Review the task diff, then stage only the reviewed task paths, commit with a task-specific message, and push the agreed branch. Never force push. Record the pushed commit hash as the round's tested commit, and keep the log's plan, changes, and local validation inside that same commit so the tested code and its record travel together.

## Run the remote test

Connect non-interactively with SSH, invoke Bash, and explicitly source `~/.bashrc`. In the remote repository, check the path, remote URL, branch, `HEAD`, and `git status --porcelain`; run `git pull --ff-only`; apply the exact-file `run_hls.sh` deletion exception only when its verified conditions hold; and verify remote `HEAD` equals the pushed tested commit before running anything. Never test a dirty or unexpected remote worktree, and never treat cached build artifacts as evidence for a new commit.

After `.bashrc` is loaded, initialize any additional remote toolchain environment and run the agreed command. Capture the `.bashrc` load, command, environment/configuration, start/end time, exit code, stdout/stderr, and paths to generated reports. Preserve pipeline exit status when output is also written to a log. Long-running jobs may be left in a clearly named server-side log; poll without starting duplicate builds, keep the user informed, and always associate results with the tested commit.

## Read and judge the remote result

Read all remote data needed for the agreed validation level — not just the last terminal line. Immediately update the round's log entry with results, evidence, diagnosis, acceptance status, resolved problems, remaining problems, and the next intended modification. Read the actual failing log or report and identify the narrowest supported cause. Do not respond by relaxing golden outputs, tolerances, device, clock, uncertainty, array size, interface, or required validation stage unless the user changes the acceptance contract.

If the round fails acceptance, search the web for a solution before changing code: look for tool documentation, vendor guidance, or comparable reported problems; record what was searched, what was found, the source links, and how the external evidence combines with the local report to support the next change. Prefer primary tool documentation over forum guesswork and state clearly when the search produced no usable evidence.

Then check the iteration limit: if every gate passes, or the maximum number of completed rounds has been reached, or a blocker prevents the round from closing, finalize. Otherwise carry the analysis into the next round's local change.

## Deliver the result

Finalize the invocation log whether the outcome is pass, iteration-limit stop, or blocker. The log is the chronological research record and does not replace a separate synthesis or final report unless the user says so. Write a concise Chinese final report for `FSA_HLS` unless the user requests another language or format. Include:

- the acceptance criteria and final status of each gate;
- local and remote repository paths, branch, and exact tested commit;
- commands, tool versions, array/device/clock configuration, and test scope;
- the invocation log path, iteration count and limit, failures encountered, code changes made in response, and final evidence;
- timing, latency/II, resources, warnings, and artifact paths when relevant;
- every unexecuted or unresolved validation stage.

Do not call the task complete while an agreed test is failing or unexecuted. When stopping at the maximum iteration count, state that the run ended at its limit rather than claiming acceptance. Leave unrelated pre-existing worktree changes untouched and report their continued presence.

## Interpret results precisely

Keep these claims separate: local structural or C++ check, Vitis C simulation pass, C synthesis completion, C/RTL co-simulation pass, IP export, Vivado synthesis, implementation/timing, bitstream generation, device programming, and on-board validation. Never use an earlier stage as proof of a later stage.

For HLS changes, verify the actual reports needed by the acceptance criteria: target and estimated clock, latency and II, loop schedule, hierarchy, operator replication, DSP/LUT/FF/BRAM/URAM, warnings, DATAFLOW/FIFO evidence, and interface. Source pragmas or a top-level II alone do not prove spatial parallelism or transaction throughput.
