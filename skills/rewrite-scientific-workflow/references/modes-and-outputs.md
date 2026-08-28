# Modes and Output Contracts

Read the section matching the user's request. Adapt the structure to available evidence; do not manufacture empty sections.

## Project Design

Use when the user provides an idea, phenomenon, method, or broad topic.

Output:

1. Central scientific question
2. Why it matters and provisional literature gap
3. Testable hypothesis and main competing explanations
4. Minimal experiment sequence, each tied to a question
5. Expected decision branches for positive, negative, and anomalous outcomes
6. Evidence required for the central claim
7. Likely boundary conditions and reviewer risks
8. Minimum sufficient story

Do not assume the user's preferred method is itself the research contribution.

## Finding Expansion

This is the default daily mode when the user provides a result.

Output:

1. Normalized finding, separated from raw data
2. Nearest 1-hop implication
3. Provisional literature position: Confirm, Contradict, Refine, Extend, or Reframe
4. Focused new questions across relevant question dimensions
5. Answerability classification for each question
6. Experiments or analyses for questions answerable now
7. Questions outside current scope and the precise reason
8. Integrated 2-hop interpretation
9. Tentative general principle
10. Genuine limitations
11. Future studies linked one-to-one to unresolved questions

If the result is unexpected, run the anomaly branch before building the narrative.

## Results Audit

Inspect each subsection for:

- an explicit scientific question;
- an experiment or analysis capable of answering it;
- data distinguished from the finding;
- a claim no stronger than the evidence;
- a suitable 1-hop implication;
- an answer-oriented heading rather than a method-oriented heading;
- answerable questions incorrectly deferred to Discussion;
- missing controls or untested alternative explanations;
- anomalies that have been ignored or rationalized away.

Return a concise subsection-by-subsection table or list with: current role, problem, proposed scientific-question framing, supported answer, and concrete revision or experiment.

## Discussion Audit

Check whether:

1. the opening integrates multiple 1-hop implications into a 2-hop interpretation rather than repeating Results;
2. the middle abstracts cautiously toward a possible general principle;
3. the principle opens meaningful mechanism and boundary questions;
4. literature positioning states how knowledge is confirmed, contradicted, refined, extended, or reframed;
5. limitations correspond to important unresolved questions;
6. each future study answers one of those questions;
7. speculative language is calibrated to evidence.

Return proposed paragraph roles and, when useful, revised topic sentences. Keep study-level interpretation separate from field-level claims.

## Next-Experiment Selection

For each candidate experiment, state:

- question answered;
- hypothesis or competing explanations;
- minimum viable design and required controls;
- decisive readout;
- how positive, negative, or mixed outcomes change the central claim;
- estimated cost or dependency when known;
- priority and reason.

Rank by:

1. central-claim risk;
2. fatal reviewer challenge;
3. ability to distinguish competing explanations;
4. important boundary condition;
5. information gain relative to cost.

Separate “must do,” “high value,” and “defer.”

## Deadline Mode

Output:

- **Central claim:** retain, narrow, or reconsider, with reason
- **Must do:** work required for validity or the central claim
- **Should do:** high-value work if resources allow
- **Can omit:** work that does not materially affect the evidence chain
- **Limitation:** unresolved questions caused by current scope
- **Future study:** concrete tests for those questions
- **Stop decision:** whether the minimum sufficient story is reached and which criterion remains unmet if not

Do not classify a fatal flaw as a limitation or use deadline pressure to overstate evidence.
