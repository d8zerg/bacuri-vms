# ADR-004. Binding observation time to the archive

[Русская версия](../ru/adr/004-time.md)

**Status** - Accepted.

**Context.** The source clock can drift, and the camera at an observation point can be replaced. The operator needs the frame of the source that was active at the time of the alarm.

**Options.** Search for the nearest frame by receive time, trust the original camera time, or store the original timestamp, the receive time and the quality of their binding.

**Decision.** Store the binding revision of the point, the original timestamp when present, the receive UTC and the normalization basis. For bound clocks store the epoch and the uncertainty. Allow navigation by intervals that take this uncertainty and the source revision into account. Show pending, gap, unknown coverage or uncertain time according to the contract data.

**Rationale.** The same time on the timeline can belong to different sources. The stored basis lets the search be repeated and explains why an unambiguous link is unavailable.

**Consequences.** The manifest and the observation metadata grow. With a large uncertainty the operator sees the precision limit, and automatic navigation is available only for an unambiguous binding.

**Verification and review.** For the "Data and access" and "UX" requirements, feed a clock jump, a binding change and an event at a block boundary, and automatically compare the revision, the frame and the displayed uncertainty. Understanding by participants is compared with the reference in manual acceptance. Review for new time sources or when the measured precision hinders alarm analysis. [Time model](../domain-model.md#time-and-intervals) · [Requirements](../quality.md#acceptance-requirements)
