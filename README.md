# Bacuri VMS

[Русская версия](README.ru.md)

![Bacuri mark](design/bacuri/brand/icon-48.png)

Bacuri VMS is an open video management system that runs autonomously on site. Recordings stay on the site. In a browser, an operator finds a camera on a floor plan, opens live video, goes from an alarm to the exact frame and preserves an episode for an investigation.

The system is free software under the MIT license: no per-camera licenses, no cloud dependency and no lock-in to a camera vendor.

## Features

- **Cameras.** ONVIF discovery, RTSP connection, configuration of camera groups, pan, tilt, zoom and audio control.
- **Recording and archive.** Continuous, scheduled and event recording. The archive timeline shows available intervals and gaps with their causes.
- **Investigations.** Search by motion and events, synchronized playback of several cameras, protection of fragments from deletion and exports with a verifiable signature.
- **Access.** Local accounts, administrator sign-in with a WebAuthn key and permissions for cameras, archive and exports.

## Installation and operation

A signed offline bundle for Debian installs the system on site. The guides are in Russian and are being translated. [Installation and first sign-in](docs/install.md) · [Maintenance and recovery](docs/maintenance.md) · [Web interface guide](docs/ui.md)

## For developers

The local stand runs in Docker Compose and includes a synthetic RTSP camera. [Contributing](CONTRIBUTING.md) · [Stand commands](docs/local-development.md#preparation-and-commands) · [Architecture](docs/architecture.md) · [Product principles](docs/product-principles.md) · [Archive and export format](docs/archive-format.md) · [API](contracts/v1/openapi.yaml) · [Security policy](SECURITY.md)

<a name="сервер-управления"></a>
<a name="rtsp-эмулятор-с-веб-камерой"></a>

[Server setup](docs/local-development.md#control-server) · [Connecting a webcam to the stand](docs/local-development.md#rtsp-emulator-with-a-webcam)

<a name="запись-архива"></a>
<a name="поиск-и-воспроизведение-архива"></a>
<a name="прямой-эфир"></a>
<a name="тревоги-и-переход-к-записи"></a>

[Recording](docs/local-development.md#archive-recording) · [Search and playback](docs/local-development.md#archive-search-and-playback) · [Live video](docs/local-development.md#live-video) · [Alarms](docs/local-development.md#alarms-and-the-transition-to-the-recording)

[Changelog](CHANGELOG.md) · [Bacuri Manager](docs/architecture.md#8-federation-and-external-integrations) · [MIT license](LICENSE)
