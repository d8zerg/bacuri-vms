# Bacuri VMS local stand

[Русская версия](ru/local-development.md)

## Preparation and commands

Linux x86-64, the native Docker Engine, the Docker Compose plugin and Go Task are required. [Installing Go Task](https://taskfile.dev/docs/installation/)

```sh
task check
```

The command builds the optimized services, checks the GCC and Clang Debug builds, runs CTest with ASan/UBSan and TSan, runs static analysis and checks the contracts and the TypeScript types. Then it starts the Compose stand, applies the storage schemas and checks recording, live video, the archive and alarms. CMake and Ninja run in a container without network. The results are kept in `build/`.

For a full run, use a working copy with the full Git history. In the `task check:report` report, open `coverage/cpp/index.html` and `workspace/coverage/index.html` to see the uncovered lines. `secrets/` contains separate Gitleaks results for the history, the index and the working files. [Mandatory code checks](quality.md#automatic-code-checks) The `coverage/cpp/modules.txt` report lists covered and total lines and the threshold result for each C++ module and target.

| Command | Action |
| --- | --- |
| `task release:package SUITE=bookworm SIGNING_HOME=... SIGNER=... OUTPUT=...` | Build an offline kit with the `bacuri-vms` Debian package and sign it with the supplier key. For Debian Trixie use `SUITE=trixie`. [Building the kit](test-profile.md#offline-installation-and-access) |
| `task install:check SUITE=bookworm` or `SUITE=trixie` | Install the signed kit on a clean Debian Bookworm or Trixie in KVM, reboot the node and check WebAuthn and the point permissions. The host needs `/dev/kvm`. [Offline installation](install.md) |
| `task maintenance:check PREVIOUS=/path/to/report` | Check a schema upgrade with data preserved, a repeated migration, a rollback and a recovery on a separate node with a preserved archive. Needs the directory of the signed previous kit with `bundle/` and `release-key.gpg`. [Node maintenance](maintenance.md) |
| `task build` | Build the C++ targets with the `gcc-debug` preset |
| `task build PRESET=clang-debug` | Build with the given CMake preset |
| `task release:check` | Build the optimized services, run `bacuri-vms-core --version` |
| `task build:repro` | Compare the Release executables byte for byte with a clean build in a separate directory of the same image |
| `task deps:lock` | Recompute the common Conan lockfile for all profiles after a dependency change |
| `task test` | Build and run CTest. Supports `PRESET` |
| `task module:test` | Build and run owner operation tests for the nine Core modules in a container without networking or PostgreSQL |
| `task module:unit-coverage` | Measure line coverage for each of the nine modules from these tests alone. The report is written to `unit/modules.txt` in the result directory |
| `task module:check MODULE=maps` | Build with Clang, check architecture and formatting, run GoogleTest and clang-tidy and enforce coverage for the selected module using GCC CTest and integration scenarios. Choose `access`, `events`, `export`, `inventory`, `investigations`, `maps`, `media_sessions`, `recordings` or `recovery` |
| `task lint` | Check C++ by the rules of [.clang-tidy](../.clang-tidy) |
| `task media:check:bookworm` | Check RTSP recording, WebRTC and alarm handling on the Debian Bookworm system libraries |
| `task postgres:performance` | Compare the results and plans of the queries to the map queue, the camera placements and the archive before and after the migration. [Method and measurements](postgresql-performance.md) |
| `task format:check` | Check the C++ format by `.clang-format`. A mismatch stops the check |
| `task secrets:check` | Run Gitleaks on the full Git history, the index and the working files, including new files outside the ignore rules |
| `task coverage:check` | Require at least 75% line coverage for each C++ module, production CMake target and web client module. Installation and recovery coverage requires KVM |
| `task control:up` | Start the core, recording and delivery of the archive and live video, apply the PostgreSQL migrations |
| `task live:check` | Check WebRTC in Chrome and Firefox, recording while a client slows down and delivery restarts, and the access revocation deadlines |
| `task recovery:check` | Measure the recovery of permissions, recording and the archive on an isolated stand with stopped services and media failures. [Guide](recovery.md) |
| `task playback:check` | Check the archive over HTTPS, access revocation, decoding and seeking in Google Chrome and Firefox |
| `task control:check` | Check sign-in, the registry, the application and the recovery of the configuration in an isolated database |
| `task api:check` | Check the schemas and examples of REST, gRPC and WebSocket |
| `task api:types` | Generate and check the TypeScript types in `build/contracts/api.d.ts` |
| `task docs:check` | Check the local links, the scenarios, the ADRs and the coverage of operations, profiles and test vectors in the full release register |
| `task ui-kit:check` | Build the UI Kit and Storybook, check the types, icons, accessibility, keyboard, screen references and TypeScript coverage in the browser |
| `task web:build` | Build the static client into `build/web-client` and a separate comparison build for the stand |
| `task workspace:check` | Check both screens against the reference, go through the scenarios over HTTPS in Chrome and Firefox, measure loading and the coverage of the source TypeScript |
| `task access:check` | On an isolated stand, check local and corporate sign-in, permissions, revocation during delivery and autonomous work with Keycloak disconnected |
| `task ui-kit:up` / `task ui-kit:down` | Start or stop the [local component catalog](http://127.0.0.1:6010) |
| `task check:report` | Run `task check` and keep the environment information, the log and the exit code in `build/reports/check-*/` |
| `task stand:up` | Start PostgreSQL and the test camera |
| `task camera:commissioning` | Check ONVIF discovery, camera replacement and playback of both binding revisions in the browser |
| `task stand:check` | Check PostgreSQL, the RTSP streams and the volumes |
| `task stand:check-storage` | Check that PostgreSQL and the archive survive a restart and the limit of the test volume in a separate temporary Compose project |
| `task schema:check` | Check the manifest and the initial PostgreSQL and SQLite migrations on temporary databases |
| `task stand:status` | Show the container state |
| `task stand:logs` | Show the latest PostgreSQL and camera logs |
| `task stand:down` | Stop the stand and keep the persistent volumes |
| `task shell` | Open a shell in the development container |

Inside the development container, build the maps library alone with `sh tools/build-preset gcc-debug --target bacuri-vms-maps`. To run its tests, build the `bacuri-vms-maps-test` target the same way, then run `ctest --test-dir build/gcc-debug/src/modules/maps --output-on-failure --no-tests=error`.

`task --list` lists the tasks. `task test PRESET=clang-tsan` runs CTest in a separate container without network. The container uses a test seccomp profile that allows the `personality` call with the `ADDR_NO_RANDOMIZE` flag for TSan initialization.

In the Compose network the RTSP addresses are `rtsp://rtsp-camera:8554/main` and `rtsp://rtsp-camera:8554/sub`. From the host, `127.0.0.1` and port `8554` are used. The host port can be overridden with `BACURI_RTSP_PORT`. PostgreSQL is reachable inside the Compose network with the test account of the stand. The archive volume and a limited volume for failure tests are mounted in the development container.

The content of the test fill volume is removed when the development container ends.

## Product version and release

The Bacuri VMS version is set by an annotated Git tag of the form `vMAJOR.MINOR.PATCH`. Pre-releases add `-alpha.N`, `-beta.N` or `-rc.N`. A published tag always points to the same commit. The public contract of the product includes the documented APIs, administration commands and exchange formats. [Semantic Versioning](https://semver.org/)

Commit messages are written in English in the form `type(scope): summary`. The most significant change since the previous tag determines the number.

| Change | Example message | Effect on the version |
| --- | --- | --- |
| Compatible fix | `fix(recorder): preserve timestamps after reconnect` | PATCH |
| Compatible functionality | `feat(api): expose camera health` | MINOR |
| Breaking the public contract | `feat(api)!: require a site identifier` | MAJOR. Instead of `!`, a final `BREAKING CHANGE: ...` paragraph can be added |
| Project maintenance | `docs(install): clarify archive permissions` | The `docs`, `test`, `ci`, `chore`, `build` and `refactor` types by themselves do not require a release |

`task release:next` reads the commits and suggests a tag. In a pre-release series the target number is computed from the last stable release. Before the first stable release the first tag is the starting point. A fix within a series increments the `N` counter. When a change requires a new target version, an `alpha.1` series opens. The maintainer makes the move to `beta`, `rc` and the stable release after checking the readiness. [Conventional Commits](https://www.conventionalcommits.org/en/v1.0.0/)

`task version` shows the version of the working copy. On a clean tag it equals the tag without the `v` prefix. Between tags a build gets a number from the count of commits on the main line of history since the tag, the short Git hash and, with local changes, the `dirty` mark. After a stable release such builds belong to the next PATCH version. `bacuri-vms --version` shows the installed version.

For a signed release, `task release:package` requires a clean working copy on a tag with the full history. The kit contains the product version and the full commit hash in `sbom.json`. The Debian package number uses `~` before a pre-release series so that APT upgrades it to the stable release. [Debian version ordering](https://www.debian.org/doc/debian-policy/ch-controlfields.html#version)

## Building and updating dependencies

When the image is prepared, Conan installs Boost (headers), nlohmann/json and, for tests, GoogleTest/GoogleMock. The recipe sets the versions, the common lockfile pins the recipe revisions. The profiles fix the architecture, C++, the compiler and the ABI of the standard library. Release builds the services, Debug includes the test targets. Two instrumented presets use the Clang Debug dependencies and apply the corresponding sanitizer to the project build. [Recipe](../conanfile.py) · [Lockfile](../conan.lock) · [CMake presets](../CMakePresets.json)

GStreamer with loadable plugins, GLib, OpenSSL, libsodium, the PostgreSQL and SQLite clients, gRPC, Protobuf and the generators come from one Debian snapshot. The Conan client is pinned by version and SHA-256, the base Docker images by digest. The network is needed when the image is prepared. Compilation uses the packages installed in the image and the CMake configurations generated by Conan. When the toolchain changes or an old build is upgraded, `task build` configures the selected preset again. A repeated run with the same dependencies reuses the objects already built by Ninja. [Dockerfile](../Dockerfile) · [ADR-010. Build dependencies](adr/010-build-dependencies.md)

To update a library, change the recipe and run `task deps:lock`. Check the changes of the direct and transitive dependencies in the lockfile, then run `task check:report` and `task build:repro`. On the Release files the common run also checks the path from an observation to a reproducible alarm frame in Chrome and Firefox. After a successful run the report in `build/reports/check-*/` contains the tool versions, the list of Debian packages, the Conan graph with the identifiers of the actual packages for each profile and the SHA-256 of the four Release files.

### Core regression check

`task regression:check` copies the sources and the Git history to a new directory, uses a separate Compose project with empty volumes, runs `task check:report`, the storage durability check and an independent Release build. `build/reports/regression-*/` keeps the revision, the hash of the source content, the scenario materials, the recovery results and the exit codes. For a working copy with changes its exact content is kept as well. A hardware run on a dedicated IP camera has its own protocol.

`task deps:check` compares the installed npm trees and the Conan graphs with the lock files, checks the runtime versions and scans the product, build and test images. The preparation needs the pinned images and the Trivy databases. For temporary images and databases use the on-disk file system of the build directory. The scan itself runs without network. To repeat the decision on a saved snapshot offline, use the command

```sh
python3 tools/lifecycle/policy.py "$report/snapshot" "$report/replay.json"
```

For a full run with the saved Trivy databases, run `BACURI_ADVISORY_SNAPSHOT="$report/snapshot" task check:report`. The scanner checks the pinned revisions and the checksums of the databases and checks the current images and dependencies again.

Here `report` is the dependency check directory. `blocking.json` lists the reasons for a refusal and the affected versions. For a new scan with the saved databases and already loaded images, run `python3 tools/lifecycle/scan.py --offline-from "$report/snapshot"`. A new scan applies the current freshness term of the information. The inputs and their SHA-256 are listed in `snapshot/inputs.json`. To refresh the maintenance information, run `task deps:refresh`. The command gets the revisions of both official databases and the maintenance information. After checking the changes, repeat the series. [Evidence content](test-profile.md#core-regression)

`task stand:check-postgres-upgrade` creates a separate volume with the Bacuri VMS schema, an archive catalog and a pending alarm in PostgreSQL. On a physical copy with the upgraded server it rebuilds the indexes of the application schemas, runs pg_amcheck and compares the query results. Then it creates a new full backup, checks it with pg_verifybackup, starts the restored server and repeats the comparison. [PostgreSQL upgrade notes](https://www.postgresql.org/docs/release/17.12/)

### Maintenance of system packages

`task packages:build` exports the Debian packages, the source archives and patches, the `.buildinfo` files, the checksums, Gitleaks and gosu to a separate report directory. The build, recording and PostgreSQL containers use the same Dockerfile stages. The local revisions of libxml2 and libsoup keep the ABI of the Debian Trixie branch. The libxml2 fix is taken from the stable Ubuntu branch on the same source version of the library. For libsoup two GNOME fixes are applied. [Ubuntu advisory](https://ubuntu.com/security/CVE-2026-6653) · [Content handling fix](https://gitlab.gnome.org/GNOME/libsoup/-/commit/b91bbd7d7888c85b17a8b33173caa806dff51681) · [Compression fix](https://gitlab.gnome.org/GNOME/libsoup/-/commit/dbd0417ea3a569135e9a9344b7812076b7b178f1)

The web server of the UI Kit catalog uses an image with a signed Brotli fix from the stable Alpine branch. The gate maps each Compose service to a checked image or an identical configuration of its build. For Gitleaks and gosu the hash of the installed utility and the Go version from the build metadata are checked.

On an update, check the version in the signed Debian archive. When the fix is already part of the supplier's package, return it to the images and remove the local rule. For a new backport, pin the version and the checksum of the source in the Dockerfile, update the list of fixes and versions in `packaging/maintenance/policy.json`, run `task packages:build`, then `task regression:check`. A mismatch of the patch, the library bytes or the source version makes the gate block the kit. [ADR-016](adr/016-system-backports.md)

## RTSP emulator with a webcam

`task webcam:up` opens `/dev/video0` through V4L2. One capture feeds two H.264 encoders. By default `rtsp://127.0.0.1:8555/main` and `rtsp://127.0.0.1:8555/sub` are available on the host. When registering in the core, use `rtsp://webcam:8554/main` and `rtsp://webcam:8554/sub`. The emulator runs in a separate Compose profile and can run together with the synthetic camera.

The device is selected with the `BACURI_WEBCAM_DEVICE` environment variable, the host port with `BACURI_WEBCAM_PORT`. For example, `BACURI_WEBCAM_DEVICE=/dev/video2 task webcam:up`. The container gets access to the selected device and its group. `task webcam:check` checks both profiles, stops reading the preview queue and interrupts the test TCP connections. After the work, run `task webcam:down` to release the camera. For an automatic run on a machine without a camera, `task ingest:check` is used.

After the configuration is applied, the recording process opens both profiles. `docker compose logs -f recorder` shows the `rtsp_ingest` events with the point identifier, the profile, the resolution, the number of frames and reconnections. The `gap_begin_us` and `gap_end_us` fields give the bounds of a loss on the UTC receive scale. An open gap has a `null` end. For each connected consumer the `queues` field contains the current and peak depth, the bytes and the number of dropped frames. [Compatibility matrix and intake rules](rtsp-compatibility.md)

## Control server

`task control:up` starts the API at `https://localhost:8443/api/v1`. The port is set with `BACURI_HTTPS_PORT`, the permitted camera networks with a list of CIDRs in `BACURI_CAMERA_NETWORKS`. HTTPS uses Boost.Asio, Boost.Beast and OpenSSL. The "Cameras" wizard gets the profile addresses through ONVIF or accepts addresses manually. The open streams of the synthetic stand require an explicit exception by IP. The trust, exception and segment connection settings are described in the guide. [Connecting cameras](rtsp-compatibility.md#connection-wizard)

At the first start the stand creates a local certificate and separate encryption keys of the core and the recording node. The public certificate can be obtained with `docker compose exec -T core cat /identity/tls.crt` and used to verify TLS. The volume of each key must be kept together with the corresponding database. The processes run under different UIDs and pass configuration snapshots through gRPC over a Unix socket. The node restores the last applied revision from the encrypted snapshot in SQLite.

To create an account, use `task control:create-user USERNAME=admin ROLE=administrator` and pass the password as one line on stdin. Before the first administrator sign-in, run `task control:enroll-admin USERNAME=admin` in the local console. Open `/cameras` over trusted HTTPS, enter the password and the code, then confirm the registration and the sign-in with the WebAuthn key. For an operator, give `ROLE=operator` and grant access to a registered point with `task control:grant-point USERNAME=operator POINT=<UUID>`. To sign in, the browser sends JSON to `POST /api/v1/session` with the current HTTPS Origin. Writing the configuration uses the session cookie and the CSRF token. [Contracts and access model](adr/001-api-contracts.md)

The administrator opens the "Access" section at `/access`. It configures the permissions for points and groups, the privacy policy and the optional corporate sign-in through Keycloak. [Access management](ui.md#access-management)

## Administrator and operator workplace

After `task control:up`, open the [Bacuri VMS workplace](https://localhost:8443/). The administrator has the "Cameras" section. Enter the name and the addresses of the main and secondary streams. After the configuration is applied, the client checks a fresh recording block and a frame of the secondary stream. Choose "Create a test recording", open the new fragment and confirm the result when the frame appears.

In "Monitoring" the operator selects a permitted point, watches live video and opens the recording from an alarm card. "Back to the selected alarm" returns the focus to the feed. The page address keeps the site, the point and the viewing context. For the test source the addresses inside Compose are `rtsp://rtsp-camera:8554/main` and `rtsp://rtsp-camera:8554/sub`. Accounts and permissions are granted with the commands from the "Control server" section. [Interface scenarios and verification protocol](ui.md)

## Archive recording

After `task control:up`, register a camera through the registry. The recording node stores the main H.264 stream without re-encoding in the `archive-data` named volume. Each `/archive/recorder/blocks/<UUID>` directory contains a self-contained fragmented MP4 with an index and `manifest.json`. The manifest binds the file to the point, the channel, the binding revision and a half-open UTC interval. [Format and commit order](domain-model.md#recording-and-reconciliation-on-the-volume)

`task archive:status` reads the journal of the running node. The `committed` field gives the number of locally committed blocks, `pending` the number of unacknowledged catalog changes, `missing`, `corrupt` and `rejected` the numbers of missing block directories, damaged blocks and registration refusals. `last_commit_utc_us` contains the time of the last local commit. The node keeps recording by the saved configuration when the core or PostgreSQL is unavailable.

`task archive:check` checks recording and decoding from the synthetic camera, exclusive volume ownership, recovery after a stopped process and a returned database, repeated registration, a manifest conflict, and missing and damaged media. The scenario uses a temporary database and a separate volume directory.

## Archive search and playback

After `task control:up`, open `https://localhost:8443/archive/`, sign in with a local account, choose the site, the point and the UTC bounds. Search shows a recording, a confirmed gap with its reason, a block waiting for registration and an interval with unknown coverage. "Play" opens the recording, and the "Position (UTC)" field lets you go to a frame. Search and opening are available for intervals of up to one hour. One browser can open up to 16 media sessions at a time.

The manifest includes whole H.264 blocks starting at a keyframe. When a position inside a block is selected, the player loads it and seeks to the required frame. When the interval starts or ends inside a block, widen the request to the block bounds to view this block. The session expires after five minutes or earlier when the browser session ends.

`bacuri-vms-media` reads the archive from a volume mounted read-only. The core checks the permissions in PostgreSQL and passes the fragments through the same HTTPS origin. `task playback:check` starts an isolated database and browsers in Compose and outputs the latency to the first displayed frame and the positioning error. The reference consists of colored frames with a known time, a resolution change and a one-second gap. [MSE segment format](https://www.w3.org/TR/mse-byte-stream-format-isobmff/)

## Alarms and the transition to the recording

After `task control:up`, open the [monitor](https://localhost:8443/monitor). The alarm feed shows the operator the last 100 alarms of the own points and is updated over WSS. Selecting an alarm opens the archive at the frame of the event. When the block is still being registered, the player checks the same interval again on "Retry".

The local test source creates a `motion` observation for an already configured point and binding revision. Put the point UUID instead of `POINT_UUID` and the current revision instead of `1`. Keep the message to send it again.

```sh
task observation:sample POINT=POINT_UUID REVISION=1 > motion.json
task observation:submit < motion.json
task observation:submit < motion.json
```

The first response is `REGISTRATION_STATE_COMMITTED`, the repeated one `REGISTRATION_STATE_REPLAYED`. The emulator and the core use the clock of one Linux host. To check the recording, wait for the archive block to be published. `task alerts:check` runs the full scenario with an isolated database, failures and two browsers.

## Live video

After `task control:up`, open the [monitor](https://localhost:8443/monitor), sign in and choose the observation point. The player starts live video, "Archive" switches to the recording. The operator needs the permission for the point. Leaving the point closes the media session. After a lost connection, press "Retry" to connect again.

The core negotiates WebRTC over HTTPS. The `bacuri-vms-media` process gets the secondary H.264 stream from the recording node through a Unix socket and sends it to the browser over DTLS-SRTP without re-encoding. On a Linux host the browser needs a direct UDP route to the container address in the `live-media` network. The stand profile uses local ICE candidates. Viewing requires WebRTC H.264 support. The test Firefox image contains OpenH264 with a checksum check.

`task live:check` outputs the latency to the first decoded frame, the number and duration of archive gaps and the time to the last video packet on revocation and expiry of access. The synthetic RTSP camera is used by default. To check with a webcam, run `task webcam:up`, then `task live:check CAMERA=webcam`. After the check, `task webcam:down` releases the device.
