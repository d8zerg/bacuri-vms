# Changelog

## [Unreleased]

### Added

- ADR-024 defines the desktop post when the site is unreachable: the client reconnects to another node of the installation and restores the post with the rights checked again, and an optional emergency mode, off by default, shows live video of granted cameras directly with masks, no recording and a view journal for the site audit. The desktop plan adds step 5.14 and the parity register adds the site failover and emergency live scenarios. [ADR-024](docs/adr/024-post-without-site.md)

- The video cells of a Bacuri VMS Client grid play recorded video on a common timeline: one clock and one base time, a grid start and seek on worker threads four cells at a time, and a cell that fell back to software decoding rejoins the timeline. The pipelines share the CUDA, VA and GL display contexts, the hardware decoder is NVDEC, then VA-API. A station without a GPU is detected by its software OpenGL renderer and decodes in software with cells limited to 640x360 at 15 frames per second. 16 cells start in 1.6 s instead of 2.9 s, seek in 0.9 s with the longest interface frame within 40 ms instead of 0.55 s, and stay within 2 frames of each other instead of 11. The bench reports the grid start and seek, the spread between cells and the longest frame. [ADR-022](docs/adr/022-desktop-video-path.md)

- Bacuri VMS Client has an architecture of layers and modules with declared dependencies, one owner for each object of the site, a common command registry and a user interface built on tokens shared with the web client. The desktop code is split into the diagnostics, site, media and map modules with a video and map bench and a sign-in check as tools. [ADR-023](docs/adr/023-client-architecture.md)

- `task maps:fixture` builds a Monaco raster MBTiles package from the OpenStreetMap extract for the server tests, and `task maps:package:check` imports it into the stand and compares the served scene, style and tiles with the package. The desktop client bench shows the installed map of the site: MapLibre loads the style and the tiles through a local gateway that adds the session token. [ADR-022](docs/adr/022-desktop-video-path.md)

