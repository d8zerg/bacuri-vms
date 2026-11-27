# ADR-025. Choice of acceleration for server tasks

[Русская версия](../ru/adr/025-server-acceleration.md)

**Status** - Accepted.

[Server task costs and measurement procedure](../server-acceleration-costs.md)

**Context.** A node of the site may have no GPU, an NVIDIA GPU with NVDEC and NVENC, an Intel or AMD GPU with VA-API, or an accelerator for neural network inference through CUDA or OpenVINO. Recording writes the camera stream without re-encoding and needs no decoding. Other tasks decode, draw or encode video: privacy masks in live video, live profiles for the web, mobile and wall clients, export, keyframes and thumbnails, and the analysis of [ADR-021](021-video-analytics.md). Consumer GPUs limit the number of encoder sessions, and video memory is shared by every task of the node. A fixed choice of the accelerator either leaves the GPU idle or lets a background task take the encoder from live viewing.

**Options.**

| Option | Assessment |
| --- | --- |
| Everything on the CPU | Predictable, but masks, transcoding and analysis of many cameras need several times more cores |
| One global switch for the node | Simple, but either all tasks fall back to the CPU when the GPU is busy, or a background task competes with live viewing |
| Automatic choice per task with a budget of the device | Each task uses the GPU while its share of the budget is free and moves to the CPU otherwise. Needs a check of the device, a budget and a visible reason for every choice |
| Choice per task by the administrator | The administrator decides which tasks use the GPU from a measured budget. Needs the same check and a fallback when the budget or the device is not enough |

**Decision.** The acceleration is set per node in one of three modes. After deployment a node computes on the CPU only. An administrator switches the node to the manual or the automatic mode after a check of the device.

| Part | Decision |
| --- | --- |
| Modes | CPU only: every task runs on the CPU, the default after deployment. Manual: the administrator selects the tasks that use the GPU, the others run on the CPU. Automatic: the node selects the tasks itself from the budget and the priorities |
| Check | A background check runs when the administrator leaves the CPU only mode, at node start, after a change of the device or the driver and on request. It determines the decoders and encoders (NVDEC, NVENC, VA-API), the inference backends (CUDA, OpenVINO, CPU), video memory and the limit of encoder sessions, and measures the budget with short decode, encode and inference runs. The check reports its stages and progress and can be cancelled. Until a new mode is applied the node keeps working in the previous one. The result is stored with the device, the driver and the time of the check |
| Settings form | One form for both GPU modes: each kind of task with its cost, the device, the share of the budget and the expected state. In the manual mode the administrator selects the tasks and sees whether the selection fits the budget. In the automatic mode the form is read only and shows the choice of the node and its reason |
| Budget | Each kind of task declares its cost: decoder and encoder sessions, video memory and GPU time. The node grants a task the accelerator while the budget of the device allows it, otherwise the task runs on the CPU |
| Priorities | Live video with masks and live profiles first, then live analysis, then export, archive analysis jobs, keyframes and thumbnails. A task of a lower priority releases the device when a task of a higher priority needs it and continues on the CPU or waits in its queue |
| Failure | In the manual and the automatic modes the GPU is never a point of failure. When the device fails, its driver is reset or the selected tasks exceed the budget, the affected tasks continue on the CPU and the administrator gets a warning with the reason. After a successful check the node returns them to the GPU. Live video and recording do not stop. A selection that does not fit the budget is shown in the form before it is applied |
| Visibility | The node state, the metrics and the diagnostics of each task show the chosen path, the device and the reason for a move to the CPU |

| Task | Acceleration |
| --- | --- |
| Recording and archive | None: the stream is written without re-encoding |
| Privacy masks in live video | Decoding, drawing of masks and encoding on the GPU: the most expensive live task |
| Live profiles for web, mobile and wall clients | Encoding through NVENC or VA-API |
| Export with masks or in another format | GPU when free: a background task that may wait in its queue |
| Keyframes and thumbnails for fast scrubbing | NVDEC or VA-API for many short decodings |
| Live analysis | Hardware decoding, DNN inference on CUDA or OpenVINO. Classical OpenCV analyzers such as motion on a substream stay on the CPU where they are cheaper |
| Archive analysis jobs and synopsis | GPU at a low priority without taking the device from live tasks |
| Metadata, events and API | CPU |

**Analysis on different backends.** Inference on CUDA, OpenVINO and the CPU can give slightly different results. Every observation therefore records the analyzer, the model and the backend, and the repeated analysis of [ADR-021](021-video-analytics.md) gives the same result on the same backend. The precision and recall of a model are measured and stated per backend, and a backend without measured results is not used for that model.

**Rationale and consequences.** A per-task choice uses the GPU where it gives the largest saving and keeps live viewing ahead of background work. The CPU only default keeps a new installation predictable on any hardware, and a GPU mode is chosen only from a measured check. The cost is a check, a budget per device and a scheduler that moves tasks between the GPU and the CPU, plus the qualification of each model on each backend. The client follows the same principle on the workstation ([ADR-022](022-desktop-video-path.md)). [Placement of video analytics](021-video-analytics.md) · [Media](005-media.md)

**Verification.** A new node computes on the CPU only. Leaving this mode starts a check with visible stages, and its cancellation keeps the previous mode. On nodes without a GPU, with NVIDIA and with VA-API the check shows the devices and the measured budget. In the manual mode only the selected tasks use the GPU, in the automatic mode the read-only form matches the actual paths of the tasks. Under a full load of live masks an export job runs on the CPU or waits and does not reduce the frame rate of live viewing. A reset of the GPU during work in either GPU mode moves the tasks to the CPU without stopping live video and recording, warns the administrator and returns the tasks after the check. Every observation names its backend, and a repeated analysis on the same backend gives the same result.
