# Server acceleration qualification

[Русская версия](ru/server-acceleration-qualification.md)

Each run starts a private Compose project with PostgreSQL and a synthetic camera on an internal network. It copies the release binaries into the worker container, records their hashes and completes the node probe. Run one capacity series at a time on the same host. Each qualification run has a ten-minute time limit, including the node probe. Image builds and stand setup precede the run. The four inputs are H.264 Main and H.265 Main at 1920x1080 and 2560x1440, 25 fps, with 150 SMPTE test pictures, 8-bit 4:2:0, a 6 Mbit/s encoder target, a one-second GOP and no B-pictures. Fixtures and test credentials exist only inside the qualification container. Reports under `build/acceleration/qualification/<backend>-<phase>-<run>/` survive its removal.

| Phase | Command | Evidence |
| --- | --- | --- |
| Capacity | `task acceleration:qualify BACKEND=cpu PHASE=capacity` | Device inventory, completed probe, budget proposals, individual workers, median export and archive analysis speed |
| Faults | `task acceleration:qualify BACKEND=nvidia PHASE=faults` | Masked worker frame sequence, actual backend changes and continuity of the production recorder |
| Variable load | `task acceleration:qualify BACKEND=nvidia PHASE=soak` | Continuous Auto operation, per-phase memory samples, frame counts and device switches |

For GPU capacity select `BACKEND=nvidia` or `BACKEND=vaapi`. For VA-API set `BACURI_RENDER_DEVICE` and `BACURI_RENDER_GID` to the render node and its host group. A host without a verified GPU supports CPU only. Repeat the NVIDIA series on each model and driver. Keep the probe fingerprint with the measurements.

## Measurement and acceptance

- Each live worker decodes the input through the production live codec, applies a mask or produces a separate H.264 profile, and checks every output timestamp. Each counted worker must deliver all 150 pictures. After the first two seconds of input, every picture must arrive within one second of its input time. Input clocks in a cohort must start within one second. Each worker also reports time to first picture and maximum delay including startup. `BACURI_DENSITY_LIMIT` sets the maximum cohort size, eight by default. A passing result at the ceiling is a lower bound. The per-worker reports identify GPU execution and CPU overflow.
- Export selects and masks all 150 pictures. Archive analysis includes the keyframe, thumbnail and motion index. Frame counts are checked. Throughput is the median of three fresh processes in frames per wall-clock second, including startup, finalization and output. The input is unpaced. `manual` selects only the named task. Archive subtasks can run on different devices. The full probe and the placement preview preserve the session, time and memory budgets.
- During faults an independent production recorder writes the original synthetic RTSP input while a paced 1080p worker transforms video. Verify every recorded block has 25 samples, source timestamps have no gap longer than half a frame and the catalog reports zero gaps. The scenarios distinguish a reported driver reset, device visibility removed for a new worker, a rejected CUDA allocation larger than physical memory followed by a worker error report, and actual hardware encoder exhaustion. Physical driver reset, hot removal and pressure from allocations consuming the device's available VRAM require a dedicated device.
- The Auto test keeps one masked H.264 1080p worker alive for 120 seconds by default (3000 frames). The load alternates every 15 seconds between that worker alone and an additional 2K HEVC live profile with an export or archive analysis. The RSS sample sums Core, Media and the persistent worker. VRAM uses the same processes, with a separate reading for the masked worker. NVIDIA reports per-process framebuffer usage, and VA-API uses resident local memory where DRM exposes it. Comparable idle samples after warm-up determine growth, using the median of the first and last three samples. Telemetry loss, missing frames, missed delivery deadlines or an exceeded limit prevent a passing report.
- The local working limits are 128 MiB growth in RSS, 128 MiB growth in VRAM and zero backend changes in the permanent masked stream. Each added profile may move from CPU to GPU once after an earlier background job releases its grant. The default total switch limit is one per busy period (four in the reference run). Set `BACURI_RSS_GROWTH_MIB`, `BACURI_VRAM_GROWTH_MIB` and `BACURI_SWITCH_LIMIT` from the node passport before the run. Set `BACURI_SOAK_SECONDS` to request 48-600 seconds of video. The runner shortens the video interval if needed to fit the probe and verification inside the ten-minute limit. The report records both the requested and actual intervals. Read `status.json` for elapsed time and completion. A stop or restart requires a fresh continuous run.

