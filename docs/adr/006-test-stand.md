# ADR-006. Video sources for reproducible acceptance

[Русская версия](../ru/adr/006-test-stand.md)

**Status** - Accepted.

**Context.** Checking time, repeats and breaks needs a known input. Confirming camera compatibility requires the camera's own protocol implementation.

**Options.** Test only on a real camera, use only an emulator, or run complementary passes on one Linux Docker Compose stand.

**Decision.** A controlled source feeds two profiles with reference frames, gaps and a clock shift. Automatic scenarios for discovery, stream selection, PTZ and hardware events run on the simulator and on physical devices according to their declared functions. For each required model and firmware, the result of its own server, the settings, the confirmation of the function and a decoded frame are kept. An unavailable required model blocks the qualification of the kit.

**Rationale.** The first pass lets a failure be repeated and the frame accuracy be checked. The second reveals the specifics of profile negotiation, RTSP and time of a particular device.

**Consequences.** The final acceptance requires hardware. A webcam behind an RTSP emulator gives an additional result for the capture path. A statement of IP camera support applies to the tested device and its firmware.

**Verification and review.** For the "Data and access" and "Operation" requirements, attach the commands and references of the synthetic pass and a separate row for the physical camera with the results of both routes. Review when the device matrix grows or when the stand does not reproduce a failure observed in operation. [Profile](../test-profile.md#environment) · [Requirements](../quality.md#acceptance-requirements)
