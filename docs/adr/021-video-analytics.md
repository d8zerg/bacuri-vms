# ADR-021. Placement of video analytics

[Русская версия](../ru/adr/021-video-analytics.md)

**Status** - Accepted.

**Context.** The site detects motion inside the recorder: it decodes the stream to a 64×48 grayscale image and compares neighboring frames. The result drives alarms and the motion search in the archive. Further analysis needs object tracking, lines and zones with direction, camera tampering checks, object classes and automatic anonymization of exported copies. Such analysis can run on the server, in the web or desktop client, or in the camera. The choice decides whether alarms exist when no client is open, whether two workstations see the same events and what load the recorder and the operator workstation carry.

**Options.**

| Option | Assessment |
| --- | --- |
| Analysis in the client | Events exist only while a client shows the camera. Two workstations compute different results. Alarms, the archive index and the audit cannot rely on them. The workstation spends its GPU on analysis instead of showing video |
| Analysis in the recorder process | One process again owns recording and an unbounded computing load. A failure or a slow model can delay writing blocks. The recorder cannot scale analysis separately |
| Analysis in the camera only | The capabilities depend on the model and firmware. The results do not have a common schema and cannot be repeated on the archive |
| Separate server analytics service | Events and metadata exist without clients and are the same for all consumers. Recording is isolated from analysis. Analysis scales by nodes and repeats on the archive |

**Decision.** Video analytics is a separate server service `bacuri-analytics` in C++23 with OpenCV 5. It runs on the recording node or on a separate analysis node of the same site.

| Part | Rule |
| --- | --- |
| Input | A subscription to the stream of the recorder, by default the secondary profile, with hardware decoding where the node has it. For a repeated analysis or a rule test the service reads the archive of the same point |
| Analyzers | Each analyzer declares its name, version, input profile, frame rate, configuration schema and resource cost. Classic analyzers use OpenCV image processing and video modules. Models run through the OpenCV DNN module from ONNX files pinned by hash in the delivery |
| Output | Observations go to the core through `SubmitObservation` with the point, the binding revision, the rule version and the source time. Metadata goes to the analysis index: tracks, object classes, the motion grid, the activity level and camera health. Each record carries the UTC time and the PTS of the frame and the analyzer version |
| Alarms | The core forms alarms from observations as today. The service never shows anything to people and never sends text meant for people: causes are codes of the error catalog |
| Clients | The web and desktop clients read metadata through the public API and show it over the frame: track corners, a short track tail, rule zones and the activity strip on the timeline. A client computes no events and no index. Display-only processing, such as dewarping a fisheye image, does not change the archive or the exported copy |
| Cameras | Events and metadata of the camera, including the ONVIF metadata stream, are normalized into the same schema with the camera as the source |
| Privacy | Faces are detected only to mask them. Identification of people by biometric features is not part of the product. Metadata follows the rights, masks and retention of the video of the same point |
| Failure | A stopped or overloaded service does not stop recording or live video. The point shows that analysis is unavailable, the archive keeps a gap in the analysis index, and a repeated analysis can fill it |

**Rationale and consequences.** The server placement keeps one result for alarms, search, export and audit and lets analysis run around the clock without operators. The recorder loses the detector and keeps only writing. A new analyzer is added to the service without changing the clients, which read the common metadata schema. The service needs its own resource budget per node, and the qualification states the supported number of streams for each analyzer and node profile. The delivery carries the models with their licenses and hashes and works without network access. OpenCV follows the technology lifecycle rules: a pinned supported branch and controlled promotion of fixes. [Technology lifecycle](015-technology-lifecycle.md) · [Offline delivery](011-offline-delivery.md) · [Time model](004-time.md)

**Verification.** A labeled video set with day, night, infrared and weather scenes gives precision and recall per analyzer and the false alarm rate per camera-hour. The same archive analyzed twice gives the same observations and metadata. The time from a frame to an observation and to an alarm on a workstation is measured at p95 under the node load profile. Stopping the service keeps recording and live video, and the point state shows the unavailable analysis.
