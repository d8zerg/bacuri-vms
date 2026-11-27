# Bacuri VMS stand profile

[Русская версия](ru/test-profile.md)

**Status** - Accepted. [Acceptance conditions](release-profile.md)

## Environment

| Parameter | Profile of the first run |
| --- | --- |
| Node | One Linux x86-64, Intel Core i7-13700H, 20 logical CPUs, 15 GiB of RAM available to the system. The distribution in the containers is Debian Trixie |
| Storage | ext4 on NVMe on the reference node. The persistent PostgreSQL and archive volumes are separate. Archive tests use a private 2 GiB tmpfs for temporary files so the host disk usage does not determine the recorder’s free-space reserve. A separate 64 MiB tmpfs is allocated for a reproducible fill check |
| Network | The isolated network of the Docker Compose project. On the host the test camera is reachable through loopback. For a run with a physical camera, the network segment, RTT and throughput are recorded |
| Browsers | Google Chrome and Firefox on Linux. The exact versions and H.264 compatibility are recorded in the browser check protocol |
| Synthetic camera | RTSP/TCP, H.264, main profile 1280×720, 25 fps, target bitrate 2 Mbit/s. Secondary 640×360, 15 fps, target bitrate 0.5 Mbit/s. A keyframe is requested at least once per second, and the pipeline timestamp is visible in the frame |
| Webcam of the work node | Chicony Integrated Camera through V4L2 and a test RTSP server. Two H.264 profiles are encoded from one capture. [Measurements and compatibility matrix](rtsp-compatibility.md) |
| Physical IP camera | Checking the device's own RTSP implementation needs a camera with two H.264 profiles. The model, the firmware, the resolutions, the actual bitrates and the GOP are entered when it is connected. Credentials are kept outside the profile |

The synthetic camera is used for a reproducible check of two streams. The physical run confirms the compatibility of a specific device. The profile of a compact hardware node is checked separately by the actual resource consumption.

## Offline installation and access

`task install:check SUITE=bookworm` and `task install:check SUITE=trixie` build a native kit with a test key and install it on a clean Debian Bookworm or Trixie amd64 in QEMU/KVM. After the initial setup, the versions of the actually running PostgreSQL server and client are checked. `task media:check:bookworm` builds the native services on Debian Bookworm and runs the RTSP recording, WebRTC delivery in browsers and alarm handling scenarios on the system GStreamer. The installation media is read-only, the archive is placed on a separate ext4 disk. QEMU allows an incoming HTTPS connection from the browser stand. The external network is disabled. After the installation the virtual machine reboots. The check compares the boot identifiers, the systemd state, the versions of the installed packages and the SHA-256 of the web resources with the signed delivery content.

| Action | Expected result |
| --- | --- |
| Modify a Debian package on a copy of the media | The signature and hash check stops the installation before the dpkg database changes |
| Enter the administrator password before enrollment | A one-time code from the local console is required. There is no session |
| Register a key and confirm the request in Chrome | The administrator gets access to the camera registry. A repeated sign-in requires the password and a key confirmation |
| Send a wrong signature, then a correct one with the same request | Both requests are rejected |
| Grant and revoke operator access | The point appears and disappears in one open session. Camera management stays closed |
| Give the operator the administrator role | The previous session is revoked. Sign-in requires a key enrollment |

The command prints the report directory. `installation.json` keeps the matrix and the browser and test environment versions, `installed.json` the actual content of the node. `bundle/sbom.json` lists the packages, plugins, C++ libraries and web client components. `build-tools.json` gives the versions of the build tools and of the completed test, and the SHA-256 of the signed file list. To build with the supplier key, use `task release:package SIGNING_HOME=/absolute/gnupg SIGNER=<fingerprint> OUTPUT=/absolute/output` with an empty output directory. [Installation and initial enrollment](install.md) · [Configuration and archive recovery](recovery.md)

## Batch connection and channel rollback

`task camera:commissioning` runs two scenarios in separate databases. In the first the browser opens the recordings of two consecutive bindings of one point. In the second the ONVIF emulator announces one unit with four video inputs, with two H.264 profiles per input. Chrome saves a template and applies it to the four channels. The check compares the recorder acknowledgements and the presence of an archive block for each point. The `batch.json` protocol contains the original and actual revisions, the conflict, the partial failure and the image commands. The screenshots show the preliminary comparison and the results.

| Model and firmware | Capabilities and check |
| --- | --- |
| Four-input encoder, software emulator | A common endpoint, four ONVIF sources, separate points and recordings over RTSPS. Three sources support brightness from 0 to 100. The fourth allows changing the profiles without brightness setting |
| Fixed camera, software emulator | A separate camera without Imaging breaks the HTTPS connection between the comparison and the application. A repeat after recovery creates one binding. A rollback disconnects the camera and keeps the recorded blocks of the point |
| bacuri.vms ONVIF emulator and Camera B, software emulator | Replacing a device keeps the point. The new and the previous recordings open with the bindings to the corresponding devices |

