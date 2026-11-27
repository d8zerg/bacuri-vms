# ![Bacuri mark](design/bacuri/brand/icon-48.png) Bacuri VMS

[Русская версия](README.ru.md)

**An open video management system that runs autonomously on site.**

Bacuri VMS records cameras on the site's own servers, shows live video and the archive in a browser and keeps working without internet access or a cloud service. An operator finds a camera on a floor plan, goes from an alarm to the exact frame and keeps an episode as signed evidence. An administrator connects cameras, assigns recording and grants access to exactly the points a person needs.

The project is free software under the [MIT license](LICENSE): no per-camera licenses, no cloud dependency and no lock-in to a camera vendor.

## Principles

| Principle | What it means in practice |
| --- | --- |
| Openness | Public REST, WebSocket and gRPC contracts, a documented archive and export format, an error catalog with stable codes, no vendor lock-in |
| Operator speed | Live video, the alarm feed and the archive are one click apart, the interface works from the keyboard, every state has a visible cause |
| Archive truth | The archive timeline separates recordings, gaps, estimated time and cataloging. Exports are signed and verifiable on a machine without the system |
| Autonomy | The site owns its cameras, data, permissions and recovery. Local sign-in and recording keep working when external services are unavailable |

[Product principles](docs/product-principles.md)

## Features

- **Cameras.** ONVIF discovery, including unicast discovery behind a router, RTSP connection, batch configuration of channels against a site template with revisions and rollback, camera replacement that keeps the archive of the point.
- **Live video and control.** WebRTC live video with the actual profile, codec and resolution of each cell. Pan, tilt, zoom, focus, presets, patrols, illuminator, wiper, relays and the camera inputs within the ONVIF capabilities. Separate permissions for listening, recording sound and speech.
- **Recording and storage.** Continuous, scheduled, event and manual recording with pre-event and post-event intervals for cameras and groups. H.264 and HEVC Main are recorded without re-encoding. Retention, a volume reserve, a verifiable second copy of the archive and recovery of gaps from the camera's own storage.
- **Alarms.** A live feed with resume after a break, handling with one owner per alarm, limited delegation, a journal, video rules with zones, lines and schedules tested on a recording before they are enabled.
- **Investigations.** Search by motion in an area of the frame and by events, synchronized playback of four cameras, bookmarks, cases with holds that protect intervals from cleanup, export of the original or a masked derivative with an offline signature check.
- **Maps and plans.** Offline region maps, floor plans with georeference, camera placement and calibrated view sectors.
- **Access.** Local accounts, administrator sign-in with a WebAuthn key, optional corporate sign-in through OpenID Connect, point groups, privacy masks and an access audit.
- **Workspace.** One web client for operators and administrators with light and dark themes and an English and Russian interface.

## Architecture

A site runs a modular control core and separate processes for recording, video delivery and observation intake. The processes share contracts and can be placed on one node or distributed across a cluster.

| Component | Technology | Responsibility |
| --- | --- | --- |
| Control core | C++23, Boost.Asio and Boost.Beast, PostgreSQL | Resource catalog, permissions, policies, alarms, REST API and WebSocket feed |
| Recorder | C++23, GStreamer, SQLite journal | Stream ingest and the archive on the local volume, independent of the core |
| Media delivery | C++23, GStreamer WebRTC | Live video and archive playback within a permission granted by the core |
| Observation source | C++23, gRPC | Motion and camera events to the core |
| Web client | TypeScript, React, Vite, MapLibre GL JS | Operator and administrator workspace |

Services talk over gRPC inside the site. Clients use the public HTTPS API. [Architecture](docs/architecture.md) · [Domain model](docs/domain-model.md) · [Archive and export format](docs/archive-format.md) · [Architecture decisions](docs/adr/)

## Installation

A signed offline kit installs the system on Debian Bookworm or Trixie on amd64. The node needs no network repository: PostgreSQL and all components are part of the kit.

[Installation and first sign-in](docs/install.md) · [Maintenance and upgrade](docs/maintenance.md) · [Recovery](docs/recovery.md) · [Web interface guide](docs/ui.md)

## Development

The development environment needs Linux x86-64, the native Docker Engine with the Compose plugin and [Go Task](https://taskfile.dev/docs/installation/). All builds and checks run in containers.

```sh
task stand:up       # PostgreSQL and a synthetic RTSP camera
task control:up     # core, recorder and media delivery with the migrations applied
task test           # build and run the C++ tests
task check          # the full quality gate: builds, sanitizers, static analysis, contracts and stand scenarios
```

Every product module keeps at least 75% line coverage, and the contracts, documentation links and error catalog are checked automatically. [Local stand and commands](docs/local-development.md) · [Code quality](docs/quality.md) · [Test profile](docs/test-profile.md)

### Contracts

| Contract | Source |
| --- | --- |
| REST API | [openapi.yaml](contracts/v1/openapi.yaml) |
| Alarm feed over WebSocket | [alerts.schema.yaml](contracts/v1/alerts.schema.yaml) |
| Service gRPC | [control.proto](contracts/v1/control.proto) |
| Error codes | [errors.yaml](contracts/v1/errors.yaml) · [Error reference](docs/errors.md) |

## In development

- **Desktop workstation.** A Qt client for multi-monitor posts, a video wall, a physical PTZ console and offline export verification. [Screen mockups](docs/desktop-ui.md)
- **Server video analytics.** A separate OpenCV service for object tracking, lines and zones, camera tampering checks and automatic anonymization of exports. [ADR-021](docs/adr/021-video-analytics.md)
- **Bacuri Manager.** Management of several sites with shared policies and search. [Federation](docs/architecture.md#8-federation-and-external-integrations)

## Contributing and security

Contributions are welcome. Read the [contribution guide](CONTRIBUTING.md) before opening a pull request. Report vulnerabilities as described in the [security policy](SECURITY.md).

[Changelog](CHANGELOG.md) · [MIT license](LICENSE)
