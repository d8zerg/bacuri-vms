# Scope and quality criteria of Bacuri VMS

[Русская версия](ru/quality.md)

## First delivery scenarios

The VMS release covers the compact and the extended configurations of an autonomous site. Distributed installations are qualified separately with Bacuri Manager. The basic routes below form the mandatory regression. The register sets the criteria by configuration, camera capabilities and user tasks. After the automatic check of the signed kit, participants perform the tasks on the same build. [Requirement register](release-register.md#functions) · [Scenario matrix](test-profile.md#scenario-matrix)

| Scenario and initial conditions | Action and acceptance criterion | Automatic check |
| --- | --- | --- |
| Administrator, a clean node without internet | Check the signature, install the kit, create an administrator, assign the access scope. After an upgrade and a recovery, the configuration, the permissions and the available frames are kept | Installation, a failed upgrade, rollback and recovery on a clean node with checks of the version, the old and the new frame, RPO/RTO. |
| Administrator, an available camera and a new point | Discover the camera, choose the profiles and confirm the recording by a played frame. RTSP entry by address is supported. When the device is replaced, the previous archive stays with the original point | Comparison of the applied revision and the frame, wrong credentials, a repeated discovery, a break and recovery. A matrix of physical models and firmware. |
| Operator, a site plan and an assigned point | Open live video from the plan and move a supported PTZ camera. The interface shows the result or the reason for a refusal | Checks of the point binding, the camera confirmation, an unavailable function and a forbidden command. |
| Operator on duty, a camera event with a known point and time | Take the alarm, open the corresponding episode, return to the card and close it. With a gap or uncertain time the reason is visible | Repeated delivery, a competing action and a refusal by role. Identifiers, history, point, frame and focus are compared. |
| Operator with the export permission, a known interval | Export a permitted fragment and verify it on a separate disconnected node. A modified file is rejected | Compare the frames, the point and the time, check the integrity, the refusal outside the permissions and the audit. |
| Administrator, a limited volume with data of different age | Set the retention period, see the remaining capacity, wait for the cleanup. The available intervals play after an interruption and a repeated cleanup | A controlled clock, a filled volume, old and fresh blocks. Compare the volume and the catalog, interrupt viewing and repeat the recovery. |

The automatic scenarios run on one signed build through the regular sign-in. In manual acceptance the same facts serve as references for checking the results of the users' actions. [Stand profile](test-profile.md#scenario-matrix) · [Final tasks](ui.md#first-use-check)

## Limits of the first delivery

The table describes the initial limits of the compact profile of an autonomous site. In the full release, fleet management, new recording modes, a protected archive, investigations, site recovery and built-in help in Russian and English are also mandatory. Each capability is checked under the conditions of the corresponding acceptance profile.

| Limit | Impact and reliability of the check | When to review |
| --- | --- | --- |
| One Linux node, a local volume, PostgreSQL and SQLite | Stopping processes, filling and returning the volume and recovery from a local copy are checked. The loss of the only copy limits the available archive | For requirements to survive the loss of a node or a medium with a guaranteed archive |
| Continuous H.264 recording, the set of models and firmware from the profile | The simulator reproduces failures and reference frames. Compatibility, hardware events and PTZ are checked on physical devices with the declared functions | When a model, firmware, codec, sound, recording mode or a device with another set of functions is added |
| Local accounts and event delivery through PostgreSQL | Permissions, actions under a limited delegation, the audit, repeated delivery and revocation on one site are checked | With corporate sign-in, distributed consumers or load above the measured one |
| Local network, Chrome and Firefox on Linux | The result applies to the tested browsers, the direct media route and the site profile | With a mandatory mobile client, an external network with relaying or a browser change |
| Receive time when the camera clock is not bound | The interface shows the uncertainty. An unambiguous frame is confirmed only for a known source revision and interval | With requirements for more precise correlation between cameras. [ADR-004](adr/004-time.md) |
| A controlled motion rule and a limited set of camera events | The delivery of a hardware event up to the alarm and the frame is checked, the detector quality needs separately labeled video | When detectors and analytics rules are added |
| Reproducible interface states and a finite set of participants | Storybook lets the states and the layout be checked. Qualification tests the working client with the site services. The first-use conclusion applies to the participants of manual acceptance | When a key screen, a role or the load profile changes, or when an advantage over another product is claimed |

## Acceptance requirements

All rows are mandatory for the acceptance of the first release. Before manual acceptance the automatic tests run on the build being accepted. The reviewer assesses the architecture, the readability of the code and the composition of the screens by the contracts, the changes and the run materials. A significant defect gets a regression test. After the automatic qualification, participants perform the tasks on site.

| Requirement | Acceptance criterion | Verification method and material |
| --- | --- | --- |
| Current stack | For the changed product, build and test dependencies the exact version, the fix source and the compatibility are known. In the release candidate each delivered dependency is matched with the SBOM. An unmaintained component, an expired exception and an applicable critical advisory without a verified protective measure block the release | On a change, compare the graphs and run the affected automatic scenarios. In the core regression, check the content and the support report, on the release candidate repeat the comparison for the signed kit and check the upgrade from the previous supported delivery. [ADR-015](adr/015-technology-lifecycle.md) · [ADR-016](adr/016-system-backports.md) |
| Architecture | For each transition the owner of the fact, the input, the acknowledgement, the limit and the visible result of a failure are known. Recording continues when a viewer slows down with a healthy source and volume | In the architecture review, follow the changed scenario across the process boundaries, discuss a simpler variant and the cost of the choice. Attach the diagram, the ADR, the result of a regular call and a test with a failure. [Boundaries and failures](architecture.md#site-scenario-contracts) |
| Code | At the real call site the action, the types and the result can be read. Invariants hold on an error, a repeat and a stop. The response and the side effects match the contract | Technical review by the rules below, compile-time type checks, domain, contract and failure tests, the GCC and Clang matrix report. [Technical code review](#technical-code-review) |
| Data and access | The applied revision is distinguishable from the desired one, each verified completed block has one catalog record, a repeated event keeps one alarm. Viewing is limited to the point, the interval and the permit term | Lose the response after the commit, repeat the request, compare the rows and the frame. Check a forbidden point and measure the end of active delivery after revocation. [Stand profile](test-profile.md#scenario-matrix) |
| UI and accessibility | On whole screens the source, the time, the state and the next action are distinguishable. On narrow and wide screens accessible names, keyboard control and focus visibility are kept | Automatic browser tests compare captures, contrast, structure, focus and states. In manual acceptance participants go through the available routes, including with a screen reader. [Screen criteria](ui.md#design-review-of-whole-screens) |
| UX | In acceptance a new user completes the task of the role within the fixed time without hints, explains the result correctly, and there are no critical errors | Automation checks the route and the data. In manual acceptance the individual time, help, explanation and comparison with the reference are recorded. The administrator checks the kit by the operating guide. [Protocol](ui.md#first-use-check) |
| Operation | Slow viewing adds no recording gaps. After a failure the data loss and the time to resume recording, reading and configuration application are known | Repeat the recovery from the guide in a clean environment with the saved copies, compare checksums and permissions, open an old and a new frame. For each failure attach diagnostics by point, revision and interval without secrets and video in technical logs. [Measurement protocol](test-profile.md#measurement-protocol) |

## Release candidate security

The checks below are mandatory for a compact node. On an extended autonomous site also check audio delivery, masking and archive copies. Keep the evidence for each VMS profile. For the web client and the API, the target verification level is OWASP ASVS Level 2. For administrator sign-in, the reference is the NIST SP 800-63B-4 requirements for AAL2, including two different factors and a phishing-resistant sign-in method. Conformance is assessed by the results of a specific build. [OWASP ASVS](https://asvs.owasp.org/) · [NIST SP 800-63B-4](https://pages.nist.gov/800-63-4/sp800-63b.html)

| Area | Condition of automatic acceptance | Automatic check and material |
| --- | --- | --- |
| Administrator | On an isolated site, administrator rights are available only after MFA. WebAuthn with user verification is provided. Emergency recovery leaves an audit record | Check the first sign-in, the refusal with a password only, sign-in with two factors, the loss and replacement of a key, revocation of the key and the previous sessions. Repeat after the node is restored. [Access contract](adr/001-api-contracts.md#local-session-and-access-rules) |
| Camera channel | By default, credentials, commands and video use channels with camera authentication and encryption. Open RTSP is allowed only by a separate policy for a specific device in an isolated segment | Check trusted and untrusted certificates, a name mismatch, the absence of an open connection after a TLS error and the scope of the exception. Keep the results by model and firmware. [Camera matrix](rtsp-compatibility.md#hardware-matrix-for-the-release) |
| Web client, API and delivery | For each ASVS Level 1 and 2 requirement the applicability, the reason and the verifiable evidence are given. The signature and the content of the offline kit are checked on the build being accepted | Compare the matrix with the full list of identifiers of the standard version. For applicable rows check the test result or the design analysis material, for excluded ones the reason. Compare the SBOM with the kit content, reject a substituted package, check recovery after a failed upgrade. [Offline delivery](adr/011-offline-delivery.md) |

The applicability matrix covers 253 OWASP ASVS Level 1 and 2 identifiers. Each row gives the applicability, the reason and the source material for the check. The `pending` status requires evidence on the release candidate, `scope-review` requires comparing the reason of the exclusion with the actual delivery. `task camera:check` compares the full set of identifiers and levels with the fingerprint of the official CSV and checks the links to the materials. [Applicability matrix](asvs.csv) · [OWASP ASVS source list](https://github.com/OWASP/ASVS/blob/v5.0.0/5.0/docs_en/OWASP_Application_Security_Verification_Standard_5.0.0_en.csv)

The report binds the results to the hash of the signed kit. A missing result, an unjustified exclusion or a failed mandatory check blocks manual acceptance.

## Automatic code checks

For each change `task check:report` runs the mandatory checks on Linux in Docker Compose. A non-zero exit code blocks acceptance. The report directory keeps the common log and the check results.

| Check | Acceptance criterion | Evidence |
| --- | --- | --- |
| C++ coverage | CTest and the integration scenarios run at least 75% of the executable lines in `src/`. The services and the product headers count | gcovr report by file and line in `coverage/cpp/` |
| TypeScript coverage | All executable TS and TSX from `web/client/src` count in the denominator. V8 data of the component routes and of the end-to-end HTTPS scenarios are combined by source maps. At least 75% of lines | `workspace/coverage/index.html` contains the source files and the uncovered lines. A separate report `workspace/ui-coverage/index.html` covers views and scenarios on test data. |
| ASan, UBSan, TSan | CTest passes under ASan/UBSan and separately under TSan. The integration checks of control, intake, recording, playback, live video, alarms and recovery pass under ASan/UBSan | Test log with the preset names |
| Recovery | `task recovery:check` confirms one catalog record per healthy block, the read ban until reconciliation and for a foreign point, the recovery of configuration and permissions, reading old and new video. Losses and time are measured for each of the three operations | JSON and JUnit XML in `recovery/`. [Failure matrix](test-profile.md#failure-and-recovery-tests) |
| Delivery maintenance | `task maintenance:check PREVIOUS=/path/to/report` checks a signed upgrade, a repeated migration, a rollback, the revocation of an administrator key and recovery after the loss of the system disk | `operations.json` and `diagnostics.json` in the run directory. [Maintenance procedure](maintenance.md) |
| Static analysis and format | `task lint` uses the [.clang-tidy](../.clang-tidy) configuration shared with the IDE with the `clang-analyzer-*` and `bugprone-*` groups, and findings are errors. `task format:check` runs clang-format on the sources and headers in `src/` and `tests/` | Analysis log and the check against the [project style](../.clang-format) |
| Secrets | Gitleaks scans the full available history of all Git references, the index and the current content of tracked and new non-ignored files. A shallow clone requires fetching the history | Separate reports in `secrets/`. Found values are masked |

In the TSan run the `RecordingPolicy.ArchiveDecodesNumberedSamplesAcrossTheRepeatedHour` scenario passes real buffers between the recorder and the GStreamer, GLib and FFmpeg system libraries. For this scenario `ignore_noninstrumented_modules=1` limits the interception of calls from distribution libraries built without TSan. The recorder, the queue and the test code are instrumented. The other CTest scenarios use the standard TSan mode.

## Technical code review

### Web client and UI Kit

`task ui-kit:check` runs the strict TypeScript check, ESLint with typed rules and import restrictions, Prettier, the Vite build and Storybook. In Chromium and Firefox it checks states, focus, keyboard input, accessibility, responsiveness and whole screens. `task workspace:check` repeats these checks and adds real scenarios over HTTPS, WSS, WebRTC and MSE. The common gate calls it through `task coverage:check`. The results and budgets are described in the [stand profile](test-profile.md#single-web-client-acceptance).

The reviewer checks the call site of the component, the owner of the state and the release of resources. A point change needs a test with responses in reverse order, a media player needs repeated opening and closing of the route, permissions need revocation during viewing. The report must show the user action and the verified result. [Client boundaries](adr/014-web-client.md)

### C++ and common rules

For a change the reviewer takes a regular call and one significant failure from the affected scenario. The review result names the call site, the expected observable behavior, the check and the findings that require a fix.

- **Readability.** Follow the main path from the input to the result. Names express the domain action, the signature reveals the role of the arguments and the possible error. For a new layer or template parameter, show the real place of use and the complexity it removes. If a simpler implementation gives the same guarantees, simplify the code or record the reason for the choice.
- **Correctness.** Name the owner of the resource and the state, check the bounds of time and identity, the lifetime and the invariant. For the identifier example, passing `SiteId` instead of `DeviceId` is rejected at compile time. For recording, check exactly one registration after a lost acknowledgement. [Type check example](../tests/domain/model.cpp)
- **Failures and shutdown.** Follow the timeout, the cancellation, the release of the resource and the repeat after a partial success. For a concurrent path, check the queue limit and stopping during active work. For a changed schema, use existing data and check the read after the migration.
- **Contract conformance.** Compare the response, the side effects, the permission check and the visible client state with the schema and an error example. Check through the regular entry point. Keep the review result next to the report of the automatic checks of the affected revision. A finding that changes a result, an access right or the reliability of a status requires a fix and a repeated check before acceptance.
