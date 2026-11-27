# ADR-022. Video and map path of the desktop client

[Русская версия](../ru/adr/022-desktop-video-path.md)

**Status** - Accepted for Linux. The Windows and macOS video paths are evaluated on their respective workstations under the [additional platform support plan](../devplan-desktop-platforms.md).

**Context.** The desktop client shows live video and the archive of up to 16 cameras on one screen next to a map. The frame has to reach the screen without copies through system memory, the interface has to stay responsive under load, and the workstation has to keep working when the GPU or its decoder fails. Qt Quick can draw through several graphics APIs, GStreamer offers hardware and software decoders, and MapLibre Native for Qt offers a Qt Location plugin and a core library.

**Options.**

| Option | Assessment |
| --- | --- |
| Decoding to system memory and uploading every frame | Works everywhere, but costs one copy per frame and 4 to 6 CPU cores for 16 cameras in 1080p |
| Hardware decoding into OpenGL memory and `qml6glsink` | No copies through system memory, 2.4 CPU cores for 16 cameras. Needs the OpenGL graphics API of Qt Quick |
| Own RHI node importing decoder surfaces | Works with every graphics API, but needs the import of decoder memory per platform before the first useful screen |
| Map through the Qt Location plugin of MapLibre | Needs private Qt Location headers that Debian does not ship, and the plugin sources are LGPL or GPL |
| Map through the MapLibre core and an own Qt Quick item | BSD-2-Clause, no Qt Location dependency, the map renders into an OpenGL framebuffer of the same scene |

**Decision.** On Linux the client uses Qt Quick on OpenGL. Each cell decodes with NVDEC when it is available, then with VA-API, and passes the frame in OpenGL memory to `qml6glsink`. The pipelines of a process share the CUDA, VA and GL display contexts, so the device is initialized once. The cells of a grid play recorded video on a common timeline: one system clock and one base time for all pipelines, with prerolls and seeks on worker threads, at most four cells at a time. Live sources play as their frames arrive. The map is drawn by an own `QQuickFramebufferObject` item on top of the MapLibre Native for Qt core, built from the pinned release without the Qt Location integration. The map comes from the site: MapLibre loads the style and the tiles itself and cannot add the bearer token, so the client rewrites site URLs to a local gateway on `127.0.0.1` with a random secret path, and the gateway forwards each request to the site with the token and the trusted certificates of the site.

| Situation | Behavior |
| --- | --- |
| Hardware decoder available | NVDEC, frames stay in GPU memory |
| Hardware decoder missing at start | Software decoder, one upload per frame |
| Hardware decoder fails during playback | The cell rebuilds its pipeline with the software decoder and continues from the last shown frame. The window, the layout and the other cells keep working |
| No GPU | The client detects a software OpenGL renderer before the window opens, decodes in software and limits each cell to 640x360 at 15 frames per second. Drawing goes through Mesa. The post keeps working with a lower density |
| Graphics device lost while drawing | The client restarts in software drawing mode and restores the post: layouts, alarms, archive position and focus |

The supported density is stated per station from measured runs: on the reference station 16 cameras in 1080p at 25 frames per second take 2.4 CPU cores with NVDEC, the interface keeps the 60 Hz rate of the screen with a p99 frame interval of 17 to 20 ms in regular playback, and the map adds 0.2 ms per frame. A grid of 16 cells starts in 1.6 s and seeks in 0.9 s with the longest interface frame within 40 ms, and its cells stay within 2 frames of each other. Without a GPU 16 limited cells take 14 CPU cores with a p99 frame interval of 45 ms. In a container the window reaches the screen only with the DRI devices of the host.

**Rationale and consequences.** The OpenGL path gives zero copies on the reference station now, while an own RHI node would delay the first working screen by per-platform surface import work. The choice ties the client to the OpenGL graphics API of Qt Quick on Linux. Windows and macOS need their own decision: Direct3D 11 and Metal sinks exist in GStreamer, and the map item has to follow the chosen API. A flush of many hardware decoders at once holds the graphics driver and delays the interface frame by up to 0.45 s with 16 H.265 cells, so the grid limits the parallel steps to four at the cost of a slower seek. Each `qml6glsink` creates its own GL context, which bounds the start of a grid; a grid sink or scene graph node with one context and a keyframe cache for the synchronized archive remain to be done. The map item keeps the client free of LGPL and GPL code. [Technology lifecycle](015-technology-lifecycle.md) · [Map sources](017-map-sources.md)

**Verification.** `task desktop:video` runs reference recordings in H.264 and H.265, live cameras, the map next to 16 cells, a decoder failure during playback and a station without GPU access with 4 to 16 cells. Every run checks the first frame, the frame after a seek or after the fallback, the decoder output memory, the spread of shown frames between cells and the absence of errors. `task desktop:passport` ties the results to the measured station.
