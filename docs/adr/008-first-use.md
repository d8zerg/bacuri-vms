# ADR-008. Acceptance by independent first completion of a task

[Русская версия](../ru/adr/008-first-use.md)

**Status** - Accepted.

**Context.** An expert can demonstrate a familiar route even when a new administrator takes live video for a recording and an operator on duty opens a neighboring episode.

**Options.** Run a manual check at every stage, rely only on browser tests, or automate the intermediate stages and check independent work once on the release candidate.

**Decision.** Automation runs every scenario before manual acceptance. The test profile records the goal, the conditions, the correct result, the critical errors and the time limit of each task. Manual acceptance runs on the same kit after the automatic qualification. New administrators and operators work without documentation and hints. An administrator checks maintenance by the operating guide.

**Rationale.** Time without a correct result would reward false confirmation. Observation, explanation and comparison with the stand data check both the interface and the understanding. A new interaction method can be tried on the same tasks.

**Consequences.** Technical reproducibility and data correctness are known before the final stage. Evidence of independent completion appears in manual acceptance. A defect requires a regression test and a repeated qualification of the fixed kit. A repeated first-use assessment needs new participants.

**Verification and review.** For the "UX" requirement, keep the individual times, requests for help, critical errors and the result on the working client. Review the tasks and limits when the role, the initial conditions or the work process change, keeping the previous protocol. [Tasks and thresholds](../ui.md#first-use-check) · [Requirements](../quality.md#acceptance-requirements)