When an update is prepared, the server rejects a pair of profiles from different channels and a stale template revision. After a lost response to a brightness write, a repeat compares the actual value. In the second session the name of one channel changes, and another input is disconnected before the application. The check compares the revision conflict, the addresses, the number of brightness write commands, the repeat after the connection returns and the rollback. The hardware series for the models and firmware of the full release runs by the [acceptance profile](release-profile.md#reference-devices).

An additional check reproduces a configuration without a physical input identifier. By the saved ONVIF profile, the server finds the previous point, swaps the main and secondary streams and adds the neighboring input as a separate point. After restoring a backup, the check compares all fields of the batch protocols and of all template revisions.

In the core run, 37 tests passed in each of the four C++ profiles, plus 136 browser tests. Line coverage was 76.45% for C++, 84.40% for JavaScript and 84.37% for the TypeScript of the workplace. The schema upgrade and the rollback kept all 32 checked archive blocks.

In an additional run with an instrumented RTSP emulator, LeakSanitizer found a 320-byte leak in Fontconfig when the emulator exited. The batch scenario with Core and the recorder under ASan/UBSan and a separate RTSP emulator from the GCC Debug build passed.

## Camera control and audio channels

`task camera:commissioning` checks the update of the command list, the selection of a preset and a limited patrol, zoom, focus, the illuminator, the wiper and the relay. The input state is read through PullPoint. A lost response does not cause a repeated movement. The command history stays available when the camera is disconnected, and the result is also in the action log.

The audio scenario compares the transmitted control signal with the samples at the output of the software camera after G.711 decoding. Intake, recording into an archive block, reading the interval, a refusal by permissions and the end of microphone transmission are checked separately. A hardware result requires observing the position, a contact closure or an independent control of the audio output for the corresponding function. [Scenario](../tests/cameras/operations.py) · [Hardware check criteria](release-register.md)

In the software profile, 39 CTest tests passed in each of the four build profiles, plus 168 browser tests in Chromium and Firefox. For speech transmission, receiving 1600 samples at the device output, the end of speech on permission revocation and the release of the browser audio track when the microphone is turned off were checked. The end-to-end scenario keeps the number of samples and the mean absolute error of the decoded signal in `camera-operations.json`.

## Permissions and corporate sign-in

`task access:check` starts a separate Docker Compose project with PostgreSQL, a synthetic RTSP camera, Chrome and Keycloak over HTTPS. Two independent sessions of one operator get the same permissions after local and corporate sign-in. The check uses two points, a group, recording intervals, separate permissions, a privacy mask and actual WebRTC and HTTPS delivery. [Scenario](../tests/access/scenario.py) · [Realm profile](../tests/access/realm.py)

| Change on the site | Local sign-in | Corporate sign-in | Limit |
| --- | --- | --- | --- |
| Removing a point from a group, time to the last SRTP packet | 0.218 s | 0.132 s | 5 s |
| Revoking viewing while an archive block is sent over HTTPS | 0.746 s | 0.778 s | 5 s |
| Revoking export while a file is sent over HTTPS | 0.594 s | 0.594 s | 5 s |

Before the Keycloak account was blocked, the stand waited for the active session to be renewed. After the block, Core refused access after 3.40 s, and the last SRTP packet was received after 3.70 s with a limit of 60 s. In isolation, the same e-mail for different subjects, the mandatory second factor of the administrator (password and TOTP, acr 2), local WebAuthn with the provider stopped, the expiry of a corporate session, a repeated sign-in after the provider started and the actor in the audit were checked. On a repeated configuration recovery, the binding of the external subject and the audit records made after the backup were kept. These are measurements of a single site. [Access management](ui.md#access-management)

On signed kits, an upgrade after an interrupted migration, a rollback to the original schema, the replacement of a lost WebAuthn key and recovery on an independent Debian system disk were checked. All 39 checked blocks were kept. The control archive frame played 7.37 s after the original node stopped, the frame of a new recording after 9.38 s. [Recovery scenario](../tests/offline/operations.py)

## Delivery profile

The numbers below apply to the compact site and are kept for the regression. For the full release, the extended configuration of the autonomous server, the reference models and firmware, the numerical load limits, RPO and RTO are fixed. The automatic qualification covers each declared profile. [Full release profile](release-profile.md) · [Requirement and test vector register](release-register.md)

The limits below set the qualification conditions of the signed kit. The basis is around-the-clock recording on one site, a one-month investigation horizon and core measurements on the reference node. The target node uses the CPU and memory configuration from the profile, Debian Trixie x86-64, a 1 Gbit/s network and a separate local ext4 volume for the archive. The bitrates, the series duration, the number of samples and the limits are set in the [checked profile](../tests/cameras/qualification.json). [Failure test results](#failure-and-recovery-tests)

| Metric | Limit and basis |
| --- | --- |
| Recording and viewing | 8 main H.264 streams of 2 or 4 Mbit/s, 8 secondary streams of 0.5 Mbit/s. GOP up to 1 s. One live viewer and one archive viewer serve monitoring and investigation at the same time. The main stream is recorded to the archive |
| Size | 30 days of 8 cameras need 5.184 TB at 2 Mbit/s and 10.368 TB at 4 Mbit/s. Allocate 14 TB after formatting. With a 20% margin for bitrate variation and free space, 11.2 TB remain for video, the container and the index. 1 TB = 10¹² bytes. The actual overhead is measured on the target volume |
| Operator waiting | p95 up to 2 s for the first live frame, the alarm delivery and the first search page, separately on a cold and a warm catalog. For the first archive frame p95 up to 3 s, including the search for the decoding point. A position error up to 1 s corresponds to the maximum GOP |
| Isolation | Zero additional gaps compared to the control recording, delivery stops within 5 s when a permission is revoked. Zero errors are expected on regular requests. Intentional failures are counted separately |
| Configuration and permissions | RPO up to 5 minutes with a preserved local copy at that interval. RTO up to 5 minutes from the loss of the database to sign-in and the check of a permitted and a foreign point. The budget includes deploying the copy |
| Historical archive | RPO 0 for healthy completed blocks on a preserved volume. RTO up to 15 minutes to decoding an earlier frame, including the catalog reconciliation. A damaged block and a complete loss of the medium are counted separately by the amount of the actual loss |
| New recording | RPO up to 30 s by the interval between blocks and RTO up to 60 s from the start of a controlled failure to the first decoded new block. The limit is calculated for short protocol failures and a healthy source after recovery. In the regression the small volume gave a break of up to 4.324 s and a time to the frame of up to 5.276 s |

The series uses synthetic H.264 frames. The protocol keeps the numerical results separately from the criteria. The hardware matrix and the mixed load have their own pass conditions. [ONVIF stand](rtsp-compatibility.md#onvif-stand) · [Measurement protocol](#measurement-protocol) · [Time limits by role](ui.md#first-use-check)

## Infrastructure check results

The checks performed on the existing core and the build environment are recorded below. The report of the automatic tests and the protocol of manual acceptance are produced on the release candidate.

In the full run with Gitleaks and clang-format, 2827 of 3171 lines of product C++ (89.15%) and 577 of 627 lines of working JavaScript (92.02%) were covered. Gitleaks finished three checks without findings. CTest passed on GCC, Clang, under ASan/UBSan and TSan, and in the coverage build. A repeated run measures both ratios again. [Acceptance criteria](quality.md#automatic-code-checks)

For the archive timeline and the camera connection form, `task ui-kit:check` passed 44 browser checks in Chromium and Firefox in light and dark themes. 24 checks are for the form. The focus of the first invalid field, the state of each stream, keeping the input, a single submission on a repeat and the handling of a late response after the address changed were confirmed. For the timeline, the gap bounds, time selection and the catalog update with the position kept were checked. Both components were checked at a width of 320 CSS pixels and a text size of 200%. By the source TS and TSX, 510 of 538 lines (94.80%) were covered. A control module that was not loaded was counted with zero coverage when the counting mechanism was checked. Full screens have a separate end-to-end protocol. [Archive timeline](ui.md#archive-timeline-in-the-ui-kit) · [Camera connection form](ui.md#camera-connection-form-in-the-ui-kit)

With Conan, two independent resolutions of the GCC and Clang profiles gave the same lockfile. After the move from the previous CMake directories, 31 tests passed in all four test presets, including the ASan/UBSan and TSan runs. The full `task check:report` run finished with code 0. On Release a separate alarm check in Chrome and Firefox opened the reference frame 16. `task build:repro` built the project again without network in a clean directory of the same image. All four executables matched byte for byte. [Reproducibility check limits](adr/010-build-dependencies.md)

From a clean copy of the sources, `task check:report` finished with code 0. The GCC and Clang builds, the initial CTest in regular and instrumented builds with ASan/UBSan and TSan, and static analysis passed. PostgreSQL answered a query over the Compose network. Both H.264 profiles of the synthetic camera decoded at the expected resolution.

`task stand:check-storage` confirmed that the test data of PostgreSQL and the archive survive stopping and restarting Compose. Filling the tmpfs returned `ENOSPC` at the 64 MiB boundary. When the container started again, the temporary volume was empty.

## Scenario matrix

| Scenario | Inputs | Pass condition |
| --- | --- | --- |
| Stand start | A clean copy of the repository, two RTSP profiles, PostgreSQL | The C++ build runs with GCC and Clang. PostgreSQL answers a query. Both streams decode at the expected resolution |
| Connection and recording | A real camera with two profiles and a synthetic control source | Both streams are received at the same time. The operator opens live video and plays a recorded interval, the reference frame is found within the interval between keyframes |
| Alarm and archive | An observation with a known identifier, point and time, then a repeat of the same request | One alarm leads to the correct interval. For known gaps and uncertain timestamps the corresponding state is shown |
| Slow client | A control run and a repeat with a limited client read rate with a healthy stream and volume | There are no additional recording breaks, the queues stay within the set limits |
| Recording interruption and recovery | A restart at each commit boundary, a lost database, filling and disconnecting the test volume, a damaged block | Exactly one catalog record for each checked completed block. Damaged and unavailable blocks are shown on the timeline. After the media returns, recording and reading are checked separately |
| Access | An administrator, an operator with a permitted camera, an operator outside the access scope, revocation during viewing | Search and media delivery respect the access scope. New requests after revocation are refused, active delivery ends within the permit term from the contract |
| Browser routes | Connection, a frame reference, an alarm, a gap and a break | The browser driver confirms the correct transition, the accessible names, the keyboard focus and the captures at the target widths |
| Cameras | The simulator and each mandatory pair of model and firmware | Discovery and replacement keep the point, a repeated event gives one alarm, PTZ is confirmed by the hardware, a forbidden command is refused |
| Workplace | A plan with two points, different permissions and a hardware event | The correct episode is opened, the focus and the operator's action history are kept, revocation limits further operations |
| Storage and export | A limited volume with blocks of different age, a permitted interval and a modified export copy | The cleanup policy holds after a repeat and a restart, a fresh recording is readable. Offline verification accepts the original export and rejects the modified one |
| Delivery and operation | A clean target node without internet, a signed kit, the previous version and a backup | Check the installation, the refusal on a wrong signature, an upgrade with data, a rollback, a recovery, access to the old and the new frame and the limits of a service subject's permissions |
| Full release content | Scenarios, contracts, profiles and stands | The role, the mandatory capability, the check method and the threshold are set for each row |
| Camera fleet and permissions | Two accounts, a batch of devices, PTZ, sound and codecs by the profile | Conflicts and partial failures are visible per camera, permissions are respected in all sessions, commands are confirmed by the device |
| H.265 (HEVC) | A physical camera with an H.265 Main main stream and an H.264 secondary stream, a reference HEVC stream, a browser with available and unavailable HEVC decoding | The actual codec of the original recording and of the export, live video, seeking in the archive, playback of an old interval after a codec change and compatible H.264 delivery for the browser are confirmed. When the conversion quota is exhausted, recording continues |
| Policies and recovery download | A schedule, a repeated video event, an available recording on a camera disconnected from the node | The pre-event and post-event recording match the reference, a repeated download keeps the coverage without duplicates |
| Multi-camera investigation | Recordings with known marks, a search area, masks, a full volume | The index, the desynchronization, the hold, the permissions and the playback of exports with exact bounds are checked |
| Built-in help | An isolated site, an operator and an administrator, a known error code and a symptom description in two languages | The menu and the context link open an article of the installed version. Switching opens the translation of the same article. Searching a symptom in Russian and English and an exact code works without internet. After an upgrade and a rollback both editions match the product version |
| Final manual series | A kit that passed the automatic qualification, new administrators and operators, the operating guide for the maintenance task | The time and the explanation of each participant are compared with the fixed limit and the result, there are no critical errors |

## Measurement protocol

The basic functional profile includes a controlled source and a physical IP camera. The physical configurations from the test profile are mandatory for qualification. For each test, the kit identifier, the expected and actual result and the exit code are given. An error, a missing mandatory row or a missing device blocks manual acceptance.

In the automatic series, the control recording and the mixed load are compared on one release candidate. The stand uses eight dual-stream sources. At a nominal main stream bitrate of 2 Mbit/s, the control and the mixed runs are performed, then both are repeated at 4 Mbit/s. Each recording run takes 5 minutes, including 1 minute of warm-up and 4 minutes of measurement. During the mixed run, recording, one live client, one archive client, search and test observations at 1 event/s run at the same time. For the first frame, the search window and the alarm latency, at least 200 samples of each kind are collected per bitrate in the mixed run. Cold search runs separately with a description of the initial cache state.

The main acceptance conditions are given in the matrix. The limit on this stand is up to 2 s at p95 for the first live frame, the alarm display and the first search page. Each run records p50, p95, p99, the number of samples, the error rate, the resource consumption, the queue depth, the breaks and the recorded size on disk. The storage cost is computed from the actual size per unit of time and the capacity of the selected medium. For each failure scenario, the start of the failure, the first detected gap, the moment the checked operation returns and the amount of unrecoverable data are recorded.

| Result field | What to record |
| --- | --- |
| Reproducibility | The project revision, the image and package versions, the hardware, the file system type, the network configuration and the source |
| Load | The number of streams and clients, the measured bitrate, the GOP, the event rate, the duration, the sample size and the start conditions |
| Observation | The expected result, the actual result, the metrics and links to the artifacts |
| Failures | The cause, the gap range, the checked copies, the data loss, the duration of the recovery of recording, the archive and the configuration |

## Core regression

The control run on Linux x86_64 with ext4 and Docker Engine passed all automatic gates. The SHA-256 of the list of checked sources in `files.json` is `50b7f457334907838aa59e99eea5bb03416f2a6120d0d0e1b14a01bec090a8be`. `task regression:check` produces a new report with its own hash.

| Check | Result on the synthetic RTSP camera and the local stand |
| --- | --- |
| Compilers, analysis and browsers | 31 CTest tests in each of the four profiles, 8 prototype checks and 60 checks of the UI Kit and screens. Gitleaks, clang-format and clang-tidy passed. Chrome and Firefox decoded the frame linked to the alarm |
| Line coverage | C++ 3066/3409 (89.93%), JavaScript 570/627 (90.90%), TypeScript 1985/2204 (90.06%). The 75% threshold is met separately for each area |
| Storage and recovery | Six commit interruption points passed in three profiles. With PostgreSQL unavailable, the acknowledged blocks were kept. After a Compose restart, the database and the archive were kept. Two independent Release builds gave a byte-for-byte match of the four executables |
| PostgreSQL upgrade | 48 indexes were rebuilt in the data copy. Before the upgrade, after it and after restoring a full backup, three archive records and a pending alarm matched |
| Maintenance | In 12 images, Conan and npm graphs and stand tools, 7754 component records were checked. There are no open blocking findings and no temporary exceptions. The offline run gave the same decisions |

`postgres-upgrade/result.json` keeps the images of the original and the upgraded PostgreSQL, the list of rebuilt indexes, and the keys of the found archive blocks and of the pending alarm. They must match on the original database, on the upgraded copy and after the server starts from the new full backup.

`task regression:check` checks the functional profile with the synthetic RTSP camera. For a repeat, the sources with the references, the Git revision, the SHA-256 of the content and the local changes flag are kept. A new build directory, a separate network and new volumes exclude the use of the data of the previous run. The results of the independent Release build and of the volume durability are counted separately.

`core/check.log` contains the results of CTest, the contracts, RTSP intake, recording, live video, alarms and search. `core/workspace` keeps the Chrome and Firefox screenshots and the measurements of the regular client, `core/recovery` the interruptions, losses and recovery. `core/dependencies` keeps the actual components, roles, sources and revisions of the information, the applicable advisories, the fixed versions and the exceptions. A critical advisory blocks the result until an update or a successful automatic check of a targeted protective measure. Missing maintenance information and an expired exception also block the result.

An offline repeat checks the snapshot hashes and applies the moment of the original assessment. For the core regression the snapshot freshness is limited to 30 days. Before qualification the information is refreshed for the specific candidate. For npm the source of the information is the record of the exact version in the registry and the project's fix channel, for the Node.js and PostgreSQL branches the supplier information. System packages are checked by the distribution data. For components inside third-party images, the image and the scanner report are kept as well. [Maintenance policy](adr/015-technology-lifecycle.md)

The protocol of the own RTSP implementation of a physical camera is filled in on a dedicated stand. The mixed load test runs on the release candidate. [Limits of hardware confirmation](rtsp-compatibility.md)

## Evidence set

For each mandatory requirement, record the scenario, the revision, the ADR, the command or task, the initial state, the expected and actual result and the location of the material. Interface screenshots and reference frames belong to the test materials. Technical logs are kept without secrets and video content. The final protocol also includes failed attempts and the results of repeated checks. [Requirement matrix](quality.md#acceptance-requirements)

| Area | Material and check method |
| --- | --- |
| Security | The ASVS Level 2 matrix with requirement identifiers, applicability, reasons and results. Bind to the kit hash the reports on administrator MFA and recovery, protection of the camera control and media channel, package signatures and the SBOM comparison. [Criteria and expected refusals](quality.md#release-candidate-security) |
| Core | Reports of the clean build, the contracts, the failures and the automatic routes through the regular sign-in. Compare the point, the time, the frame, the gap reason, RPO/RTO and the access scope |
| Delivery functions | For the changes, keep the contract tests, the screenshots and browser traces, the protocols of real hardware, the comparison of the cleanup, of the original and of the damaged export, and the installation and recovery results |
| Technical qualification | The hash and signature of the checked kit, the node and camera profiles, the commands, the results of all mandatory matrix rows on the same build, the reports of the control recording and the mixed load for both bitrates and the comparison with the thresholds fixed in advance. A skipped check blocks the release |
| Final acceptance | A protocol for each role and task, the time, help and errors, the explanation and the comparison with the reference, the result of administration by the guide. On a fix, link the new qualification report and the repeat of the final tasks. [UX protocol](ui.md#first-use-check) |

## Design decision registry

| Decision | Verified property |
| --- | --- |
| [ADR-001. API contracts, sessions and access](adr/001-api-contracts.md) | Permissions and the delivery term after revocation, administrator MFA, key replacement and revocation of previous sessions |
| [ADR-002. Process boundaries](adr/002-processes.md) | A delivery restart and a slow client keep the recording |
| [ADR-003. Archive commit](adr/003-archive.md) | One catalog record is restored for each completed healthy block |
| [ADR-004. Time binding](adr/004-time.md) | The frame matches the source revision, the precision is visible to the operator |
| [ADR-005. Media profiles](adr/005-media.md) | Live video and the archive decode at the same time with independent recording |
| [ADR-006. Stand sources](adr/006-test-stand.md) | The reference stream is reproducible, a physical device goes through its own run |
| [ADR-007. Screen components and states](adr/007-ui-states.md) | The states are distinguishable on a full screen and available from the keyboard |
| [ADR-008. First use](adr/008-first-use.md) | A new user reaches and explains a verifiable result independently |
| [ADR-009. Domain types](adr/009-domain-types.md) | The purpose of an identifier is checked by the compiler, an interval is checked at creation |
| [ADR-010. Dependencies and the Release build](adr/010-build-dependencies.md) | By the pinned graph the project builds without network in a ready image. A clean rebuild gives matching executables |
| [ADR-011. Offline installation and upgrade](adr/011-offline-delivery.md) | The signed kit installs without network, and after a rollback or recovery the permissions and the recording are checked |
| [ADR-012. Camera capabilities](adr/012-camera-capabilities.md) | Discovery, recording, PTZ and events are checked for a specific model and firmware |
| [ADR-013. Coverage threshold of product code](adr/013-test-coverage.md) | Each of the two reports confirms a coverage of at least 75% of lines |
| [ADR-014. Architecture of the single web client](adr/014-web-client.md) | One static build opens both routes directly, stale responses and media sessions do not change the state of the selected point |
| [ADR-015. Choosing and maintaining platform technologies](adr/015-technology-lifecycle.md) | The pinned components have a known fix source, an update keeps the data and contracts, the content and exceptions are compared with the kit hash |
| [ADR-016. Maintenance of system packages](adr/016-system-backports.md) | A package from the build contains a verified backport of the fix, the original version stays in the advisory check. A library change invalidates the evidence |
| [ADR-017. Map and floor plan sources](adr/017-map-sources.md) | The region map and the customer's plans open in an isolated network. A package update changes the source data date and keeps the camera bindings |
| [ADR-018. Boundary between Core and pluggable modules](adr/018-core-module-boundary.md) | CTest runs GoogleTest cases for all nine modules without PostgreSQL under the `core-modules` label. The build checks undeclared dependencies with `core-undeclared-module-link`. CTest `core-module-boundaries` checks the SQL and the includes of private Core headers. `task schema:check` compares the "schema, object" pairs with the catalog of the running PostgreSQL and checks the data migration, the permission revision and the revocation of a media session. With the source stopped, the site keeps the recording and the alarm history. A repeated observation keeps one alarm, a revoked service delegation is refused |
| [ADR-019. Data loading in the web client](adr/019-client-data-loading.md) | The UI Kit screen tests show loading, failure and refresh on the fixture. The end-to-end scenarios of the monitor, the maps, the access settings and the investigations read the same screens from the server |
| [ADR-020. Error catalog and localized failure messages](adr/020-error-catalog.md) | `task api:check` compares the catalog with the Problem codes, the server sources, the client messages and the error reference. The client type check rejects a code without a message |
| [ADR-021. Placement of video analytics](adr/021-video-analytics.md) | The labeled video set gives precision, recall and false alarms per camera-hour for each analyzer. A repeated analysis of the same archive gives the same observations. Stopping the service keeps recording and live video |
| [ADR-022. Video and map path of the desktop client](adr/022-desktop-video-path.md) | `task desktop:video` checks the first frame, the frame after a seek and after the decoder fallback, the decoder output memory, the map next to 16 cells and a station without GPU access |
| [ADR-023. Architecture of Bacuri VMS Client](adr/023-client-architecture.md) | The client builds every module only with its declared dependencies, `task desktop:test` runs the module tests and the bench and stand checks measure frame time, frame path and recovery |
| [ADR-024. Desktop post when the site is unreachable](adr/024-post-without-site.md) | Stopping the active node moves the post to another node with the rights checked again. With the emergency mode on, only granted cameras show live video with masks, and the site accepts the view journal after it returns |

## Single web client acceptance

`task workspace:check` uses Linux Docker Engine, an isolated PostgreSQL database, a synthetic RTSP camera with two profiles, the C++ services and clean browser profiles. The `workspace` report directory contains:

| Material | Criterion |
| --- | --- |
| `tests.json`, files in `artifacts/` and the references `tests/workspace/screens/` | Chromium and Firefox, light and dark themes, widths 320, 390, 768, 1440. A full screen matches its reference. The number of columns, the font, the heading size and the theme colors are checked. Zero violations of axe rules in the checked set, visible and returning focus. |
| Administrator and operator screenshots in Chrome and Firefox, the end-to-end test log | The configuration revision, the point, the alarm identifier, the UTC and the number of the decoded frame match. The administrator gets the recording of the main profile. The operator gets the secondary profile. An access error, a break, cataloging and a gap are checked separately. Revocation hides the video and the feed rows within 5 s. |
| `performance.json` | Three cold transitions per route in each browser for two builds. The active cookie is set before navigation. Time is measured from navigation to the available administrator form or to the card of a permitted alarm in the connected operator feed. The sum of the uncompressed bodies of the initially loaded JS and CSS counts, including the archive player modules. |
| `ui-coverage/index.html`, `coverage/index.html` | Reports by source lines. The common report counts the adapters of real services and product modules that were not loaded. Each module must cover at least 75% of its executable lines. |

The initial load budget is up to 400 KiB and up to 115% of the size of the control build. The control includes the same React, router, UI Kit and screens. The 15% margin is for the adapters and media modules. The median time to an available action is up to 2 s and up to the median of the control plus 1 s. The additional second is for the local check of the session and the permissions. The stand measures the interface latency separately from the user's time and the appearance of the first video frame.

In the control run, 60 browser checks of components and screens passed. For each of the four combinations of browser and theme, eight captures were checked. The administrator end-to-end scenario confirmed the application of the camera configuration and a 1280×720 frame of the main stream. For the operator, live video was 640 pixels wide. On the transition from an alarm, both browsers decoded the control frame 16. After the permission was revoked, the card and the previous video disappeared within 1.82 s in Chrome and 1.78 s in Firefox. Both browsers passed waiting for cataloging, the return after a restart of the HTTPS service, the reload of a direct link and the session expiry during background polling.

| Route | Control median, Chrome / Firefox | Working client median, Chrome / Firefox |
| --- | --- | --- |
| Administrator | 111 / 106 ms | 163 / 205 ms |
| Operator | 124 / 104 ms | 196 / 209 ms |

The initial JS and CSS took 357,523 bytes on the control build and 380,099 bytes on the working one. By source lines, the client ran 1998 of 2204 (90.65%). A sample check on a physical IP camera belongs to the separate hardware matrix. Independent completion of tasks by people is assessed in manual acceptance.

### ONVIF discovery and camera replacement

In the software profile, the wizard finds the synthetic camera by multicast, gets two H.264 addresses and creates a recording over RTSPS. After a replacement the point identifier is kept. In Chrome a 1280-pixel-wide frame from the previous camera and a 640-pixel-wide frame from the new one were decoded. To check recovery, the recording node is stopped before the replacement is saved. `task camera:commissioning` produces the screenshots and the result in `commissioning.json`. [Connection profile](rtsp-compatibility.md#connection-wizard)

## Failure and recovery tests

`task recovery:check` creates a separate Compose project with PostgreSQL and a synthetic RTSP camera. The recorder, the core and the media service use the working binaries. Two users, two points, a 1280×720 main stream and a 64 MiB tmpfs are part of the profile. The report contains a JSON on the failures and JUnit XML on the commit boundaries. In the common run these are the `recovery/<preset>.json` and `recovery/<preset>.boundaries.xml` files. [Recovery procedure](recovery.md)

| Interruption | Checked result | Measurement on GCC Debug |
| --- | --- | --- |
| The process ends after syncing the media, the manifest or the temporary directory | After the volume opens, the unfinished directory is not delivered | Three interruption points, zero published blocks |
| An end after the rename, the parent sync or the journal commit | A restart restores the completed block. A repeated registration is idempotent | One block in each of the three cases |
| Stopping the PostgreSQL process | During the failure the local journal grows, after the database starts all acknowledgements return | All 3 blocks recorded autonomously reached the catalog 5.216 s after the start of the failure |
| Stopping the RTSP camera | Recording resumes after the connection. The point and the bounds of `source_offline` are kept | A 3.602 s gap |
| Stopping the media service | A part request gets HTTP 503, recording continues, the previous session reads the part again | 1.119 s to the read |
| Filling the volume to zero free blocks | After space is freed there is a new block and a `storage_failure` interval | A 1.000 s gap, 2.025 s to the confirmation of recovery |
| Running the reconciliation without the volume mounted | A separate container sees the same database without the archive. The previous session and a new read are closed. When the media returns, the reconciliation runs again | 0 lost blocks, 6.166 s to decoding |
| One byte of a block changed | The size is kept, the SHA-256 differs. Healthy blocks are read, the damaged interval is visible on the timeline | 1 block, 1.000 s of lost video |
| The recovery process is interrupted | After the ban is committed and the process stops, the catalog stays closed. A repeat checks the files, lifts the ban and keeps the blocks unique | A repeated configuration recovery, an interrupted reconciliation, recovery without SQLite and a refusal of access to a foreign point were checked |
| The whole content of the only volume is destroyed | All known old blocks are unavailable. On a new volume, reading a new recording is checked | All 23 available blocks with a total of 1,513,063 bytes were lost. The new recording was read after 1.997 s |

For the three measurements below the RTO starts at the same point, the loss of the database. The end is recorded separately for each operation. The results of one automatic run on a small volume are given. The recovery speed on an archive of the design capacity is measured on the target medium.

| Object | Actual loss and RPO measurement | Result that stopped the RTO timer |
| --- | --- | --- |
| Configuration and permissions | Snapshot age 0.446 s. One change after the snapshot was lost | 0.938 s to a new sign-in and the check of the camera, the permitted and the foreign point |
| Historical archive | All 16 healthy completed blocks are available. The loss from the damaged block is 1.000 s | 1.884 s to decoding the old block over HTTPS. Each healthy block was read separately |
| New recording | 3.558 s passed between the end of the last block before the failure and the start of the first new recording | 4.704 s to receiving and decoding a new block after the recorder assignment was restored |

In a repeated GCC Debug run, the configuration and permissions were restored in 0.999 s, the previous fragment decoded in 1.986 s, the new one in 5.276 s. The snapshot age was 0.642 s, the break of the new recording 4.324 s. All 14 healthy historical blocks were checked. The single damaged block gave a loss of 1.000 s.

The commit interruptions model a stopped process with the Linux kernel running. The disconnection checks that the recovery process has no volume mounted. A test of a physical power loss and the disk performance on a full archive require a separate hardware stand.

### Map and plan check

`task workspace:check` opens the map in Chrome and Firefox in a separate internal Compose network without an external route. The Monaco control area contains one raster road network tile for each of the two source PBFs. Data © OpenStreetMap contributors, ODbL. The SHA-256 of the source extracts, the bounds, the tile address and the replication time are kept in the metadata of the [first](../tests/maps/previous.json) and the [second extract](../tests/maps/current.json). To build it again from the corresponding PBF, the [test tile generator](../tests/maps/render_fixture.py) with pyosmium, osmium-tool and Pillow is used. [Data source](https://download.geofabrik.de/europe/monaco.html) · [License](https://www.openstreetmap.org/copyright)

The scenario checks the PNG upload, point placement, the refusal of a foreign plan by a direct URL, the transition to an available recording, focus restoration, keeping the coordinates after an import and the rollback on a damaged package. The full end-to-end run uses a synthetic RTSP stream. The workplace report keeps the region and plan screenshots, the recording frame and the results of the loading budget check. [Map scenario](../tests/workspace/maps.py) · [Workplace scenario](../tests/workspace/scenarios.py)

When the installation and update over HTTPS are checked, the test catalog interrupts the transfer after 131071 bytes inside the first requested range. After the tab is closed and Core restarts, the start position of the next request, the automatic resumption and keeping a manual pause are checked. With a manual pause, the administrator stops the active transfer after receiving another 32768 bytes. The installation of another selected region, the absence of requests to an unselected region and keeping the active edition on a SHA-256 mismatch are checked separately. [Map package management scenario](../tests/workspace/map_packages.py)

When a fixed camera is checked, the workplace keeps different directions on the map and on the plan and restores the calibration from a backup. In the ONVIF connection scenario, the emulator reports the absolute position and the field of view. Panning from Bacuri, an external change of position, zoom, a lost connection, missing coordinates and a device replacement on the same point are checked. [End-to-end PTZ check](../tests/cameras/operations.py)

### Alarm handling and audit

`task alerts:check` checks two operators taking one alarm at the same time, an HTTPS break before the response and a repeat with the previous key. The owner, the revisions and the number of history records are compared. A refusal on a foreign point, a result recorded by a service, a ban on another action, an expiry, the revocation of a delegation and the loss of the initiator's permission are checked. Search is checked by text, identifier and the cursor of the next page.

`task workspace:check` takes the operator on duty from a test motion event to a numbered archive frame. The operator takes the alarm, records the result, closes it, finds the result in the log and returns to the same archive position. The run is performed in Chrome and Firefox. The log screenshots are kept with the workplace report.

In the `task camera:commissioning` run, the ONVIF emulator sends a notification through PullPoint. The browser opens the alarm from WSS, and an independent decoder compares the shown frame with the RTSP recording block. Then the operator closes this alarm and finds the own result in the log. The protocol contains the identifiers of the observation, the alarm, the block and three history records.

### Recovery of an installed node

`task maintenance:check PREVIOUS=/path/to/report` accepts a saved signed kit and its test key. The original kit must allow an upgrade and a rollback by the installer rules. Two KVM virtual machines use independent Debian Trixie amd64 system disks, an ext4 archive volume shared in turn and separate media for copies. The spare machine has the package preinstalled, no site, and a different system UID of the recorder. The video source is a synthetic RTSP camera. [Maintenance procedure](maintenance.md)

The scenario records video, creates an alarm and grants the operator access to the point. It interrupts the migration transaction, compares the schema version and the checksum of the backup, repeats the upgrade and separately performs a rollback with the signed kit of the previous version. Then, after a new backup, it revokes the administrator key. After a sudden power-off of the original machine it checks access over HTTPS, the permissions, the alarm history and the refusal of the previous keys and sessions. For an old frame the SHA-256 of the pixels is compared, for a new one the browser decodes the frame and checks for the test image.

`operations.json` keeps the kit identifiers, the number of preserved blocks, the audit events and the comparison with the profile limits. Time is counted from the power-off of the running node and includes booting the prepared spare. The configuration age is counted from the start of the last successful backup, the recording break between the end of the last completed block and the start of the first new one. `diagnostics.json` keeps the metrics, events and related OTLP spans for a disconnected camera, a stopped delivery, an accumulated catalog queue and a filled volume.

In the control run with the test kit, the SHA-256 of the signed file list was `f96befe906154aedb3f30a4d299fb7b0919d3938e1533779043b9b333c6969f8`. All 9 completed blocks were kept. For the archive frame the pixel checksum matched after the upgrade, the rollback and the system disk replacement. A damaged backup was rejected before the services were stopped.

| What was restored | Actual loss or backup age | Availability after the failure | RPO / RTO limits |
| --- | --- | --- | --- |
| Configuration, permissions and alarm history | 0.65 s from the start of the backup | 6.72 s | 300 s / 300 s |
| Completed archive | 0 blocks lost | 6.84 s to playback | 0 s / 900 s |
| New recording | A 9.85 s break | 8.35 s to playback | 30 s / 60 s |

On the signed kit, two upgrade paths from different source schemas were checked. In each run the migration completed after the interruption by a repeated start, the rollback returned the original schema, and all 9 completed blocks were restored on the spare system disk. The SHA-256 of the pixels of the control frame matched the original. From the power-off of the original machine to the playback of a new recording, 8.29 s passed in the first run and 9.17 s in the second.

### Export and offline verification

`task export:check` opens an alarm in Chrome and saves the package with the regular button of the workplace. The synthetic source has 25 frames per second. The control interval starts and ends inside a block and includes one original MP4 and a second without a camera connection. A separate container with the network disabled checks the trusted site key, the origin, the bounds, the durations of the re-encoded MP4 and the numbers of all frames.

The package is kept in `episode.tar`, the public key in `trusted-key.json`. `browser.json` contains the control manifests, the responses to refusals and the audit events. `verification.json` contains the package checksum, the verified intervals and the frame numbers. The selection and result screenshots are kept there as well. The command prints the path to the material directory. [Export scenario](../tests/archive/export.py) · [Offline verification](../tests/archive/verify_export.py)

### Retention and capacity control

`task retention:check` uses an isolated PostgreSQL, a controlled catalog clock, a synthetic RTSP camera and a separate 64 MiB tmpfs volume. A microsecond before the boundary the block is available in the catalog. At the boundary a previously issued HTTPS link to this block closes, including with the recorder stopped. The next block of the same media stream is read by its own link. After the recorder starts, the files, the PostgreSQL rows and the local journal are checked. Extending the term and catalog recovery are checked with a previously expired block.

An external file on the test volume brings the free space to the warning level, then to the reserve. The test compares the diagnostic state, the appearance of a gap, the content of a protected fragment over HTTPS and its checksum on disk. After space is freed, the commit of a new block is checked. The `capacity.json` report in the run directory contains the capacity and forecast measurements and the comparison result.

`task test` interrupts the process after the cleanup boundary is committed, after the block directory is renamed and after the files are deleted. Each variant opens the volume again and checks that exactly one active block remains and that a repeated cleanup keeps it.

## Live profiles and multi-camera viewing

`task monitoring:check PRESET=gcc-debug` starts Core, the recorder and the delivery service on a separate database and opens the workplace over HTTPS in Chrome and Firefox. Both posts get one shared layout of four cameras. The scenario checks the order of the cells, the camera permissions, keeping a personal layout, a revision conflict and the video wall. After one camera is expanded, the browser confirms the size of the decoded frame of the main and secondary streams. After the return to the grid, the second post keeps receiving frames. The log shows the latencies, the resolutions, the number of presented and dropped frames, and the RSS of the recorder and the delivery service. For the control recording the number of frames of each GOP and the UTC breaks are checked.

The common live budget is set by the Core variable `BACURI_VIDEO_BUDGET_KBPS` in kbit/s, 64000 by default. `BACURI_VIDEO_TRANSCODERS` sets the number of simultaneous conversions for live video and the archive together, 1 by default. The value 0 closes delivery that requires a conversion. In an installed node the Core service reads both variables from `/etc/bacuri-vms/core.env`. In Compose they can be set in the project environment.

The series includes H.264 and HEVC Main 8-bit main streams. For an HEVC/H.264 pair, the selection of the secondary H.264 without conversion is checked. For an HEVC/HEVC pair, H.264 delivery over WebRTC and the refusal of a second task with the converter busy are checked. The original archive block and the compatible copy are read through REST. The decoder configuration in fMP4, the decoded pixels before and after masking, the refusal on a policy change and the refusal when the budget is exhausted are checked. `task playback:check` plays neighboring H.264 and HEVC segments with a resolution change in Chrome and Firefox and compares the color and the frame number at the transition and on seeking. The software series checks the mechanisms of the workplace. The installation parameters and the hardware of the full release are set in the [acceptance profile](release-profile.md).

In the control run of two posts, expanding a cell from 640x360 to 1280x720 took 858 ms, restoring four decoded cells 957 ms. Over the control interval, eight consecutive recording blocks were checked without lost frames or UTC breaks. The RSS was 57236 KiB for the recorder and 140956 KiB for the delivery service. The first frame of the compatible HEVC copy over WebRTC was received in 1259 ms. In this configuration Firefox decoded the original HEVC from the archive, and Chrome used the compatible copy. A GStreamer receiver with 640x360 decoding checked the direct HEVC delivery over WebRTC. In this run both browsers announced only H.264 of the two checked codecs for WebRTC.

## Recording policy check

`task recording-policies:check` starts an isolated Core, recorder and RTSPS source. The sequence number of the frame is encoded in the top band of each image, and a control tone is fed to the audio input. The check reads MP4 from the saved blocks, decodes the numbers and checks their continuity taking the counter overflow into account. By the manifest marks, 5 s before the first event and 10 s after the last one are compared, including manual recording and the repeated delivery of an observation. The start of the video is rounded to the previous keyframe, the audio is limited to the selected interval.

After the recorder restarts, the scenario feeds a new event, removes the camera exception and checks manual recording without an audio track by the group policy. Then the weekly schedule intersects with the manual and event windows. The frames continue after both windows end. In the native test, eight distinguishable H.264 frames pass through the recorder with UTC marks before, inside and after both occurrences of the repeated Europe/Berlin hour. Four expected numbers are decoded from the MP4. The spring gap and an interval across midnight are checked separately.

Chrome opens the active group policy through a camera, saves an exception and reads the actual capacity. The report directory keeps the editor screenshots. The API is checked for insufficient permissions, a repeated manual start and a revision conflict. After the term is shortened, the visibility in the catalog, the deletion of the file, the independent boundary of another point and keeping the reached boundary when the term is extended are checked.

`task retention:check` fills a separate 64 MiB tmpfs volume. The warning, the stop at the reserve, access to the already recorded material and the resumption of recording after space is freed are checked.

## Archive and investigation check

`task investigations:check PRESET=gcc-debug` starts the stand, runs the checks on separate temporary databases and produces a report in `build/reports/investigations-*`. Software sources with known frames are used. The hardware check requires recording on a device from the [qualification matrix](rtsp-compatibility.md#hardware-matrix-for-the-release).

| Scenario | Reference and observed result |
| --- | --- |
| Recovery download | On an H.264 source the number is written in the pixels. A connection break interrupts the transfer, a repeat fills the available intervals. The numbers of the decoded frames, the absence of duplicates and keeping the five-second gap are checked. A separate available minute is checked completely. |
| Video rules | The labels of two hundred episodes are pinned by SHA-256 before the run. The same video goes through six independent checks: zone, exclusion, line, amplitude, number of cells and schedule. One reference can match one detection with a deviation of up to a second. Real commands on the test source and the absence of an action in a simulation are checked separately. |
| Integrity and holds | On a private volume four points of 5 minutes each at 25 fps. The reference videos are decoded before the start. After the reserve is reached, the cleanup and the journal recovery, the SHA-256 of all 20 blocks on both volumes are compared. The `protected-recording.json` report includes the media size of each copy, the number of frames and the recovery time with the comparison. |
| Search and viewing | In the browser the numbers of four decoded frames are compared by pixels, including the transition across the boundary of neighboring blocks. One point has a clock offset of +2 s, another has its index temporarily unavailable, and an explicit break is introduced. `measurements.json` contains the desynchronization, a sample of HTTPS response times and the time to stop delivery after a permission is revoked. The processing of previously queued blocks while four cameras keep recording is checked. For a cold catalog, the pages of the archive tables, their indexes and TOAST are evicted from the PostgreSQL buffers of the temporary database. The next request measures the warm catalog. The host file cache is kept. |
| Case and release | The case is passed to a second operator, and the release is checked before and after a permission change. A separate container without network checks both signature chains, the exact frames, the masks and the audio samples. A substituted signature leads to a refusal. |

In the control run each of the six detector checks gave TP 100, FP 0 and FN 0. In two series of 200 HTTPS requests p95 was 82.49 ms for the cold catalog and 82.09 ms for the warm one. In all samples of synchronized viewing the frame numbers on four tracks matched. The package transfer stopped 0.716 s after the permission was revoked. In the five-minute hold check, 20 blocks with a total of 5,171,815 bytes were checked on each volume. The journal recovery and the comparison of both copies took 0.123 s. All 30,000 frames were kept.

The measurements on the compact software stand apply to the given videos and resources. The full load and the hardware check are performed by the [extended installation profile](release-profile.md).
