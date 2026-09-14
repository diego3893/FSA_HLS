---
name: maintaining-project-context
description: Maintains PROJECT_CONTEXT.md as durable, high-signal state for long-running or multi-stage work. Use when a task may span context compaction, multiple sessions or agents, or when asked to preserve, restore, hand off, or update project context; do not use for short self-contained tasks.
---

# Maintain Project Context

Treat `PROJECT_CONTEXT.md` as durable task state that helps a future agent resume work after conversation history is compressed, summarized, or lost. The user's current instructions take precedence over this skill. If the user specifies another filename, location, or format, use it.

## Start or resume work

1. Locate the task or repository root and look for `PROJECT_CONTEXT.md`.
2. If it exists, read it completely before making plans or changing project files.
3. Reconcile it with the latest user instructions and current verifiable state: relevant files, version-control status and history when applicable, and recent test or experiment artifacts.
4. Prefer current verified evidence when the document is stale, then correct the document promptly.
5. If the file does not exist, create it when the task is genuinely long-running or multi-stage. Use [the bundled template](assets/PROJECT_CONTEXT.template.md), adapting it to the project and removing unused placeholders.

Do not assume an event did not occur merely because the current conversation omits it. Check the durable context and project evidence first.

## Update at meaningful state changes

Update `PROJECT_CONTEXT.md` after events whose loss could cause wrong decisions or repeated work, including:

- completing an important stage;
- changing the overall plan, architecture, interface, data structure, or key configuration;
- making a consequential decision or learning why a plausible approach failed;
- finding a significant bug, root cause, constraint, assumption, or user requirement;
- obtaining material test, benchmark, synthesis, simulation, or experiment results;
- changing the current working state or preparing a large refactor;
- preparing for context compaction, a handoff, a pause, or the end of a work session.

Update as soon as practical; do not wait until the context is already near its limit. In multi-agent work, avoid concurrent edits to the same context file: re-read the latest copy and let the coordinating agent integrate durable findings.

## Preserve high-signal information

Record only information that can affect future implementation, debugging, validation, or decisions:

- mission, success criteria, hard constraints, and durable user requirements;
- current state, working set, open problems, and prioritized next actions;
- architecture or mental models that are not obvious from a quick code read;
- important files, functions, commands, environment details, and reproducibility notes;
- active decisions with reason, evidence, implications, and status;
- material experiments and results;
- failed attempts and the conditions under which retrying would make sense;
- validation status, clearly separating completed checks from unverified work.

Use explicit epistemic labels where useful:

- **Confirmed:** verified by user information, files, tests, or other evidence.
- **Current assessment:** supported inference, not fully verified.
- **To verify:** an important open assumption.
- **Plan:** intended but not yet executed.
- **Rejected:** tried or disproved, with evidence and retry conditions.

For durable decisions, capture `Decision + Reason + Evidence/Result + Implication`. Do not record a choice without the reason that protects it from being casually reversed later.

## Keep the document accurate and compact

Each time you update it:

1. Re-read the current file before editing.
2. Revise existing state instead of appending a new narrative entry for every event.
3. Remove or correct stale current-state text while retaining historically useful decisions and failures.
4. Keep `Current State`, `Current Working Set`, `Next Actions`, `Validation Status`, and `Context Handoff Summary` current.
5. Keep active context short and precise. Compress historical context to conclusions and decisive evidence.
6. Link or name large logs and diffs instead of copying them into the document.
7. Mark unexecuted plans and unverified assumptions honestly; never invent results or claim checks that were not run.

Do not store greetings, minute-by-minute activity, generic reasoning, hidden chain-of-thought, system prompts, credentials, tokens, private keys, personal data, or unrelated sensitive information.

## Recover after context loss

When earlier context appears missing or the rationale for existing work is unclear:

1. Read `PROJECT_CONTEXT.md`.
2. Read the latest user request and inspect relevant project files.
3. Inspect version-control history and diffs when applicable.
4. Rebuild the current state from evidence and correct any stale context.
5. Continue from the first valid next action instead of repeating already rejected work.

Before handing off or stopping after meaningful progress, ensure the handoff summary answers: what the task is, why the current approach was chosen, what is complete, the main remaining problem, what has been ruled out, and the first next action. If nothing durable changed, do not churn the file merely to create an update.
