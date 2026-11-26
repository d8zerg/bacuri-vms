# ADR-022. Video and map path of the desktop client

[Русская версия](../ru/adr/022-desktop-video-path.md)

**Status** - Accepted for Linux. Windows and macOS are decided on their own stations.

**Context.** The desktop client shows live video and the archive of up to 16 cameras on one screen next to a map. The frame has to reach the screen without copies through system memory, the interface has to stay responsive under load, and the workstation has to keep working when the GPU or its decoder fails. Qt Quick can draw through several graphics APIs, GStreamer offers hardware and software decoders, and MapLibre Native for Qt offers a Qt Location plugin and a core library.

**Options.**

| Option | Assessment |
| --- | --- |
| Decoding to system memory and uploading every frame | Works everywhere, but costs one copy per frame and 4 to 6 CPU cores for 16 cameras in 1080p |
| Hardware decoding into OpenGL memory and `qml6glsink` | No copies through system memory, 2.4 CPU cores for 16 cameras. Needs the OpenGL graphics API of Qt Quick |
| Own RHI node importing decoder surfaces | Works with every graphics API, but needs the import of decoder memory per platform before the first useful screen |
| Map through the Qt Location plugin of MapLibre | Needs private Qt Location headers that Debian does not ship, and the plugin sources are LGPL or GPL |
| Map through the MapLibre core and an own Qt Quick item | BSD-2-Clause, no Qt Location dependency, the map renders into an OpenGL framebuffer of the same scene |

**Decision.** On Linux the client uses Qt Quick on OpenGL. Each cell decodes with NVDEC when it is available and passes the frame in OpenGL memory to `qml6glsink`. The map is drawn by an own `QQuickFramebufferObject` item on top of the MapLibre Native for Qt core, built from the pinned release without the Qt Location integration.

| Situation | Behavior |
| --- | --- |
| Hardware decoder available | NVDEC, frames stay in GPU memory |
| Hardware decoder missing at start | Software decoder, one upload per frame |
| Hardware decoder fails during playback | The cell rebuilds its pipeline with the software decoder and continues from the last shown frame. The window, the layout and the other cells keep working |
| No GPU | Software decoder and software OpenGL through Mesa. The post keeps working with a lower density |
| Graphics device lost while drawing | The client restarts in software drawing mode and restores the post: layouts, alarms, archive position and focus |

The supported density is stated per station from measured runs: on the reference station 16 cameras in 1080p at 25 frames per second take 2.4 CPU cores with NVDEC, the scene renders in at most 4.3 ms at p99, and the map adds 0.2 ms per frame.

**Rationale and consequences.** The OpenGL path gives zero copies on the reference station now, while an own RHI node would delay the first working screen by per-platform surface import work. The choice ties the client to the OpenGL graphics API of Qt Quick on Linux. Windows and macOS need their own decision: Direct3D 11 and Metal sinks exist in GStreamer, and the map item has to follow the chosen API. Each cell owns its decoder today, which makes the start of 16 hardware cells and a seek of all of them slower than with software decoding. A shared decoder context and a keyframe cache are needed for the synchronized archive. The map item keeps the client free of LGPL and GPL code. [Technology lifecycle](015-technology-lifecycle.md) · [Map sources](017-map-sources.md)

**Verification.** `task desktop:video` runs reference recordings in H.264 and H.265, live cameras, the map next to 16 cells, a decoder failure during playback and a station without GPU access. Every run checks the first frame, the frame after a seek or after the fallback, the decoder output memory and the absence of errors. `task desktop:passport` ties the results to the measured station.
