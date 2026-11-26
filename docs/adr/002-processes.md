# ADR-002. Process boundaries of a compact site

[Русская версия](../ru/adr/002-processes.md)

**Status** - Accepted.

**Context.** One camera serves recording and viewing. A viewer may stop reading, and the control core may restart during recording.

**Options.** All operations in one process, a separate service for each application function, or a modular core with separate recording and delivery processes.

**Decision.** The core owns permissions, configuration, the catalog and alarms in PostgreSQL. The recording process owns the RTSP sessions, the last applied configuration and the local volume. The delivery process owns the viewer connections and the delivery limits. gRPC sets the control boundaries, and media consumers have separate bounded queues.

**Rationale.** Separation by failure mode lets delivery stop while recording continues. The core can commit an observation, an alarm and the feed publication atomically.

**Consequences.** Revision delivery, lag diagnostics and reconnection after a restart are needed. The availability of all three processes depends on one host.

**Verification and review.** For the "Architecture" and "Operation" requirements, stop the core, slow down a viewer and restart delivery, then compare the blocks with the control recording and check the queue depths. Review when there are additional breaks with a healthy source and volume, when the core budget is exhausted, or when roles are distributed over nodes. [Scenario contracts](../architecture.md#site-scenario-contracts) · [Requirements](../quality.md#acceptance-requirements)
