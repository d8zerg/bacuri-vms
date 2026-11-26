# RTSP intake and compatibility matrix

[Русская версия](ru/rtsp-compatibility.md)

The recording node opens a separate RTSP/TCP session for each profile of the applied configuration. After a restart it restores the addresses and credentials from the encrypted SQLite snapshot. When an address or a secret changes, the corresponding session is recreated. The `receiving` state means that the node has received an H.264 frame suitable for starting decoding.

## Tested sources

| Source and scenario | Main profile | Secondary profile | Result |
| --- | --- | --- | --- |
| Synthetic GStreamer camera, RTSP/TCP | H.264, 1280×720, 25 fps | H.264, 640×360, 15 fps | Simultaneous intake and decoding, the preview queue limit, a TCP break and recovery of both sessions |
| Chicony Integrated Camera through V4L2 and an RTSP emulator, USB `04f2:b7b4` | H.264, 1280×720, 25.03 fps, 2.01 Mbit/s, on average 25 frames per keyframe | H.264, 640×360, 15.02 fps, 0.51 Mbit/s, on average 15 frames per keyframe | Both profiles decoded. With preview reading stopped, the maximum was 8 frames and 39,466 bytes. 0 drops in the main queue. After the connection was restored, the node closed a gap of 3.53 s |
| Synthetic camera, JPEG/RTP profile | JPEG, 320×240 | - | The `incompatible_profile` state was reported, the number of accepted H.264 frames is 0 |

The bitrate and frame rate for Chicony were measured on the compressed frames at the node input. The emulator captured one `/dev/video0` device, scaled the video and encoded two profiles. The result applies to this pair of source and RTSP server. Compatibility with the own RTSP and authentication implementation of a specific IP camera is checked in a separate run that names the model and the firmware.

`task check:report` finished with code 0. 19 CTest tests passed on each of the four presets. Static analysis and the contract and schema checks passed. On GCC and under ASan/UBSan, both RTSP profiles after restoring the node from SQLite with the core stopped, the refusal for JPEG/RTP, the overflow of the preview queue and the reconnection after a break were checked. `task webcam:check` separately confirmed intake and decoding from a webcam.

## Video intake by profile codec

