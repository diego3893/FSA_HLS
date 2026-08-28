---
name: rewrite-scientific-workflow
description: Apply the REWRITE framework to design or restructure a scientific study, expand a finding into testable questions, select next experiments, audit Results or Discussion, and converge on a defensible minimum story under deadlines. Use for research reasoning and paper architecture, not for grammar-only polishing, translation, citation formatting, or unsupported literature claims.
---

# REWRITE Scientific Workflow

Use writing as a research instrument: make the question, evidence, interpretation, uncertainty, boundary conditions, and next decision explicit. The goal is a coherent and defensible scientific story, not the largest possible experiment list.

## Route the Request

Infer the most useful mode from the user's material; do not force them to name one.

- **Project design:** turn an idea or phenomenon into scientific questions, hypotheses, discriminating experiments, possible findings, and decision branches.
- **Finding expansion:** turn one result into implications, literature positioning, new questions, answerability decisions, and next experiments.
- **Results audit:** check whether subsections answer scientific questions and distinguish data, findings, and 1-hop implications.
- **Discussion audit:** check the progression from findings to integrated interpretation, general principle, new question space, limitations, and future studies.
- **Next-experiment selection:** rank experiments by their effect on the central claim and information gained.
- **Deadline mode:** define the minimum sufficient story and separate must-do work from deferrable questions.

If the request spans modes, combine only the sections that help the decision at hand. For the detailed outputs of each mode, read [references/modes-and-outputs.md](references/modes-and-outputs.md).

## Establish the Evidence Boundary

Before reasoning, separate:

1. what the user directly observed or measured;
2. what the data directly support as a finding;
3. interpretations or mechanisms that remain hypotheses;
4. missing information that affects the conclusion.

Do not invent experimental results, controls, citations, novelty, or consensus. When literature positioning is requested but no literature is supplied or searched, state what must be checked and present the positioning as provisional. Preserve uncertainty and identify alternative explanations.

## Run the REWRITE Core

Use this chain as a decision process rather than a decorative acronym:

> Research Question → Examine Literature → Work / Experiment → Read Finding → Interrogate Finding → Test Answerability → Extend or Exit

For the reasoning model, anomaly branch, two loops, and placement rules, read [references/core-framework.md](references/core-framework.md).

### 1. Start from the scientific question

State the central question before methods. Expand it only along relevant dimensions: whether, what, how, why, when, where, and to what extent. Convert promising questions into explicit hypotheses and experiments able to distinguish them from competing explanations.

### 2. Tie every experiment to a question

Represent each evidence unit as:

> Question → Experiment / Analysis → Data → Finding → 1-hop Implication

Prefer Results headings that state the learned answer rather than the technique used. For example, replace “Ablation Study” with a supported answer such as “Module X accounts for most of the performance gain.”

### 3. Interrogate each important finding

Generate only questions that clarify mechanism, causality, robustness, scope, failure, competing explanations, or effect size. Avoid generic question lists detached from the actual finding.

For unexpected results, first distinguish technical error, random noise, and a stable anomaly. A reproducible anomaly may require rewriting the original question instead of forcing it into the initial hypothesis.

### 4. Test answerability now

For every important new question, decide:

- **Answerable now:** use existing data or a feasible added experiment; return it to Results.
- **Not answerable now:** explain why; connect it to a genuine limitation and a future study designed to answer it.
- **Threatens validity:** treat it as a central-claim risk or fatal flaw, not as an ordinary limitation; redesign the study or narrow the claim.

This classification is mandatory when recommending next work.

### 5. Build Discussion by controlled abstraction

Move in explicit stages:

> Multiple 1-hop implications → integrated 2-hop interpretation → possible general principle → new question space

Keep the 2-hop interpretation close to this study. Label a general principle as tentative unless evidence across relevant settings supports it. Then expand outward to mechanism, boundary conditions, strength, generalization, and counterexamples.

### 6. Stop when the story is sufficient

Prioritize questions that could change the central claim, expose a likely fatal reviewer challenge, distinguish competing explanations, establish important boundary conditions, or add high information at low cost. Defer questions that require a substantially new study and do not alter the current evidence chain.

Use the stop rule in [references/core-framework.md](references/core-framework.md) for deadline decisions.

## Produce Decision-Ready Output

Lead with the central scientific judgment. Then show the evidence chain and recommended actions. Distinguish clearly among:

- **Supported by current evidence**
- **Plausible but untested**
- **Answerable with current data**
- **Requires new experiment**
- **Outside the current study**
- **Central-claim risk**

For experiment recommendations, include the question, hypothesis or competing explanations, minimum design, decisive outcome, and how each outcome changes the paper. Rank recommendations instead of returning an unbounded wishlist.

When editing or auditing supplied prose, quote or identify the relevant passage, diagnose its role, and propose a concrete replacement or relocation. Do not rewrite technical claims more strongly than the evidence permits.

For guided fill-in structures and the research-depth diagnostic, read [references/templates-and-diagnostics.md](references/templates-and-diagnostics.md).
