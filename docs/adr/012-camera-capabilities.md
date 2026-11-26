# ADR-012. Connecting and controlling a camera by its capabilities

[Русская версия](../ru/adr/012-camera-capabilities.md)

**Status** - Accepted.

**Context.** The administrator needs to find a device and confirm the recording. The operator needs the commands and events available for the installed model and firmware. A video stream address alone does not give this information.

**Options.** Configure a device by two RTSP addresses, maintain a separate wizard for each manufacturer, or use a common ONVIF adapter that takes the device capabilities into account, with RTSP entry by address.

**Decision.** The wizard discovers a device in a permitted segment, obtains its capabilities and profiles, binds the camera to a point and checks the recording by a decoded frame. RTSP entry is available for a known address. For PTZ functions and events, the adapter uses the declared capability and the test result of the corresponding firmware. Replacing a device creates a new binding of the same point and keeps the previous archive.

**Camera transport policy.** After discovery, the wizard checks the certificate and the name of the device before sending credentials. For the protected profile, ONVIF commands go over HTTPS and video over a verified encrypted media channel, for example RTSPS with RTP carried inside TLS. The same policy applies to manual address entry and to reconnection. A trust error stops the connection and is visible to the administrator. Automatic fallback to an open channel is forbidden.

For compatibility with an old camera, the administrator can explicitly allow open RTSP for its address in a dedicated segment. The exception applies only in the given scope, and the change is recorded in the audit. This keeps recording from an outdated model possible at the cost of open video transmission within the segment. The exception is reviewed when the camera is replaced or a supported protected mode appears. [Security criteria](../quality.md#release-candidate-security)

**Rationale.** A common model keeps one input route and lets unavailable actions be hidden. The reference for H.264, motion events and PTZ is the corresponding Profile T functions. The site protocol confirms the actual compatibility. Official profile conformance is checked by product and version in the ONVIF registry. [Profile T](https://www.onvif.org/profiles/profile-t/) · [ONVIF registry](https://www.onvif.org/conformant-products/)

**Consequences.** Each declared function needs a row of hardware tests. With entry by address only, recording availability is established separately from command and event support. A firmware change requires a repeated pass.

**Verification and review.** Automatically check discovery, wrong credentials, a repeat, a replacement, playback, the actual PTZ result and a motion event up to the correct archive frame. Review the decision when the media profile grows or a mandatory function appears that the common adapter does not provide. [Requirements](../quality.md#first-delivery-scenarios) · [Camera matrix](../rtsp-compatibility.md)
