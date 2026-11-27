# Server task costs

[Русская версия](ru/server-acceleration-costs.md)

This registry gives the work unit, resource demand and preemption rule for each task in [ADR-025](adr/025-server-acceleration.md). Use the row for the input codec, size, workload and execution path. The [CPU measurements](../tests/acceleration/cpu.csv) and [NVIDIA measurements](../tests/acceleration/nvidia.csv) cover H.264 Main and H.265 Main, 8-bit 4:2:0, each at 1920x1080 and 2560x1440 (2K), 25 fps. They describe one reference node.

## Task registry

| Task and plan step | Unit and measured workload | Priority and action when preempted |
| --- | --- | --- |
| Live privacy masks, [VMS 9.4](devplan.md#шаг-94-профили-эфира-и-многоканальное-рабочее-место) | One masked output stream at source size, `mask`. Decode, apply the server's opaque rectangle to I420, encode H.264. | Live video, highest. Continue on CPU with the same masks. Suppress any frame whose mask could not be applied. |
| Web, mobile and video wall live profiles, [VMS 9.4](devplan.md#шаг-94-профили-эфира-и-многоканальное-рабочее-место) | One distinct encoded rendition. Use `transcode` at input size or `scale720` for 1280x720, both H.264. A compatible camera stream forwarded as received consumes zero video-engine sessions. | Live video, highest. Continue the rendition on CPU. |
| Masked export and format conversion, [VMS 7.4](devplan.md#шаг-74-экспорт-и-проверка-фрагмента) and [10.5](devplan.md#шаг-105-материалы-расследования-и-экспорт) | One active job with one track. Use `export-mask` or `export-transcode` for the video work at fixed quantizer 16. Multiply processing time by the number of frames. Apply the cost separately to concurrent tracks. A container-only copy requires zero video-engine sessions. | Background. Yield the device, then resume from a checkpoint on CPU if budget is available, otherwise queue. Preserve masks and frame timestamps. |
| Archive keyframe extraction, [VMS 10.4](devplan.md#шаг-104-поиск-и-синхронное-воспроизведение) | One decoded full-size random-access frame, `keyframe`. The reference reads one keyframe per second from the compressed track. | Background. Queue remaining requests and release the decoder. |
| Archive thumbnails, [VMS 10.4](devplan.md#шаг-104-поиск-и-синхронное-воспроизведение) | One 64x48 grayscale image from a keyframe, `thumbnail`. The reference includes decoding and resizing. | Background. Resume on CPU when budget permits or queue. |
| Live analysis, [analytics 1.1](devplan-analytics.md#шаг-11-живой-анализ-и-интерфейс-анализатора) and [1.4](devplan-analytics.md#шаг-14-исполнитель-моделей) | One input stream, `motion`. Decode once, scale to 64x48 and run the server motion sample. Add the cost of each enabled analyzer or model beyond this baseline. | Below live video, above background jobs. Continue on CPU and apply the configured sampling rate and overload policy. |
| Archive analysis, [analytics 1.3](devplan-analytics.md#шаг-13-очередь-заданий-повторный-анализ-и-проверка-правила-на-архиве) and [1.4](devplan-analytics.md#шаг-14-исполнитель-моделей) | One job on one track. Use `motion` for the same analysis of the recorded frames, plus each requested analyzer or inference. | Background. Release the device at a checkpoint and resume on CPU or queue at the next unprocessed timestamp. |

Recording the original camera stream uses zero decode and encode sessions. Charge shared decoding or an already generated rendition once per actual pipeline. Separate encoder outputs each consume an encoder session. The sampled RSS and VRAM include process and driver overhead. Measure the incremental memory of a second task in the same worker before deriving multi-stream capacity.

## Reading the measurements

The reference node has an AMD Ryzen 9 3900X and a GeForce RTX 4070 Ti with driver 595.99.02. Both paths use GStreamer 1.26.2. Each row is one isolated run per workload and input profile, including process startup. Queued jobs hold zero decoder and encoder sessions.

One masked live stream at 25 fps

| Input | CPU cores, software path | CPU cores, NVIDIA path | NVIDIA framebuffer, MiB |
| --- | ---: | ---: | ---: |
| H.264 Main 1920x1080 | 0.38 | 0.23 | 245 |
| H.265 Main 1920x1080 | 0.48 | 0.23 | 259 |
| H.264 Main 2560x1440 | 0.55 | 0.30 | 286 |
| H.265 Main 2560x1440 | 0.79 | 0.32 | 306 |

| Field | Unit and interpretation |
| --- | --- |
| `cpu_cores_at_25fps`, `peak_rss_mib` | User plus system CPU seconds divided by six seconds of source video, and peak resident process memory in MiB. These also cover CPU work on the NVIDIA path. |
| `dec_sessions`, `enc_sessions`, `sampled_vram_mib` | Decoder and encoder sessions required by the explicit hardware elements in the pipeline, and the highest per-process framebuffer memory reading in MiB. The node probe measures device limits separately. A short allocation between samples may exceed this value. |
| `dec_ms_per_input_frame`, `enc_ms_per_input_frame`, `sm_ms_per_input_frame` | Estimated busy time for the decoder, encoder and GPU compute engines separately. At 25 fps, multiply each mean utilization percentage by 0.4 ms. Engines can overlap. A reported zero can fall below the counter resolution. A blank indicates unavailable telemetry. |
| `input_frames`, `output_frames`, `motion_samples`, `wall_s`, `gpu_samples` | Input and verified output counts, motion observations, elapsed time and valid GPU readings. For a sparse output, multiply per-input-frame time by `input_frames / output_frames` to obtain cost per delivered image. |

For example, a masked live stream reserves one decoder and one encoder on NVIDIA. For a minute of masked export at 25 fps, use the matching `export-mask` row for 1,500 frames with those two contexts held concurrently. For an off-keyframe seek, include every decoded frame from the preceding random-access point to the requested frame. File I/O, muxing, audio, transport and evidence signing require their own budgets. This paced fixture measures processing cost at 25 fps. Maximum export throughput, cold seek latency and concurrent capacity require separate runs.

For model inference, add calls per second times measured CPU seconds or GPU engine milliseconds per call. Add the peak workspace per concurrent call and the resident weights once per loaded model instance. Measure with the model hash, input tensor size, batch size, sampling rate, device, precision and backend fixed. Compare the same selected frames with inference disabled and enabled. Keep the per-model result separate for CPU, CUDA and OpenVINO. A model without a measurement has unknown additional cost and cannot be admitted against that backend's budget.

## Reproduce

```sh
task acceleration:cost BACKEND=cpu
task acceleration:cost BACKEND=nvidia
```

Run sequentially on an otherwise idle node. The CPU command also works on a host without a GPU. The NVIDIA command uses device 0 with video, compute and per-process utilization reporting. Each command checks all four generated Main-profile fMP4 inputs (150 frames, GOP 25, no B-frames), runs each workload in a fresh process and writes a complete report to `build/acceleration/<backend>.csv`. Fixture generation is outside the measurement.

The deterministic workload uses SMPTE test bars, a 6 Mbit/s encoder target for the input and a 2 Mbit/s H.264 encoder target for live output. The export encoder uses fixed quantizer 16. The privacy mask covers the central quarter of the image. Full-rate pipelines must return 150 frames. Keyframes and thumbnails must return six images. Motion must return 150 frames and 22 samples using the server's 250 ms sampling interval. The NVIDIA reference uses NVDEC and, for video outputs, NVENC. The native-size unmasked transcode passes CUDA surfaces directly. Masking, resizing and motion run on the CPU in this reference. Per-process NVIDIA counters are sampled at one-second intervals. Repeat with the intended transformations, driver and recordings before assigning a node budget.

## Background node check

A newly started media node queues a check. It samples one capability at a time in a low-priority subprocess with a 12-second deadline. A live delivery session or archive read interrupts the active sample. The check waits and retries that sample after foreground work releases the node. Recording continues in the recorder. NVIDIA video samples also wait when the driver reports active encoder sessions or decoder use. Progress can remain in `waiting` on a busy node.

The site administrator uses the following operations under `/api/v1/sites/{siteId}/nodes/{nodeId}/acceleration/probe`. Writes use the session CSRF token.

| Request | Result |
| --- | --- |
| `GET` | Current job ID, reason, state, completed/total samples per stage, last saved result and its `stale` flag. |
| `POST` | Queue a check or return the existing active job. Use the optional body `{"reason":"prepare_gpu"}` when preparing to leave CPU-only mode. |
| `DELETE /{probeId}` | Cancel that job. The previously saved measurements and current execution mode remain available. |

The stages cover inventory, decoding, encoding, inference and representative workloads. Each supported video backend processes a synthetic H.264 Main or H.265 Main 1920x1080 reference at 25 fps. Sixteen input frames exercise masking, resizing to 720p, masked export and motion analysis for live or recorded video. Keyframe and thumbnail extraction use the random-access frame. The result identifies the actual codec elements, elapsed microseconds, frame count and observed throughput. A 25 fps single-stream budget is 0 or 1. Timing includes pipeline startup. Encode and decode use the median of three independent pipeline timings and return all three in `elapsed_samples_us`. Inference runs 32 passes on 256 FP32 values and checks every ReLU output on CPU, CUDA and each available OpenVINO target. Production model costs use the separate measurement procedure above.

The encoder test holds simultaneous hardware sessions up to a ceiling of 16 per codec. `verified_sessions` is the number actually opened together. If `reached_ceiling` is true, this is a lower bound. On the reference RTX 4070 Ti, both runs held 12 H.264 sessions and 12 H.265 sessions when each codec was tested separately. A device with shared or unreported video memory returns `memory_bytes: null`. Failed, timed-out and unsupported capabilities have their own states. The check publishes the result atomically once all stages and the required CPU references finish. It stores the device model, driver, hardware fingerprint and UTC measurement time on the media node. It checks device and driver identity every 15 seconds and queues a replacement when they change. Clients can still read the prior result with `stale: true` during that check.

Run the qualification on each otherwise idle installation. On two consecutive runs the verified hardware session counts and single-stream decode/encode budgets must match. Raw throughput may vary. The CPU-only configuration works without vendor libraries. For NVIDIA, use the NVIDIA Container Toolkit and the NVIDIA Compose overlay. For VA-API, grant the media service access to the render device and install the matching user-space driver in its runtime. The VA-API Compose overlay includes Mesa and Intel user-space drivers and accepts the device and its numeric host group. OpenVINO is optional. The media process loads `libopenvino_c.so` or the installed library named by `BACURI_OPENVINO_LIBRARY`.

```sh
task acceleration:check BACKEND=cpu
task acceleration:check BACKEND=nvidia
BACURI_RENDER_DEVICE=/dev/dri/renderD128 BACURI_RENDER_GID=$(stat -c %g /dev/dri/renderD128) task acceleration:check BACKEND=vaapi
task live:check
```

## Node acceleration settings

Each site node starts in `cpu_only` at revision 0. The site stores the selected mode and the manual task list in PostgreSQL. Site members can read the settings. The site administrator can preview a candidate and apply it. The following requests use the base path `/api/v1/sites/{siteId}/nodes/{nodeId}/acceleration`.

| Request | Effect |
| --- | --- |
| `GET` | Read the saved mode, manual selection, revision and expected assignment and reason for each task. The `ETag` contains the quoted configuration revision. |
| `POST /preview` | Evaluate a candidate. Returns the proposed assignments, device reservations and any `rejected_tasks`. The saved revision stays available for subsequent edits. |
| `PUT` | Evaluate the candidate against the current hardware and save it if it fits. Send the `ETag` as `If-Match`. A concurrent edit returns HTTP 412. |

For example, send `{"mode":"manual","gpu_tasks":["live_mask","export"]}` to preview those two task kinds. A manual selection that exceeds capacity or includes an unsupported task returns `applicable: false` in the preview. Applying it returns HTTP 409 with `acceleration_budget_exceeded` and the rejected task names. In `auto`, send only `{"mode":"auto"}`. Task assignments are computed by the node. A return to `cpu_only` is available even when the media service is offline. Each effective change and its audit entry commit together. The site journal records the node, initiator, revision and both mode and task lists before and after the change.

GPU modes require a completed measurement matching the current device and driver. The server returns `acceleration_probe_required`, `acceleration_probe_stale` or `acceleration_device_unavailable` for an absent measurement, changed hardware or a CPU-only inventory, respectively. Leaving CPU-only queues a probe or joins the current one. A fresh completed measurement permits immediate application. A preview with a missing or stale result also requests a probe when preparing a GPU mode from CPU-only. Read its progress at `/probe` and repeat the preview after completion.

The configuration budget uses one 1080p reference unit per task kind. Full-rate tasks reserve 25 frames per second. Keyframes and thumbnails reserve one image per second. The cost uses the slower of the two measured input codecs, including decoder throughput, H.264 encoder throughput where required, and the end-to-end task timing. One million time units per second is a full device budget. All H.264 outputs share the verified encoder session count. The sum of measured device memory reservations must fit dedicated memory. The sample includes device memory already in use while the reference pipeline runs. With shared memory, missing telemetry or an older sample without memory readings, only one measured workload is admitted on that device. The two analysis kinds reserve their own baseline video processing costs. Model-specific inference and 2K capacity require their own measurements.

When several GPUs are available, the server checks placements across them and chooses the lowest total measured time for the same set of tasks. Live masks and profiles have first priority, followed by live analysis, export, archive analysis, keyframes and thumbnails. A task that cannot fit gets an expected CPU assignment with the reason `unsupported` or `budget_exceeded`. The assignments describe the configuration budget.

## Runtime scheduling

The media service reads the committed policy from Core over the local authorization socket once per second. It uses CPU while the policy is unavailable or until its startup probe confirms the hardware. The scheduler uses the reference budget above for each active worker instance. Independent pipelines of the same kind consume independent shares. Equal-priority tasks keep their arrival order. An eligible running task keeps its GPU. For a new task the scheduler prefers capacity already free, then the previewed placement in manual mode, then lower measured GPU time.

A worker serializes its updates and polls the private `NodeAcceleration.ScheduleTask` RPC with a unique execution UUID and task kind. It starts a GPU pipeline only with a device ID and grant. Renew at least once per second and stop GPU use within four seconds of the **start** of the last successful GPU request. On a CPU or queue reply, close the GPU pipeline, then acknowledge its grant with `RELEASED`. The scheduler keeps charging that grant until acknowledgement or expiry. A background worker without CPU capacity may set `wait_for_gpu`. Live tasks always have a CPU path. Report `FINISHED` after closing the pipeline. A previous grant cannot release a newer one. Workers keep their own media timestamps and job checkpoints.

| Worker report | Node action |
| --- | --- |
| `SESSION_LIMIT` | Reduce encoder capacity to the other reserved sessions and reassign by priority. A new live stream can take a session from an export after the export releases it. Decoding remains available. |
| `DEVICE_LOST`, `DRIVER_RESET`, `ENCODER_ERROR` | Quarantine the affected device, withdraw its grants and emit a warning. Other GPUs keep serving work. After the grants drain, an isolated worker verifies the failed workload on both H.264 and H.265 input before readmission. |
| Successful recovery | Retry checks at intervals of at least 30 seconds. On an idle device verify the encoder capacity. At the reduced encoder limit, test one additional session alongside the existing ones. Within a workload check a busy sample can be retried for up to three seconds while the utilization reading settles between codecs. A failed check keeps the restriction. |

Forced fallback takes effect at the next worker poll. After eviction a task spends at least 30 seconds off the GPU before promotion. A failed encoder open can be retried as soon as a lower-priority worker releases its session. A restarted scheduler waits out old leases and verifies the hardware again. Recording of the original stream runs independently.

The site administrator can read `scheduler` in `GET /acceleration/probe` at the node path. It contains task counts, device reservations, active warnings and the last 64 decisions. The media service also emits structured events to its process journal. Prometheus exposes `bacuri_acceleration_tasks`, `bacuri_acceleration_encoder_sessions`, `bacuri_acceleration_memory_bytes`, `bacuri_acceleration_time_ppm`, `bacuri_acceleration_device_warning` and `bacuri_acceleration_transitions_total`. The device gauges measure reservations, including grants awaiting release. Transition counters group paths and reasons without per-task labels. The media service samples physical device telemetry every ten seconds in a separate process. Supported counters appear as `bacuri_acceleration_observed_encoder_sessions`, `bacuri_acceleration_observed_memory_bytes` and `bacuri_acceleration_engine_busy_ratio` per device and engine.

Run `task acceleration:check BACKEND=nvidia` to exercise the worker RPC under a full measured live budget and a worker-reported driver reset. On a consumer NVIDIA device with a measured session ceiling, the fixture also fills the physical NVENC sessions, verifies the next open fails, and retries live encoding after export releases its session. The fixture records consecutive one-second camera segments and processes the same frames with the server privacy mask on the assigned backend. Production pipeline integration and its end-to-end qualification are tracked in [task integration](devplan-acceleration.md#шаг-22-задачи-узла-на-выбранном-устройстве).