- The desktop client signs in through the system browser with PKCE S256 and a loopback address. A signed-in browser confirms the sign-in on the "Sign in to Bacuri Desktop" page and issues a one-time code, the client exchanges it for its own session and presents it as a bearer token. API 1.7.0 adds `issueDesktopCode`, `createDesktopSession` and the `desktop_code_invalid` error. `task desktop:sign-in` checks the sign-in through Chromium on the stand. [Desktop session](docs/adr/001-api-contracts.md#local-session-and-access-rules) · [Desktop application sign-in](docs/ui.md#desktop-application-sign-in)

- The video and map bench of Bacuri VMS Client plays H.264 and H.265 in 1 to 16 cells with NVDEC or software decoding next to a MapLibre map and reports the decoder output memory, the first frame and the frame after a seek, the interface frame time and the CPU and GPU load. A failed hardware decoder falls back to software decoding from the last shown frame, and a station without a GPU uses software decoding and drawing. `task desktop:video` runs the series on the stand with reference recordings, a Monaco test map and live cameras. [ADR-022](docs/adr/022-desktop-video-path.md)

- The desktop workstation passport names the build image, the stand hardware, the media sources and the server profile. `task desktop:passport` compares it with the stand and the desktop image with the GPU, and `task desktop:reference` writes and verifies H.264 and H.265 reference recordings with a frame number in every frame. [Workstation passport](desktop/workstation.yaml)

- The desktop client has a parity register that maps every public operation, web client section and console action to desktop scenarios with roles, failures, steps, tests and acceptance material. Extensions beyond the web client name the contract they need, how the client detects it and the behavior with an older server. `task desktop:parity` checks the coverage and the reconciled contract revision. [Parity register](desktop/parity.yaml)

- An error catalog names every cause of a failure with its category, HTTP status, gRPC status code, retry class and audience. The server, the API contract, the service gRPC and the web client take their codes from it, and the client shows each cause in the interface language. [Error reference](docs/errors.md) · [ADR-020. Error catalog](docs/adr/020-error-catalog.md)

- The workplace interface switches between Russian and English without a reload. The switch is next to the theme selection, the choice is saved, and the browser language is used by default. Dates, times and numbers are formatted in the selected language.

- The control and mixed recording series take 5 minutes per mode. The archive protection check compares two copies of a five-minute recording from each of four cameras after the volume is filled and the catalog is recovered. [Test profile](docs/test-profile.md#archive-and-investigation-check)

- Camera archive recovery download, ONVIF and local detector video rules, a second verifiable archive copy and interval holds were added. The "Investigations" section provides motion and event search, synchronized viewing of four points and cases with a signed export of the original or a derivative. [Working with an investigation](docs/ui.md#investigations) · [Replica volume](docs/maintenance.md#second-archive-copy)

- Camera and group recording policies combine a schedule, motion with pre-event and post-event recording, and manual and continuous modes. The "Recording and storage" section provides sound, archive depth, retention and the volume reserve. [Recording settings](docs/ui.md#recording-and-storage)

- The operator can open a personal or shared layout, expand a camera and show the grid on the video wall. Each cell shows the actual profile, codec and resolution. The H.264 or HEVC Main main stream is recorded without re-encoding. For viewing, Core takes into account the client decoder and the common budget of the posts. Protected areas are painted over before a frame is sent. [Workplace](docs/ui.md) · [Profile and layout check](docs/test-profile.md#live-profiles-and-multi-camera-viewing)

- The console in the live window combines pan, stop, zoom, focus, presets, patrols and sound. The optics can be controlled from the keyboard. In full screen viewing the console stays next to the frame. [Camera control and sound](docs/ui.md#camera-control-and-sound)
- In live video the operator controls zoom, focus, presets, patrols, the illuminator, the wiper and the relays within the capabilities of the ONVIF profile. The states of the digital inputs, the control owner and the command result are visible. When an acknowledgement is lost, the log keeps an unknown outcome. The connection wizard supports unicast discovery of cameras behind a router. [PTZ control](docs/rtsp-compatibility.md#ptz-control-and-motion-intake)
- Independent permissions and controls for listening to the camera, recording sound to the archive and speech transmission. The recorded sound is bound to the original video block and verified by SHA-256. [Camera control and sound](docs/ui.md#camera-control-and-sound)

- In the "Cameras" section the administrator can prepare a batch of ONVIF channels, compare the settings with the site template and check the revision applied by the recorder. The list is grouped by device and shows the state of each channel. The expanded row provides the stream comparison, the revisions and a rollback. [Camera batches](docs/ui.md#camera-batches)

- The top bar of the workplace got a switch of the system, dark and light themes and a sign-out icon. The browser keeps the selected mode.

- In the "Access" section the administrator manages users, point groups, permissions, privacy and Keycloak bindings. The server checks the scope when video and exports are delivered and writes changes to the audit. Corporate sign-in is optional, local sign-in with WebAuthn works autonomously. [Access management](docs/ui.md#access-management)

- A common sign-in contract is described for the web and desktop clients. For testing, a requirement register and measurable acceptance criteria are defined. `task docs:check` checks the operations, links and test vectors of the register. [Register](docs/release-register.md) · [Acceptance profile](docs/release-profile.md)

- Signed offline kits for Debian on amd64 include PostgreSQL. The installer checks the operating system and the architecture before running APT. [Installation](docs/install.md)

- New indexes speed up the archive timeline search, the selection of plan cameras and the map queue processing. A reproducible comparison of query plans was added. [PostgreSQL measurements](docs/postgresql-performance.md)

- On the map and plans the operator sees new alarms at the cameras, checks the recording of the selected event next to the plan and switches to live video. The handling card can be opened from the preview. [Checking an event on the map](docs/ui.md#checking-an-event-on-the-map)

- Optional georeferencing of a plan to the map by two landmarks, or by one landmark, the width and the azimuth. The map shows the building, the selected floor and the camera directions. When aligning, the administrator checks the overlay and chooses the coordinate source for previously placed points. [Georeferencing a plan to the map](docs/ui.md#georeferencing-a-plan-to-the-map)

- Camera icons and sector calibration by a landmark appeared on maps and plans. For PTZ the workplace shows the orientation confirmed by the camera and the change of the field of view with zoom, and on a lost connection keeps the time of the last measurement. [Cameras and view sectors](docs/ui.md#cameras-and-view-sectors)

- The administrator can choose several regions to install from an HTTPS catalog or a ZIP and update the installed maps from any source. After a connection break or a Core restart, the download from the catalog continues from the saved position. The administrator can pause a single region and resume the download later. [Region management](docs/ui.md#installing-and-updating-regions)

- Upgrade of an installed node, rollback and recovery of the node from an encrypted backup with a preserved archive volume were added. The administrator can revoke a lost WebAuthn key through the local console. Diagnostics link the state of the camera, the services, the catalog and the volume with the system journal and traces. [Upgrade and recovery](docs/maintenance.md)

- Bacuri VMS can be installed from a signed offline kit on Debian on amd64. A local command configures the systemd services, the archive volume and TLS. Administrator sign-in requires a password and a WebAuthn key with user verification. [Offline installation and initial enrollment](docs/install.md)

- A signed MP4 package can be saved from the archive of a point and from an alarm. A separate permission limits the point and the interval. A standalone utility checks the origin, the bounds, the checksums and the site signature. [Fragment export](docs/ui.md#fragment-export)

- The administrator sets the archive retention and the volume reserve. Diagnostics show the depth forecast, a low space warning and a recording stop. Expired blocks are closed for reading and deleted, continuing after a restart. [Retention setup](docs/ui.md#archive-retention)

- The operator on duty can take an alarm, save the check result and close it with a reason. The log lets the result be found and the corresponding archive opened. A service can be delegated one action on the selected point until a given term. [Alarm handling and the log](docs/ui.md#alarm-handling-and-the-log)

- In the "Maps and plans" section the administrator uploads site plans and places points. The operator opens live video and the archive of an available point and returns to the map. An OpenStreetMap region is imported from a local MBTiles, and the interface shows the source data date and the attribution. [Preparing a region and working with plans](docs/ui.md#region-map-and-plans)

- In live video the operator can pan an ONVIF camera and stop the movement. The available commands are determined by the profile, the movement duration is limited to one second. PullPoint motion notifications enter the alarm feed with a transition to the recording frame. A repeated delivery keeps one alarm. [PTZ control and motion intake](docs/rtsp-compatibility.md#ptz-control-and-motion-intake)

- The connection wizard finds cameras through WS-Discovery and gets the addresses of H.264 profiles through ONVIF. Replacing a camera keeps the point and the previous recordings. HTTPS and RTSPS with certificate and name checks apply by default. Open RTSP requires an exception by camera address. [Connection and check](docs/rtsp-compatibility.md#connection-wizard)

- `task camera:check` checks the test profile and eight functions of the ONVIF emulator on synthetic H.264 video. `task camera:hardware` runs the same series by the register of physical models and firmware. [ONVIF stand](docs/rtsp-compatibility.md#onvif-stand)

- `task packages:build` exports the maintained libxml2 and libsoup packages for Debian with the sources and build materials. The check links each fix to the bytes of the installed package and keeps checking the advisories for the original version. [ADR-016](docs/adr/016-system-backports.md)

- `task regression:check` repeats the functional tests of the core with a clean build and empty volumes. The report keeps the sources, the screenshots, the recovery results and the Release checksums.
- `task deps:check` checks the product, build and test dependencies by the image content, the Conan graphs, the npm installation and the supplier advisories. A snapshot can be checked again without network. [Core regression](docs/test-profile.md#core-regression)

- The administrator can save an encrypted backup of the site configuration and permissions and recover the catalog from verified archive blocks. Access to the archive is closed until the reconciliation completes. The journal keeps the confirmed gap intervals. [Guide](docs/recovery.md)
- `task recovery:check` checks publication interruptions, stopped services, filling, disconnecting and losing the volume. The report contains the losses and the time to return the configuration, the reading of the earlier archive and the new recording.

- The administrator and operator workplace is available through one HTTPS sign-in. In the "Cameras" section the administrator connects a camera, checks both streams and confirms a new recording after a frame is decoded. In "Monitoring" the operator opens the archive at the time of an alarm and returns to the selected card with the point, the interval and the feed position kept.
- A React and TypeScript UI Kit with a local Storybook catalog. The archive timeline supports time selection from the keyboard and shows the reasons for gaps. The camera form checks the stream addresses and keeps the input for a retry. The same components, the player and the alarm feed are used on the work screens.
- `task web:build` builds the web client. `task workspace:check` checks the screens in two themes at four widths, keyboard accessibility and the initial load budget. End-to-end scenarios in Chrome and Firefox compare the camera revision and the actually decoded frame and check connection recovery and access revocation.

- `task check` got Gitleaks, clang-format and a check of at least 75% line coverage separately for product C++, browser JavaScript and the TypeScript of the single client. The TypeScript report maps the executed code to the source lines and counts the modules that were not loaded.

- The common run includes the documentation check and the mockup scenarios in Chromium and Firefox with accessibility checks and full screen comparison.

- The C++ skeleton of `bacuri-vms-core` with the `--version` command and CMake presets for building with Ninja on GCC and Clang.
- A local Docker Compose stand with PostgreSQL, a synthetic camera with two RTSP/H.264 streams, a persistent archive volume and a limited volume for fill tests.
- Go Task tasks for the build, CTest, static analysis and Compose stand management. `task check` runs the full set of checks, including ASan/UBSan, a PostgreSQL query and decoding of both RTSP streams.
- A report of local checks and a test that the database and the archive survive a Compose restart.
- CTest with ThreadSanitizer in a separate container for the `clang-tsan` build.
- A C++ domain model for resources, binding revisions, time, the archive and observations.
- Initial PostgreSQL and SQLite migrations, the archive block manifest and the `task schema:check` check.
- REST/OpenAPI, gRPC and WebSocket contracts, the access model and media session delivery terms.
- Checking contracts and examples with `task api:check`. Generating and checking TypeScript types with `task api:types`.

- An HTTPS control server on Boost.Asio and Boost.Beast, local sessions, a camera registry with audit and access control.
- Configuration delivery over gRPC and recovery of the encrypted snapshot in the recording process. The `task control:up` and `task control:check` tasks.

- Intake of two RTSP/H.264 profiles in the recording process, independent bounded queues, connection recovery and diagnostics of gaps and time binding.
- An RTSP emulator based on a Linux V4L2 webcam. The `webcam:up`, `webcam:check` and `webcam:down` tasks and the reproducible `ingest:check` scenario.

- An fMP4 archive of the main stream with a manifest, SHA-256, atomic publication and an SQLite journal. The PostgreSQL catalog accepts a repeated registration and reflects the volume reconciliation result.
- `task archive:status` for diagnostics and `task archive:check` to check recording, interruptions and catalog recovery.

- Archive search by point and time with permission checks, a cursor and the reason for a break. Confirmed breaks are kept in the recording journal and passed to the PostgreSQL catalog.
- An archive player with seeking by UTC, the `bacuri-vms-media` process and streaming HTTPS delivery of fMP4 parts with a short permit check. `task playback:check` checks the frames and access revocation in Compose.

- Live video of the secondary H.264 stream through WebRTC in Chrome and Firefox. HTTPS signaling is bound to the observation point and the browser session. Recording of the main stream works independently of viewers.
- `task live:check` measures the first frame and the end of delivery over the network, and checks the archive with a slow client and a restart of the delivery process.

- Durable intake of observations over gRPC, a motion rule and transactional publication of alarms in PostgreSQL. The test source supports creating and resending one observation.
- An alarm feed over WSS with permission checks, resumption by cursor and the transition to an archive frame that takes into account the binding revision and the clock uncertainty. `task alerts:check` checks the end-to-end scenario in Chrome and Firefox.

### Changed

- Each error code has one HTTP status. A wrong method returns `method_not_allowed`, an unavailable media service `media_service_unavailable`, and a changed object behind `If-Match` `precondition_failed`, which replaces `policy_conflict`. An unexpected failure returns `internal_error` instead of `authorization_unavailable`. The service gRPC sends the code of the cause as the status message.

- HTTP request size limits and unsupported expectations return a Problem with a request ID. The workspace displays the reported cause for failed media offers, archive reads and map installations. Recording policy changes can be found in the journal by policy ID.

- The server no longer writes text for people. A failed corporate sign-in returns to the workspace with the code of the cause, and changes of recording settings are journaled as codes with their values.

- The control sources live in the directories of their owning modules. The ONVIF adapter is split into SOAP transport, discovery, stream profiles, imaging, PTZ, camera recordings and events, and is built as a library. Core calls it through the `DeviceFamily` interface, which selects the adapter of a camera from its control settings. [ADR-018. Core and pluggable modules](docs/adr/018-core-module-boundary.md)

- The investigations, recording policies, maps and access screens of the web client are split by task. They read server data through one layer with a cache per session, cancellation, retries of transient failures and polling. The recording policy editor keeps its save and confirmation steps in one state. [ADR-019. Data loading in the web client](docs/adr/019-client-data-loading.md)

- Times in the investigations, the recording policies and the video rules are formatted in the selected interface language.

- Every error code of the server is listed in the OpenAPI contract, and `task api:check` compares them with the server sources.

- Live video, archive playback and the alarm feed work only in the single web client. The separate pages `/live/`, `/archive/` and `/alerts/` were removed. The archive player and the WebAuthn sign-in step are part of the client bundle, and the browser scenarios drive the monitor of the client.

- The Storybook fixture of the client answers with the shapes of the OpenAPI contract and UUID identifiers. `task ui-kit:check` validates its responses against the contract, and the end-to-end scenarios validate every HTTP response. The WebSocket upgrade of the alarm feed is part of the contract.

- The export verifier `bacuri-vms-verify-export` is built from verification, manifest, MP4 and identifier libraries and links only libsodium. An architecture test rejects SQLite, GLib, GIO, GStreamer and the archive volume in its sources. [Archive format](docs/archive-format.md#verification-tools)

- An archive block is written by an own fragmented MP4 writer instead of a GStreamer pipeline per block. The CPU time per block dropped about five times. The live session waits for connection state changes instead of polling. [Archive format](docs/archive-format.md)

- The recording state of a point is derived from the archive catalog and the report of the recording node. "Recording in progress" is shown only when a recently received frame is already in an available block. For a stopped or lagging recording the reason is visible, and the age of the last frame in the archive is shown next to it. [Product principles](docs/product-principles.md#3-archive-truth)

- The recording node starts recording right after the start. Archive checksums are verified in the background with a limited read rate, and a damaged block is restored from the second copy. The catalog recovery tool still checks the whole archive before publishing the result. [Maintenance](docs/maintenance.md)

- The core reuses PostgreSQL connections. Workplace requests, password checks and long operations with cameras, maps and export are served by separate pools, so a recovery download or the sign-in of other users does not delay the operator.

- The documentation is written in English, with Russian versions in `README.ru.md` and `docs/ru/`. `task docs:check` checks that each document has its pair and that both link to each other.

- The "Cameras" section opens the catalog of connected devices. A neighboring subsection provides manual connection, ONVIF discovery and batch connection. Workplace lists and the "Access" subsections use a common panel with search, reset and refresh icons. In the monitoring feed the alarm states switch with one press. The timeline is available in "Archive" and loads the selected moment after the handle is released. Switching the mode stops the previous player. [Workplace](docs/ui.md)

- In the activity journal the search panel and the alarm filter are above the records. Plan zoom is chosen with one press. The active menu item and the browser tab mark the open screen. The work area starts with the point selection, the map, the filters or the connection form. [Workplace](docs/ui.md#visual-system)

- The "To alarms" button leads to the feed when it is under the video. The journal is renamed to "Activity journal". An empty journal explains when the first record appears. [Alarm handling and the log](docs/ui.md#alarm-handling-and-the-log)

- In a wide window the subsections with icons are placed under the active section of the side menu. On a phone the buttons of the "Access" subsections are aligned in a grid. [Workplace navigation](docs/ui.md#visual-system)

- On a wide screen the workplace sections open from a collapsible menu on the left. Refresh is marked by an icon with a tooltip. In the camera and alarm cards the transition actions are buttons. [Workplace](docs/ui.md#visual-system)

- Channels are grouped by device in the selection and in the inventory. Access management opens the selected account, and the privacy form shows the consequences before applying. The logs show details on request. On the map the coordinates open when a point is edited. [Workplace](docs/ui.md)

- In monitoring the alarm archive can be compared with live video while keeping the card and the result draft. The feed supports a state filter and the return to an event on a phone. The log is available from the main navigation, and the camera wizard shows the check progress before the player opens. [Work scenario review](docs/ui.md#work-scenario-review)

- A migration places the accounts and permissions, media sessions, maps and the retention policy in the schemas of their owners. The package supports upgrading previous installations and rolling back to the original version by a saved snapshot. [Site maintenance](docs/maintenance.md)

- Inside Core the device registry, the recording catalog, media sessions, permissions, alarms and maps use the operations of the data owners. CTest checks the C++ dependencies and the ownership of PostgreSQL objects. [Core boundary](docs/adr/018-core-module-boundary.md)

- The screen scenarios, reference captures and the icon check run in `task ui-kit:check` on the working web client. The separate static mockup is no longer maintained. [Browser checks](docs/ui.md#browser-checks)

- The Debian package and the local administration command are named `bacuri-vms`. `task install:check` and `task maintenance:check` check the installation and maintenance, `task release:package` builds the signed kit. [Installation](docs/install.md) · [Upgrade](docs/maintenance.md)

- A check of the PostgreSQL upgrade on a separate physical copy with index rebuilds, a comparison of the archive and the alarm queue and recovery from a new full backup was added.
- The UI Kit catalog image uses the fixed Brotli package from Alpine. The gate checks the image and the Conan build utilities.
- PostgreSQL and Selenium on the test stand, Node.js in the API generator, ESLint and c8 were updated.

- The automatic qualification of the release candidate includes the mixed load and failure isolation check.

- The light and dark themes use common typography, spacing and color tokens. Controls, selected elements and the archive timeline got muted golden accents. The platform mark and the favicon use an image of a bacuri cross-section.
- Criteria for choosing supported versions, checking compatibility and reviewing dependencies are fixed for the platform, build and operation stack. [ADR-015](docs/adr/015-technology-lifecycle.md)

- Boost, nlohmann/json and GoogleTest/GoogleMock are delivered through Conan with a common lockfile for GCC and Clang. Offline compilation in a prepared image, a Release preset and a byte-for-byte comparison of the results of a clean rebuild were added.

- The webcam RTSP emulator keeps the RTP timeline of the main and secondary profiles when passing the capture to the RTSP server.

- The `.gitignore` rules cover the CMake, Ninja and CTest artifacts and the local Docker Compose settings.
- The dependencies of the development image are pinned through a fixed snapshot of the Debian repositories.
- Domain time projections and recording intervals are created through checking C++ factories. Domain tests run on GoogleTest through CTest, and the test environment includes GoogleMock.

### Fixed

- Frames dropped after the block limit until the next keyframe are marked on the timeline as a gap with the reason "The camera rarely sends keyframes". Previously this loss did not enter the gap journal.

- A camera archive recovery download stopped by the time limit reports a partial result and continues from the end of the copied interval. Previously it reported a complete copy and skipped the rest of the window.

- In the common check of the offline installation, Bookworm and Trixie use their own images. The protocol records the operating system codename obtained from the booted virtual machine.

- During archive recovery, search and opening a media session keep the `404` response for a point unavailable to the user. A user with the viewing permission gets the archive unavailability state.

- When the workplace opens or reloads, the browser applies the selected theme before the first frame. The background is kept while the client loads and the session is checked.

- In the top menu on a phone the section icons are aligned on one line. The short labels "Journal" and "Maps" fit inside the buttons.

- In the mockup and the documentation, camera connection belongs to the site administrator role. Navigating between the mockup sections keeps the session role.

- The background check of the session and the list of permitted points keeps the idle timeout. When it expires, the open client returns to the sign-in form.
- The archive player applies the last selected position when the previous seek is still running.
