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
