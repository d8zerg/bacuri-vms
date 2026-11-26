# ADR-007. Shared components and states of work screens

[Русская версия](../ru/adr/007-ui-states.md)

**Status** - Accepted.

**Context.** The same point is visible on the setup screen and at the operator workplace. A successful camera connection, recording, playback and a pending catalog require different actions.

**Options.** Design every screen separately, limit the work to a library of basic elements, or define shared domain components and check them on both screens.

**Decision.** The UI Kit defines semantic tokens, typography, fields, statuses, the player, the timeline and the alarm card. States are derived from the contract, explained in text and keep one meaning on both routes. Design reviews look at whole screens, including the narrow layout and the focus order.

**Rationale.** A button and a status can each be correct while their combination wrongly confirms a recording. The full route shows the order of actions and the link between a confirmation and a decoded frame.

**Implementation.** Native elements form the basis of input. For a complex element, the native variant and a React Aria primitive are compared on keyboard, focus and accessible value in the same scenario. Public states are reproduced in the local Storybook. The view code is used both in the catalog and in the working client. The cost of this decision is maintaining fixtures when the contract changes. [References, timeline example and acceptance criteria](../ui.md#ui-kit-quality-and-hypotheses-for-the-two-routes)

**Archive timeline.** The first component uses a native `input type="range"`. In Chromium and Firefox it keeps focus and supports pointer selection, Home and End. The component sets an arrow step of one second and an accessible value with the UTC time and the recording state. Return to the comparison with React Aria when two independently controlled bounds or an input method unsupported in a target browser appear. [Examples and automatic checks](../ui.md#archive-timeline-in-the-ui-kit)

**Consequences.** A new state variant needs a shared name, an accessible representation and a check in context. Changing the visual design is allowed when the meaning and independent completion of the task are kept.

**Verification and review.** For the "UI and accessibility" requirement, attach full screen captures, introduce a delay and a failure, and check text wrapping, contrast and focus return. Review the component model when the same state is understood differently or a new API state cannot be shown unambiguously. Browser tests run the scenarios. Participants go through them in manual acceptance. [Design review criteria](../ui.md#design-review-of-whole-screens) · [Requirements](../quality.md#acceptance-requirements)
