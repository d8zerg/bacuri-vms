# ADR-005. Media profiles for recording and viewing

[Русская версия](../ru/adr/005-media.md)

**Status** - Accepted.

**Context.** A compact node must record a camera continuously and at the same time deliver live video and an archive fragment to the browser.

**Options.** Transcode one input for every viewer, use the main profile for all tasks, or accept two H.264 profiles and separate the archive and live paths.

**Decision.** The main RTSP stream is stored in fMP4 in its original encoding. The secondary one is delivered through WebRTC. For the archive the browser gets fMP4 parts over HTTPS and decodes them through Media Source Extensions.

**Rationale.** Separate profiles limit the cost of viewing and keep the archive quality. The start of decoding and its relation to the keyframe can be checked against a reference.

**Consequences.** Two supported camera profiles, a compatible H.264 decoder and an available WebRTC network path are needed. The keyframe interval affects latency and seeking.

**Verification and review.** For the "Architecture" and "Data and access" requirements, play live video and the archive at the same time in Chrome and Firefox, compare the frame and the position with the reference, and check a delivery restart during recording. Review when H.265, audio or an external network route is required, or when the latency or resource budget is exceeded. [Compatibility](../rtsp-compatibility.md) · [Requirements](../quality.md#acceptance-requirements)