## Reference node

AMD Ryzen 9 3900X (12 cores, 24 threads), Linux 7.0.0-44-generic. The CPU profile runs in a container without GPU devices. The GPU profile uses the same host with GeForce RTX 4070 Ti and driver 595.99.02. The probe verified 12 H.264 and 12 HEVC encoder sessions separately. These codecs share the physical encoder limit. A server-class NVIDIA GPU and a usable VA-API render node have no measurements on this stand.

| CPU-only input | Simultaneous masks | Simultaneous profiles | Masked export, fps | Archive analysis, fps |
| --- | ---: | ---: | ---: | ---: |
| H.264 1080p | ≥8 | ≥8 | 230.02 | 130.27 |
| H.265 1080p | ≥8 | ≥8 | 212.63 | 112.43 |
| H.264 2K | ≥8 | ≥8 | 160.94 | 79.64 |
| H.265 2K | ≥8 | ≥8 | 140.48 | 73.45 |

Each density column was measured in a separate series. The CPU node probe reported one verified 25 fps reference stream per codec and direction. CPU-only mode has no hardware encoder or VRAM reservation.

The GPU capacity run reported 11.56 GiB of device memory and a budget of 1,000,000 time units per second. A reference mask or live profile costs 40.78% of that budget and one H.264 encoder slot. The Auto preview for the reference task mix reserved two encoder slots, 91.91% of the time budget and 4.30 GiB. Runtime placements depend on the tasks actually running.

The fault run delivered all 5500 masked pictures over 220 seconds. After startup the largest frame delay was 360 ms. The recorder published 222 consecutive GOPs (5550 pictures) across the test window, including the boundary blocks, with zero catalog gaps. The reported reset, a worker with CUDA hidden and the oversized allocation plus its error report each produced GPU -> CPU -> GPU. With all 12 NVENC sessions occupied, a new worker completed 150 pictures on CPU and the existing masked stream kept its encoder. The node journal recorded `session_limit`.

## Modes on the RTX 4070 Ti

Density columns show the final GPU + CPU worker counts in the largest passing cohort (test ceiling eight). Export and analysis ran individually. In manual mode only the measured task kind was selected for hardware acceleration.

| Input | Mode | Masks, GPU + CPU | Profiles, GPU + CPU | Export, fps | Archive analysis, fps |
| --- | --- | ---: | ---: | ---: | ---: |
| H.264 1080p | CPU only | 0 + 8 | 0 + 8 | 209.62 | 129.84 |
| H.264 1080p | Auto | 2 + 6 | 2 + 6 | 117.69 | 73.14 |
| H.264 1080p | Manual | 2 + 6 | 2 + 6 | 117.31 | 82.06 |
| H.265 1080p | CPU only | 0 + 8 | 0 + 8 | 187.36 | 108.48 |
| H.265 1080p | Auto | 2 + 6 | 2 + 6 | 112.29 | 70.71 |
| H.265 1080p | Manual | 2 + 6 | 2 + 6 | 114.35 | 78.75 |
| H.264 2K | CPU only | 0 + 8 | 0 + 8 | 149.79 | 80.21 |
| H.264 2K | Auto | 2 + 6 | 2 + 6 | 99.31 | 54.48 |
| H.264 2K | Manual | 2 + 6 | 2 + 6 | 98.15 | 60.28 |
| H.265 2K | CPU only | 0 + 8 | 0 + 8 | 142.39 | 72.74 |
| H.265 2K | Auto | 2 + 6 | 2 + 6 | 98.13 | 52.38 |
| H.265 2K | Manual | 2 + 6 | 2 + 6 | 95.91 | 57.06 |

Across the passing eight-worker cohorts the time to first picture was 120-160 ms for CPU workers and 1080-1280 ms for NVIDIA workers. After two seconds of input the largest measured frame delays were 84 ms on CPU and 81 ms on NVIDIA. In the six-second export and archive analysis jobs, CPU-only mode finished faster for every input. These timings include creating a process and its codec context for each job.

The 120-second variable-load run delivered 3000 masked pictures through four busy periods. The masked worker stayed on the RTX. Two secondary profiles initially used CPU and then acquired a GPU grant. Comparable idle RSS grew by 0.13 MiB and VRAM by 0 MiB.