ONVIF Media and Media2 report the codec when profiles are selected. On connection the recorder checks the actual RTP type and the video stream parameters. For H.264 and HEVC Main 8-bit, the codec and the decoder configuration are kept in every fMP4 segment. In the multi-camera workplace the main and secondary streams are chosen independently for each client. [Live and archive verification](test-profile.md#live-profiles-and-multi-camera-viewing)

## Hardware matrix for the release

For the software profile, the bacuri.vms ONVIF emulator with the firmware from the stand profile, HTTPS for control and RTSPS for both streams are pinned. The stand trusts a temporary certificate created for one run. There are no exceptions for open transport. The inventory of qualified physical devices is empty so far. For the full release, the reference pairs, the equipment and the mandatory functions are defined. When each pair is connected, keep the observed identity and the results of the individual functions. [Reference devices of the full release](release-profile.md#reference-devices)

| Function | What the automatic scenario checks on a physical device |
| --- | --- |
| Protected transport | In the model row, give the control mode and the media channel, the trusted certificate source and the recording result. In protected mode, confirm the encryption of the actual video stream. On a controlled source, separately substitute an untrusted certificate and a wrong name and check the refusal without sending the secret and without falling back to open RTSP. For an old camera, check the refusal before the address exception is enabled and after it is revoked |
| Discovery and entry | The camera is found in a permitted segment, a repeated search keeps its identity, wrong credentials give a diagnosable refusal |
| Streams and replacement | H.264 profiles are selected, frames of live video and of a new recording are received. After a replacement, the old and new intervals are available on one point |
| PTZ | On a model with PTZ, the stand confirms the actual change of position and the stop by feedback or a control frame. For a model without the function, the interface and the API take the missing capability into account |
| Hardware event | A programmatically controlled stimulus causes a camera event. The source, the time, one alarm on repeated delivery and the transition to the frame are checked |

Attach to each row the release candidate identifier, the model, the firmware, the declared capability, the experiment conditions, the actual result and the report. A mandatory function without confirmation on the release candidate blocks the release. Hardware qualification stays open until the mandatory physical models and results are available. [ADR-006](adr/006-test-stand.md) · [ADR-012](adr/012-camera-capabilities.md) · [Reference devices](release-profile.md#reference-devices)

## ONVIF stand

`task camera:check` starts an isolated Docker Compose container without an external network. An RTSP server creates two synthetic GStreamer H.264 streams. The simulator accepts a unicast UDP Probe and SOAP requests over HTTPS with UsernameToken, returns media profiles, models the PTZ position and a PullPoint subscription. A stimulus queues a motion event for the active subscription. A webcam is available in a separate V4L2 run through `task webcam:check`. [Hardware scenario](../tests/cameras/series.py) · [ONVIF specifications](https://www.onvif.org/profiles/specifications/)

The series checks a repeated discovery, the exact identity, the refusal for a wrong password and two different profiles. For each profile it requests the URI and decodes H.264 over TCP inside TLS. For PTZ it compares the coordinate before and after a limited move and waits for the stop confirmation. For motion it checks the source, the time and the value of the event after the stimulus. A separate run of the wizard checks the connection to a point and playback. An end-to-end camera control scenario checks the link between an event and the archive frame.

The `emulator.json` report in the output directory contains the observed device properties and the function results. Negative runs check the refusal on a trust error, a mismatch of the name, the firmware or the codec, an incomplete profile and a missing result. In the reference run 8 functions passed. Frames of 1280×720 and 640×360 were decoded, the pan coordinate changed from 0 to 0.1, and the IDLE state was received after Stop. A motion event from the main source was received. The empty inventory of physical cameras gave code 1.

### Connection wizard

In the "Cameras" section the administrator chooses "ONVIF discovery and camera replacement" and starts the search. A replacement needs an existing point. After the credentials are entered, the wizard shows the manufacturer, the model, the firmware, the serial number and the declared PTZ and Events services. The administrator chooses different recording and live profiles, connects the camera, creates a test recording and confirms the decoded frame. After a replacement, the previous recording of the same point is available. Manual entry is available for a known pair of addresses.

- Discovery uses IPv4 WS-Discovery multicast `239.255.255.250:3702`. A local segment needs an interface with multicast available. To make the core send a Probe to a single camera behind a router, put its IPv4 with a `/32` mask in `BACURI_CAMERA_NETWORKS`. When a device announces only HTTP on the standard port, the wizard offers the same path over HTTPS on port 443. When a camera is selected, the core checks TLS before sending credentials.
- Control uses Device and Media ver10 over HTTPS with WS-Security UsernameToken PasswordDigest. The wizard reads the Media service address and the tokens from the camera response, chooses H.264 and requests GetStreamUri. Service and stream addresses with the same host name, two different stream URIs with an optional `profile` parameter and `transmode=unicast` mode, and RTP over RTSP/TCP are supported. Protected video needs RTSPS.
- By default both processes use the system trusted certificates. For a private certificate authority, mount a PEM file read-only into both containers and set its absolute path inside the container in `BACURI_CAMERA_CA_FILE`. The name in the address must match the certificate. Credential parameters are passed separately from the URI.
- For open RTSP in a dedicated segment, set `BACURI_LEGACY_RTSP_ADDRESSES` to a comma-separated list of specific IPs for both processes and recreate the containers. The address must also be within a permitted CIDR. An empty list closes the open transport. Changes of the active list are recorded in `bacuri_inventory.camera_transport_audit`. Changing an address or removing an exception requires reviewing the setting. For the local RTSP stand, `docker compose exec -T rtsp-camera hostname -i` gives the IP.

`task camera:commissioning` runs the check on synthetic video. `commissioning.json` and Chrome screenshots keep the results of opening the recording before and after a replacement, including after the recording node is restored. The hardware protocol checks a specific pair of model and firmware. [End-to-end scenario](../tests/cameras/commissioning.py)

### PTZ control and motion intake

Movement commands use the standard coordinate spaces of the main profile. When a one-second timeout is supported, Bacuri sends ContinuousMove. For a profile with a longer minimum movement time, Bacuri checks the availability of a short RelativeMove on the selected axis. Stop addresses the supported axes. Focus uses a relative move or an Imaging Move pulse followed by Stop. On a disconnect, Bacuri repeats the focus stop for the original device and connection revision. Presets are read from GetPresets. Starting a patrol is available for a route with positions and a duration of up to 60 seconds set by the camera. An active patrol can be stopped. The illuminator, the wiper and the relays use the commands and tokens declared by the device.

A control request includes the current camera identifier and a replay key. Core checks the permission for the point and the ownership, records the dispatch before contacting the device and keeps the result for a repeated request for 24 hours. The action log keeps the result after this term. A lost response gives an unknown outcome. For a digital input, the initial state and the changes of the `Device/Trigger/DigitalInput` topic are accepted. A reading is bound to the input token and the connection revision. The output state and the input change are checked separately.

To receive sound, the node chooses the RTSP audio track. For speech transmission, a separate ONVIF backchannel G.711 A-law or µ-law at 8 kHz is negotiated. The recorded mono PCM has its own checksum and belongs to the same block and binding as the video. Old blocks without sound still play. An export with requested sound contains a separate WAV file for each recorded period. [Export format](archive-format.md#signed-export)

For a previously connected camera or after a firmware change, run the ONVIF search, read the profiles and choose "Refresh the connected camera". The wizard checks the identity and stores the new configuration under the previous binding. With manual RTSP entry only the media channel is available.

The core reads PullPoint notifications over HTTPS, renews the subscription and creates a new one after a lost connection. From the `RuleEngine/CellMotionDetector/Motion` topic, a change of `IsMotion` to `true` for the `VideoSourceConfigurationToken` of the selected profile is accepted. The observation identifier depends on the device, the binding, the event source and the camera time. The `motion` rule publishes one alarm on repeated delivery. For matching with the archive, the timestamp precision and the clock difference from the PullMessages response are taken into account. With an uncertainty above 5 s, the receive time is used, and the transition to an exact frame gets the uncertain time status. [ONVIF Events service](https://www.onvif.org/ver10/events/wsdl/event.wsdl)

| Device on the stand | Verified functions |
| --- | --- |
| bacuri.vms ONVIF emulator | Command selection by profile, horizontal movement, zoom in and out without panning, Stop, stop by timeout, command repeat, control priority, PullPoint renewal and recovery, motion repeat up to one archive frame |

`task camera:commissioning` keeps `camera-operations.json` and a Chrome screenshot. The report contains the identifiers of the observation, the alarm, the binding and the block, the UTC position and the frame index. The stand compares the image in the browser with an independent decoding of the original block and the neighboring frames. [End-to-end test](../tests/cameras/operations.py)

### Run on dedicated hardware

Add to the `hardware` array mandatory rows of the same format as `emulator`, with the manufacturer, the exact model and firmware, the functions and the trust source. For the current profile HTTPS, RTSPS and no exception are mandatory. Include the `ptz` and `motion` functions only for the corresponding models. The whole kit must cover both functions. [Inventory](../tests/cameras/qualification.json)

`task camera:hardware STAND=/absolute/path/to/stand` mounts the stand directory read-only. Put the `bundle` delivery file, the trusted PEM, the password file and the `connections.json` array in the directory. Each inventory row needs one connection record with the following fields.

| Fields | Meaning |
| --- | --- |
| `manufacturer`, `model`, `firmware` | The same exact values as in the inventory. The identity is compared with GetDeviceInformation |
| `device_service`, `discovery_port` | The full HTTPS address with the name from the camera certificate, reachable from the Compose network. The device accepts a unicast Probe on the UDP port, 3702 by default |
| `ca_file`, `username`, `password_file` | The path to the trusted PEM relative to the stand directory, the ONVIF account name and the relative path to the password file |
| `profile_tokens` | An object with different `main` and `sub` tokens from GetProfiles. For the PTZ series, the main profile supports GetConfigurationOptions, RelativeMove, ContinuousMove with a 1 s timeout, Stop and GetStatus |
| `stimulus`, `motion_source` | To check motion, give the relative path to the executable of the physical stimulus and an object with the `name` and `value` fields from the ONVIF event source. Synchronize the clocks of the camera and the stand |

Code 0 means a successful run of every declared function of every mandatory firmware. The `hardware.json` report contains the hash of the kit when it is checked, the hash of the profile, the identity, the decoding result of each stream and the command results. An empty hardware matrix, a skip, a firmware mismatch and unavailable hardware end the command with an error.

## Load and recovery

| Boundary | Limit | Behavior |
| --- | --- | --- |
| Main consumer queue | 64 frames or 8 MiB | Keeps the accepted frames. When space runs out, records the loss of incoming frames and its start. Intake into the queue resumes from a keyframe |
| Preview consumer queue | 8 frames or 1 MiB | On overflow, empties the queue and resumes delivery from a keyframe that contains the decoding parameters |
| Input of each pipeline | 250 ms jitter buffer, appsink up to 4 buffers or 8 MiB | Streams have separate pipelines and separate threads. An oversized H.264 unit or a damaged buffer ends the current session |

On an error, EOS or missing data the node ends the session. Waiting for the first frame is limited to 8 s, for the next frames to 5 s. Reconnections happen after 1, 2, 4, 8, 16 and then 30 s. After 5 s of stable intake the initial delay returns to 1 s. Stopping interrupts the wait for a retry.

The `rtsp_ingest` log contains counters of frames, bytes, keyframes, reconnection attempts, gaps and the losses of a specific consumer. A source gap opens after the last accepted frame and closes at the first suitable keyframe. The bounds are given in UTC microseconds of receive time. Until the first successful connection the log reports the reason the source is unavailable. Frame drops of the preview consumer are counted in its queue.

## Time and repeatable verification

For an H.264 unit, the original 32-bit RTP timestamp at 90 kHz, the PTS in nanoseconds, the receive time and the available original NTP timestamp are kept. The node matches the RTP metadata with the frame by PTS within one session. After an NTP reference with a difference of up to 5 s from the node clock is received, `source_clock` is used. The uncertainty estimate equals the observed difference. Without a reference or above the threshold, `reception` is used. A reconnection or a change of the NTP to PTS difference by more than 500 ms creates a new binding epoch. [GStreamer metadata](https://gstreamer.freedesktop.org/documentation/gstreamer/gstbuffer.html#GstReferenceTimestampMeta) · [RTCP synchronization in jitterbuffer](https://gstreamer.freedesktop.org/documentation/rtpmanager/rtpjitterbuffer.html)

`task ingest:check` uses the synthetic camera. `task webcam:check` additionally starts the emulator and decodes 25 frames of each profile at the same time. A test TCP proxy closes the established connections and rejects reconnections during the interruption. After the run, `task webcam:down` releases the device. [Webcam setup](local-development.md#rtsp-emulator-with-a-webcam)
