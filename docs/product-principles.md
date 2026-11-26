# Bacuri VMS product principles

[Русская версия](ru/product-principles.md)

This document names the properties in which Bacuri VMS has to be better than every available VMS, and the properties where the level of the leaders is enough. Design decisions, ADRs and code reviews are checked against it. [Acceptance profile](release-profile.md)

## Positioning

Bacuri VMS is a free video management platform under the MIT license. It has no per-camera licenses, no lock-in to a hardware vendor, no cloud dependency and no mandatory integrator. Any site gets the capabilities that are usually available only in commercial VMS: a verifiable archive, access control, audit and investigations.

The product succeeds when people can install it, understand it, verify its data, extend it and continue its development without its author.

## Priority properties

Status: **Accepted.** The order decides when properties conflict.

1. Openness.
2. Operator speed.
3. Archive truth.

The AI actor becomes a priority property once the thresholds of the first three are met.

The thresholds below are the target level of the product. The thresholds of the [acceptance profile](release-profile.md#measurements-and-thresholds) remain the minimum condition for a release.

### 1. Openness

Openness means more than the license: the user does not depend on the project or its author.

| Requirement | Check |
| --- | --- |
| The data belongs to the site | Recordings and exports play in standard players and ffmpeg without Bacuri components. The catalog and block manifest formats are documented and versioned. [Archive and export format](archive-format.md) |
| Verification without trusting Bacuri | The export verification algorithm is published, and the verification tool builds from source separately from the server |
| Open contracts | Every operation of the web client is available through the published API. The client has no private internal paths |
| Hardware of any vendor | The basic scenarios work through ONVIF and RTSP. Vendor adapters only extend them |
| Low barrier to contribution | From cloning the repository to a stand with a synthetic camera and recording: one command and at most 15 minutes on a typical developer machine |
| Documentation for everyone | The primary language of the README and documentation is English, the second is Russian. The English version is the source, the Russian version is updated in the same change |
| Verifiable delivery | Rebuilding the bundle from the same commit in the same environment gives identical artifact hashes. The bundle includes an SBOM |
| Affordable hardware | The compact profile meets the acceptance thresholds on an inexpensive x86-64 mini PC without a discrete graphics card |

### 2. Operator speed

The current acceptance thresholds (p95 up to 2 s) match the level of the leaders. A priority property needs a separate, stricter level.

| Measure | Target threshold on the local network |
| --- | --- |
| Visible reaction of the interface to an action | p95 up to 100 ms |
| From selecting a camera to the first decoded live frame | p95 up to 500 ms |
| From receiving an event to a visible alarm card | p95 up to 500 ms |
| From an alarm card to the archive frame at the event | one action, first frame p95 up to 1 s |
| Main operator actions | available from the keyboard without a mouse |
| Transitions live -> archive -> plan -> alarm | camera, time and place are kept without selecting them again |

### 3. Archive truth

The system never reports more than the recorded data confirms.

| Requirement | Check |
| --- | --- |
| Recording state comes from the archive | "Recording" is shown only when the catalog holds a frame not older than a set interval. A recording setting by itself is not a state |
| Gaps are always visible | Every gap on the timeline has a cause: no stream, recording stopped, storage failure, removal by retention |
| Time with uncertainty | Intervals with unknown or corrected time are marked in the interface and in exports |
| Integrity from recording to export | A change of any archive block is detected by verification. Exports are verified offline |
| Protected fragments | A protected fragment is removed neither by retention nor when the storage fills up |
| Live is never mistaken for recording | In manual acceptance no participant takes live video for a recording |

### Later: AI actor

Until this property becomes a priority, the architecture must not rule it out. Every action goes through the common layer of authorized commands, and the type of actor is recorded in the audit.

## Interface

Status: **Accepted.**

The interface sets the rules for the industry rather than repeating the structure of existing VMS. The goal is to surpass the leaders in ergonomics, simplicity, convenience and visual design. The interface embodies the priority properties: operator speed and archive truth are visible on every screen.

### Interaction models

| Model | Rule |
| --- | --- |
| One timeline | Live and archive are one viewing surface. Live is the right edge of the timeline, and going back in time is a scroll of the timeline without changing the section and without restarting playback |
| Command line | Ctrl+K opens a search over points, plans, alarms and actions. A query such as "gate 2 yesterday 22:00" opens the point at that moment |
| Alarm as a review session | An open alarm shows on one screen the frame at the event, live video, the point on the plan and the available actions. Next and previous alarm, acceptance, closing and escalation take one key |
| One recording indicator | A point has one composite state indicator. It comes from the archive catalog and shows the age of the last recorded frame. Connection, live and recording unfold in its details |
| Connection without addresses | An empty site starts camera discovery at once. The administrator selects cameras and enters credentials. A live checklist passes the steps stream -> recording -> archive frame by itself. Entering RTSP addresses is the fallback |
| Video is the main object | On the operator console video takes the main area of the screen. Navigation and panels collapse and do not take area from the frame |

### Defaults

- The operator console and the administrator settings are separate workspaces with their own navigation. An administrator switches between them with one action.
- The operator console uses the dark theme by default. The theme chosen by the user is kept.
- Time is shown in the time zone of the site, with relative time for recent events. UTC and the offset are available in details, exports and the audit.
- Recovery after a failure closes or updates the original alarm and does not create a new alarm.
- The severity of an alarm is visible before reading the text: by its position in the feed, the shape and the color of its label.
- One state is shown in one place. Duplicate state labels are removed.
- An empty screen names the next step and offers the action for it.
- Navigation follows the tasks of the user, not the structure of the implementation. Menus are at most two levels deep.
- Plans and maps zoom with scrolling and gestures.

### Visual design

- An own visual language of a professional tool: dense, calm, content first. The template layout of an administration panel is not used for the operator console.
- Color marks state and the main action. Decorative colors are not used.
- Nested containers with borders are not used. Grouping comes from spacing and typography.
- All controls use the shared components and tokens. Unstyled system controls are not used on working screens.

### Thresholds

| Measure | Target threshold |
| --- | --- |
| Share of a 1440x900 screen taken by video when one point is viewed on the console | at least 70% |
| Going to a point at a given moment through the command line | a query and one confirmation |
| From an alarm to the frame at the event | one action |
| Connecting an ONVIF camera | without entering stream addresses, only selecting the camera and entering credentials |
| Number of state indicators of a point on the screen | one |
| Reviewing the alarm queue | every review action is available from the keyboard with one key |

## Level of the leaders

Federation, clusters, PTZ, maps, integrations and the other functions meet the thresholds of the acceptance profile. Being better in them is not a goal until the thresholds of the priority properties are met.

## Constraints

- A function is not accepted if it worsens a priority property: it slows the first frame, makes the recording state indirect or requires a closed format.
- The main scenarios do not depend on the cloud or external services.
- The archive is not stored in formats that cannot be read without Bacuri.
- A new technology is accepted when it gives a measurable gain in a priority property or the technology policy requires it. [ADR-015](adr/015-technology-lifecycle.md)
- Breadth of camera function support does not outweigh the reliability of the basic scenarios.

## Application

- ADRs and large changes state their effect on each priority property.
- Code review checks the priority properties first, then the other [quality](quality.md) criteria.
- Target thresholds are measured in the release candidate run together with the thresholds of the acceptance profile. A missed target threshold is recorded as a deviation and does not block the release.
