# Acceptance profile of the full release

[Русская версия](ru/release-profile.md)

The numbers below set the load and the criteria of the upcoming tests. The measured results are kept in a separate report for the signed kit. The calculation is based on continuous recording, 30 days of retention and the load of an extended site with 16 cameras. The original control of the compact site keeps its own conditions. [Register of functions and vectors](release-register.md) · [Compact profile](test-profile.md#delivery-profile)

## Installations

| ID | Minimum test configuration | Concurrent load | Archive after formatting |
| --- | --- | --- | --- |
| compact | One reference node by the original profile. Local sign-in and WebAuthn | 8 dual-stream H.264 cameras, one live viewer and one archive viewer | 14 TB, 30 days, 20% reserve |
| site | One autonomous site, 1 x86-64 server, 16 physical cores, 64 GiB RAM, separate SSD for the database and an ext4 video volume, 1 Gbit/s. Repeat sign-in without an IdP and with a separate Keycloak | 16 cameras, 2 posts with 4 cells each, 1 synchronized view of 4 archives, 1 recovery download and 1 export | 30 TB, 1 copy, 20% reserve |

For site, use the same data set and the same signed kit. Half of the main streams H.264, half H.265 Main 8-bit, 1920x1080 at 25 fps, an average rate of 2 and 4 Mbit/s in separate series. Secondary streams H.264, 640x360 at 15 fps and 0.5 Mbit/s, GOP up to 1 s. A separate HEVC/HEVC pair checks live video through a compatible copy. Include video without sound, intake of AAC-LC mono 48 kHz up to 128 kbit/s and two-way G.711 µ-law mono 8 kHz. Check playback and export by audio marks. Check the controls for listening, recording and speech transmission separately.

Clients are Chrome and Firefox from the stand, separate instances for the posts. Each post has a 1920x1080 screen with 4 decoded cells, one post has a second screen for the video wall. Enable one HEVC conversion task per active recorder in the common delivery budget, and exhaust the set quota with the next task. Run the series on software encoding or record the required hardware encoder, driver and session limit in the report. A client with declared HEVC support must decode the original archive, a client with H.264 only the permitted derivative. Keep the negotiated parameters for both series.

The archive depth is at least 30 days on each extended site at the upper bitrate. One copy at 16 cameras of 4 Mbit/s takes 20.736 TB of video in 30 days and up to 0.664 TB of sound. Of 30 TB, 24 TB are available with the reserve. The remaining space must hold the container, the index and the protected copies of the control sample. Measure the growth of files, the index and the occupied space over the series, recalculate for 30 days and confirm reading the boundary frames on a prefilled volume with known bounds. Check retention on a full volume with a separate vector. 1 TB = 10¹² bytes.

## Reference devices

The table lists the mandatory models and scenarios. The target firmware is pinned in the `reference_firmware` field of the stand profile. The original physical inventory has no connected IP cameras. The installed firmware and the result of each function are entered after reading the device identity and checking the action. [Stand profile and inventory](../tests/cameras/qualification.json) · [Current hardware series](rtsp-compatibility.md#run-on-dedicated-hardware)

| Family | Model | Equipment and what to check | Source |
| --- | --- | --- | --- |
| Fixed camera | AXIS Q1656 | SD with control frames, a microphone and an active speaker. Entry, H.264/HEVC, recording on the device and recovery download, two-way sound, a hardware event and metadata, behavior without a pan-tilt mechanism | [AXIS Q1656](https://www.axis.com/products/axis-q1656/support) |
| PTZ camera | AXIS Q6225-LE | A landmark in the frame, lit and dark scenes. Pan, Stop, zoom, focus, preset, patrol, IR illuminator and wiper. Check the outcome by the position and a control frame | [AXIS Q6225-LE](https://www.axis.com/products/axis-q6225-le/support) |
| Multi-channel device | AXIS F9114-B Mk II | Four compatible sensors, separate targets, a digital input and output control. Discover one unit and four sources, map them to points, apply a template and check the I/O | [AXIS F9114-B Mk II](https://www.axis.com/en-us/products/axis-f9114-b-mk-ii-main-unit/support) |

On all three pairs, check the identity, the protected channels, discovery, the stream profile, point stability, reconnection and archive decoding. For ONVIF the target paths are Profile T for media, G for recordings on the device, M for metadata. The protocol gives the actual protocol of each function, the trust certificate, the HTTPS and media encryption mode, the profile and the control frame. For legacy RTSP, separately check the address exception and its revocation on a controlled source. The firmware image, the equipment and the set of functions are fixed before the series. An identity mismatch, an unconfirmed mandatory function or a missing device leaves the hardware qualification open. The other load channels may be reproduced by synthetic sources. [Compatibility rules](adr/012-camera-capabilities.md)

## Measurements and thresholds

Compare the control recording and the mixed load on the same kit hash, the same cameras, media and bitrate. For each mode and bitrate - 1 minute of warm-up and 4 minutes of measurement, at least 200 samples of each latency metric. For a cold search use a range outside the cache and record its state, for a warm search repeat the same request. In the mixed series the listed posts, search, recovery download, export and events (1/s per 8 cameras) run at the same time. Inject a failure under this load. Publish p50/p95/p99, the number of samples, the error rate, CPU load, peak memory, network, disk, queue depth and lost frames. [Compact profile method](test-profile.md#measurement-protocol)

| Measurement | Limit of the extended profiles | Method and basis |
| --- | --- | --- |
| Live video, alarm, search | p95 up to 2 s for each metric, cold and warm search separately. First archive frame p95 up to 3 s | From the selection to the decoded frame, from receiving the event to the visible card, from the request to the first page. The waiting budget of the compact profile operator is kept |
| Quality switch, position and synchronization | First frame after a profile selection p95 up to 2 s. Deviation of the selected position up to 1 s. On four points with a known binding the maximum desynchronization is up to 200 ms | Compare the number and the reference UTC mark in the frame. 1 s corresponds to the GOP, 200 ms to five frames at 25 fps. Inject a source offset of +2 s, and the segment with uncertain time must have an explicit mark |
| Load isolation and permissions | 0 additional gaps of a healthy recording outside the failed node, 0 errors of regular requests, 0 deliveries of a forbidden frame or audio. Permission revocation up to 5 s, blocking in the IdP up to 60 s | Compare the coverage with the control run by frame numbers, record intentional failures separately. Measure up to the last delivered frame, export part or message in the active session |
| Recovery download and copies | Recover 60 s of recording within 120 s after the camera returns, with 60 s available on the SD. Repeats give 0 duplicates. Overhead, the original archive and the protected copies of the control sample fit into the allocated volumes | Limit the recovery download of one camera to a 4 Mbit/s band above continuous recording. At an original 4 Mbit/s the budget leaves a minute for search and confirmation. Compare the original and received frames, bytes and actual coverage |
| Detector and rules | Recall TP/(TP+FN) at least 0.90, precision TP/(TP+FP) at least 0.95, 0 real reactions in simulation | Pin the SHA-256 of the labels before the start. Include 100 positive and 100 negative episodes for each mandatory rule, including zone boundaries. One match per episode, the same point and class, time within 1 s. Up to 10 misses per 100 references are allowed while limiting false alarms for the post |
| Hold and export | Protect 5 minutes on 4 points at the same time. 0 lost held frames with an intact copy and 0 signature mismatches of a genuine export | Compare the actual bytes of the original and the copies, the frames at the bounds, the absence of a closed zone and of sound in a restricted derivative. The sample size matches one multi-camera case |

### Losses and recovery

Recording RPO is measured by missing frames, read RPO by the loss of previously confirmed archive. Recording RTO counts from the start of the failure to a new decodable block, read RTO to the first earlier decoded frame with verified permissions. The original values apply to a compact installation. In each case compare the configuration and the permissions separately, and for copies check before and after the recovery. [Compact node control](test-profile.md#delivery-profile)

| Failure and conditions | New recording RPO / RTO | Reading the earlier archive RPO / RTO | Configuration and basis |
| --- | --- | --- | --- |
| site, service restart with a healthy source and volume | 30 s / 60 s | 0 s / 15 min for preserved blocks | RPO 5 min with copies every 5 min, RTO 5 min to sign-in and permission check. The limits are carried over from the compact node to the 16-channel test |

Separately run the recovery of a single node with a preserved archive volume and measure the full time from the start of the operation. [Recovery scenario](maintenance.md)

## Final tasks

The original set of the compact site, the participant selection rules and the allowed help stay in the first-use protocol. For the extended tasks the timer runs from giving the goal to demonstrating the result, including waiting for the system. Any critical error or exceeded time means an unfinished task. A task with help allows the use of the built-in portal. [First-use protocol](ui.md#first-use-check)

| Role and task | Input data | Success | Budget and basis | Critical errors |
| --- | --- | --- | --- | --- |
| Administrator, camera batch | 4 channels, different revisions, one disconnected source, a template and a recording policy | Clarified the differences, applied the settings, restored the failed channel and confirmed the recording with a new frame | 12 min to check four channels, fix one and verify the recording | Overwrote a conflicting setting, confused a point or took live video for a recording |
| Operator, case | One alarm, four points on one site, a known gap, a closed frame area and a reference event | Found the correct frames, marked the gap, protected the material and released a verifiable permitted copy | 12 min to search, compare four points, protect and release once | Substituted a frame for a gap, chose another episode, released the original or sound without permission |
| Administrator, help on a failure | Live video is available, the archive volume reached its reserve, a known error code, a free test volume | Found the article, established the cause, restored recording the regular way and showed a new frame. The request, the time to the article and the result are kept | Up to 2 min to find the right article and up to 8 min in total for diagnostics with a frame check | Deleted protected material, changed someone else's policy, took live video or an old frame for a recovery |
