# Bacuri VMS interface and user scenario verification

[Русская версия](ru/ui.md)

**Visual system status** - Accepted. The working client is started with `task control:up`. After sign-in the administrator has the "Cameras" section, the operator "Monitoring". [Workplace on the local stand](https://localhost:8443/) · [Account setup](local-development.md#control-server)

To reproduce individual states, run `task ui-kit:up` and open the "Workplace / Screens" section in Storybook. There the same components use fixed alarms, local video recordings and a simulated connection failure. `task ui-kit:down` stops the catalog. [Component catalog](http://127.0.0.1:6010)

## Catalog and camera connection

The "Cameras" section opens the "Catalog of connected devices". Channels are grouped by device. Search finds a device by model or serial number, a channel by name and an observation point. Channels can be filtered by the camera connection and by the state of the configuration application by the recorder. Expand a channel to see its revision and the point binding.

In the "Connection" subsection choose "Manual" to enter the stream addresses of one camera, "ONVIF discovery" to discover a camera and choose its profiles, or "Batch" to connect several channels.

### Camera batches

In the "Batch" connection run discovery and request the channels. For devices with different credentials, choose the device before the request. Mark the needed channels in the list. To change the name or choose a point, expand the channel row. One channel needs two different H.264 profiles of one source. The first entry creates a point or uses a selected free point. An update keeps the previous binding.

The site template sets a higher or lower resolution profile for recording and the second profile for live video. An empty brightness field keeps the camera setting. A value on the ONVIF Imaging scale applies to the marked channels that support its range. Press "Compare configurations". The channels are grouped by device. The list shows the name and the state of each channel, and on an error its reason. Expand a row to compare the previous and the selected streams and check the brightness, the firmware and the template revision. A saved template is available to the other administrators of the site.

Press "Apply the batch", then "Refresh the results". When the recorder confirms the saved configuration, the channel status changes to "Applied by the recorder". The revision numbers and the rollback command are available in the expanded row. After the connection returns, press "Retry unfinished". On a revision conflict, get the channels again and compare the changed channel with the current configuration. Rolling back an update creates a new revision with the previous settings. Rolling back an initial entry disconnects the channel from the point and keeps the history. To replace a device and open the previous recording, use "ONVIF discovery and camera replacement". The protocol list shows the last 100 batches of the current administrator.

## Access management

In the "Access" section choose "Users" and expand a user row to change the role or the state. On the "Permissions" tab choose a site user and grant a permission for a point or a group. For viewing and export, the recording interval can be limited. Export needs both permissions for the whole requested interval. The group membership applies to all its grants. Changing the role or blocking an account ends its sessions. Each site needs at least one active administrator.

On the "Privacy" tab choose a point and the kinds of output. For live video and the archive, set closed rectangles on the frame layout or give the bounds in percent. The server paints the areas over before the video is encoded for viewing. Without areas, with the conversion unavailable or the quota exhausted, the protected image is closed completely. The restriction also applies to the administrator. Thumbnails and export can be closed completely. Sound has separate permissions. A changed policy is checked for open transfers as well.

In "Monitoring" -> "Live", the "Multi-camera live video" button opens a grid of the available cameras. In the layout editor choose the cameras, set the order and save the name. A personal layout is visible to its author. The administrator has the shared layout of the site. When the operator has no permission for a camera, its place in the grid is kept without the name and the image.

In automatic mode the cell size determines the preferred profile. When a camera is expanded, the other connections are released. The back button or Escape restores the grid. Under the camera name the selected stream, the codec and the resolution are visible. In the "Live quality" menu the main or the secondary stream can be chosen manually. The site budget and the decoder support are taken into account at every opening. The video wall button opens the layout in a separate window that can be moved to another monitor and expanded to full screen.

For corporate sign-in, register a confidential OIDC client in Keycloak with Authorization Code Flow, PKCE S256 and the exact return address `https://<site-node>/api/v1/session/oidc/callback`. In "Access" -> "Sign-in", set the HTTPS issuer of the realm, the client ID, the secret and the `acr` value that the realm issues after the second factor. Save the settings, choose a user and give the user's `sub` at this issuer. With a corporate certificate authority, add it to the trusted root certificates of Core or set the path to the PEM in `BACURI_OIDC_CA_FILE`. Active Directory is connected in Keycloak. [Keycloak authentication levels](https://www.keycloak.org/docs/latest/server_admin/#_step-up-flow)

After the provider is enabled, the sign-in screen shows "Sign in with Keycloak". The local administrator can sign in with the password and the WebAuthn key even with the provider disconnected. The "Audit" tab shows the action, the time, the actor and the available resource names. Expand an event to see the identifiers, the access method and the original action code. Access audit search through the API accepts actor and resource filters. [REST contract](../contracts/v1/openapi.yaml)

### Desktop application sign-in

The Bacuri Desktop application signs in through the browser of the same computer. The application opens the "Sign in to Bacuri Desktop" page of the site. Sign in there with the usual method of the site, check the account and press "Sign in to the application" only if you started the sign-in in the application just now. The browser hands the sign-in over to the application and shows that the tab can be closed. "Cancel" ends the sign-in in the application. An expired or already used sign-in shows its cause, start the sign-in from the application again. The application gets its own session with the permissions of the account, and signing out of the browser does not end it. [Desktop session](adr/001-api-contracts.md#local-session-and-access-rules)

## Visual system

| Component | Rule |
| --- | --- |
| Workspace | In the top bar next to sign-out, three icons switch the theme. The monitor follows the system setting, the moon turns on the dark theme, the sun the light one. The system mode is selected initially, and the browser remembers the choice. The video frame and the opaque label backgrounds keep their colors in both themes. On a wide screen the video and the alarms are side by side, on a narrow one the feed follows the timeline |
| Interface language | In the top bar next to the theme, the RU and EN switch changes the language of labels, messages, dates and numbers without a reload. Initially the browser language is selected when it is supported, otherwise English. The browser remembers the choice. Camera and point names, operator text and error codes are not translated |
| Section menu | At a width from 75 rem (1200 px at the default font size) the sections are on the left. The button at the bottom of the panel collapses it to icons and expands it back. The browser remembers the choice. At a smaller width the menu is at the top. On a phone the icons are aligned in one row with a one-line label under each. At a width up to 30 rem the "Activity journal" and "Maps and plans" sections are labeled "Journal" and "Maps". A screen reader reads the full names |
| Screen name | The active menu items and the browser tab title mark the current place. The first-level heading is available to a screen reader. The work area shows the names of objects, field groups and operation results |
| Subsections | In the side menu the items with icons are under the active section. In the collapsed menu the name is available in a tooltip. On smaller screens the buttons are placed on the page. In "Access" six buttons take one row, at a width up to 44 rem the grid has three columns, up to 24 rem two |
| Commands | Refresh buttons are marked with a circular arrow. The tooltip and the accessible name say what is refreshed. From a camera card and the log the corresponding buttons open live video, the archive or the selected alarm |
| Page loading | When a link is opened or the page is reloaded, the document background immediately matches the saved or system theme. While the client loads and the session is checked, the background stays the same. With a long wait, one status line appears |
| Color and typography | Both themes use the [common color tokens](../design/bacuri/tokens.css). In the light theme headings and body text are almost black, labels are neutral gray. The main button has a muted sand-yellow fill. The active section is marked by a background and a line along the menu edge, the selected alarm by a background and a side bar. Olive marks a healthy connection and confirmation, terracotta a warning, carmine an alarm or a failure. Time and counters use monospaced digits |
| Brand mark | The header shows a cross-section of a bacuri fruit. Three cream segments with seeds form a triangular aperture of a diaphragm inside a golden ring. For the browser tab the mark has separate geometry at 16 pixels. [Brand kit](../design/bacuri/brand) |
| Camera form | Explicit labels, separate addresses and hints about their purpose. A fixable error focuses the field, and the values of the open form are kept |
| Check result | The connection, the main stream, the secondary stream, recording and playback have separate states. Camera availability and a successful frame read are checked as different actions |
| Archive timeline | Solid fill for a recording, diagonal hatching for a gap, vertical terracotta strokes for estimated time, dots for cataloging. The legend, the state text and the highlighted position complement the color |
| Alarm | The card shows the handling state, the UTC time and the point. A common feed label reports that the source has no priority. Opening the archive moves the focus to the video area, returning moves the focus to the selected card |
| Failure | The message shows the reason and the available action. A lost connection stops playback. After reconnection the selected alarm and the interval are kept |
| Keyboard | Tab and Shift+Tab move the focus, Enter and Space activate buttons. On the timeline the arrow keys change the position, Home and End select the bounds. Visible focus is marked by an outline |

After the stream check the focus moves to creating a recording. When the recording is complete it moves to opening the fragment. After the first playable frame the confirmation is available. An asynchronous step moves the focus only if it stayed on the original button.

## UI Kit quality and hypotheses for the two routes

Each component has typed input states, permitted actions and an expected result for the keyboard and the accessibility tree. For example, for a confirmed gap the timeline needs the interval bounds, for estimated time the available uncertainty information. Both screens use one component contract. In review, a change of a shared state is checked on both routes, including a delayed response after the point changes. The composition and contrast criteria are given below.

| Reference | What we bring into Bacuri | Acceptance criterion |
| --- | --- | --- |
| [IBM Carbon, typography roles](https://carbondesignsystem.com/elements/typography/type-sets/) and [semantic colors](https://carbondesignsystem.com/elements/color/usage/) | The purpose of a token determines its use. The point heading, the profile label, the time and the main action have a stable hierarchy. Both themes use the Bacuri palette | On two screens, compare the text roles, the wrapping of a long name, the width of time digits and the visibility of the main action at 200% zoom. The frame and a critical alarm must stand out among regular feed updates |
| [Adobe React Aria, slider](https://react-aria.adobe.com/Slider) and [WAI-ARIA APG, slider](https://www.w3.org/WAI/ARIA/apg/patterns/slider/) | Start with native buttons, fields and the slider. For complex control consider a React Aria primitive when it solves a specific input or accessibility problem. The time, gap bounds and labels belong to Bacuri | On the timeline press an arrow, Home and End. Check the selected moment, the accessible name, a clear time value and the focus position. When the primitive is replaced, repeat these actions in both browsers |
| [Storybook, state examples](https://storybook.js.org/docs/writing-stories) | The local catalog for developers uses the same React components and fixtures as the test screens. A variant has a clear purpose, example data and a description of the user action | From a clean state open the examples of waiting, an empty result, an error and recovery. Then assemble the camera form and the feed with the player from the same components and check them on full screens. The catalog is built separately from the delivered client |

For the review of the first domain component take the archive timeline. In the example with a confirmed gap, record the input bounds of the interval, the accessible description, the result of moving the cursor and the permitted action. A change of a public property is checked at the call sites on the screens. A new visual variant is added when it has a distinguishable purpose in a scenario.

| Benefit hypothesis | What the solution looks like | What the automation checks |
| --- | --- | --- |
| The administrator can tell a healthy connection from a usable recording | The form shows the result of the stream check and the result of the archive read separately. The confirmation becomes available after the first decoded archive frame of the selected point | Feed healthy live video and delay the archive. The confirmation action is unavailable until the frame. Change the point while waiting and deliver the old response. It must not confirm the recording of the new point |
| The operator can explain why a frame is or is not available at the time of an alarm | The player and the timeline consistently show a recording, a pending catalog and a known gap. For an estimated time binding, the uncertainty is visible next to the mark when it is known | Reproduce all these cases on one point. Compare the frame or the gap bounds, the text, the timeline pattern and the available action. A neighboring frame in place of a gap is a critical error |
| The operator can analyze an episode without searching for the alarm again | From the selected card the archive opens with one action. Returning with one action restores the same card, the feed position and the focus | While the archive is open, add a new alarm to the feed. After the return check the identifier of the original card, its visibility and the focus, including keyboard control |

For a new interaction variant, record the expected improvement over the current interface and a sign of degradation. On the same data compare the number of required actions, repeated input and keeping the context. A variant that loses the point, confirms falsely or makes an action unavailable goes back for rework. Independence, the human time and the understanding of the result are assessed in the final series by the [first-use tasks](#first-use-check).

The search and filter panel is the same in the device catalog, the alarm feed, the logs and the "Access" subsections. With local search the list changes while typing. In the activity journal, the magnifier in the search field or Enter sends the request to the server. The cross resets the request and the list filters, the circular arrow loads current data with the conditions kept. In the alarm feed choose "New", "In progress" or "Closed". Pressing the active state again shows the alarms of all states.

### Component contracts of the administrator and the operator

| Component | State and available action | Keyboard, focus and result |
| --- | --- | --- |
| Navigation and point selection | The menu marks the selected route. Above the video, next to the point selection, the connection and recording states are visible. The transition to the archive keeps the original alarm | Tab reaches the links, Enter opens a route. On the return the focus and the feed position are restored on the original card |
| Camera form | The mandatory fields of the main and secondary profiles have permanent labels. The states include input, checking, a field or connection error and a successful check of two streams | Enter submits the form once. An input error is linked to the field and moves the focus to the first invalid field. On a lost connection the values are kept |
| Recording status and action | The connection, the recording of a fragment and the first decoded archive frame have different texts. The confirmation is available after a frame of the selected point | Buttons are reachable by Tab and work with Enter and Space. A status change is announced. An asynchronous result moves the focus to the next action only while the focus is on the original one |
| Player | Live video, the archive, loading, a frame, a known gap and a delivery refusal have separate views. On a break, recovery is available. On permission revocation the previous frame is hidden | Mode switching and playback are available from the keyboard. The player gets an explicit position from the scenario. Substituting a neighboring frame for a gap is an error |
| Alarm card | The point, the time, the type and the state are readable as text. Priority information is common for the feed. The transition from an alarm opens its interval | The action has an accessible name. Background events do not take the focus and do not move the selected alarm |
| Archive timeline | Selection of a moment in UTC, the information source, the interval bounds and a refresh after a failure | The native slider announces the moment and the state, the arrows change the moment by a second. Home and End select the first and last available milliseconds of the window |

In "Monitoring" the timeline is available in the "Archive" subsection. While the handle is dragged, the UTC reading and the interval description change immediately. The player opens the selected moment after the handle or the movement key is released. Switching between "Live" and "Archive" ends the previous viewing.

#### Archive timeline in the UI Kit

The component receives a window and non-overlapping intervals in UTC milliseconds, the selected position and the selection and refresh handlers. The owner of the scenario passes a non-empty window and a position inside it. The start of an interval is included, the end is excluded. While the position is between known intervals, "No information" is shown. The component reports the selected moment to the caller. Getting the frame and confirming the recording belong to the scenario and the player.

A solid bar means a recording, a diagonal a confirmed gap, vertical strokes estimated time, dots cataloging, horizontal strokes unavailable media or another delivery refusal. The text next to the cursor gives the reason for a gap or unavailability and the bounds. For estimated time the uncertainty or a message that it is unknown is visible. Across midnight both calendar bounds are visible. Loading, an empty result and a catalog error have separate examples. On a refresh after an error the focus stays on the button, and the result is announced through the status area.

The local catalog is started with `task ui-kit:up` and is available at [http://127.0.0.1:6010](http://127.0.0.1:6010). It stops with `task ui-kit:down`. `task ui-kit:check` builds the library and the catalog in isolation, goes through the examples in Chromium and Firefox in both themes and keeps the results in the report directory. [Component](../web/client/src/ui-kit/ArchiveTimeline.tsx) · [State examples](../web/client/stories/ArchiveTimeline.stories.tsx) · [Browser checks](../web/client/tests/archive-timeline.spec.mjs)

#### Camera connection form in the UI Kit

The administrator enters the name, the main stream address for the archive and the secondary stream address for live video. The fields have permanent labels. Enter submits the form. On an error the first invalid field gets the focus, and its accessible description contains the purpose and the fix text. The form checks the RTSP and RTSPS syntax. An address with credentials, a query or a fragment is rejected before sending. Checking device availability, certificate trust and permissions for open RTSP belongs to the server. [Camera contract](../contracts/v1/openapi.yaml) · [Transport policy](adr/012-camera-capabilities.md)

The form receives the current values, the check state and the action handlers. The owner of the scenario keeps the draft and allows one active check. Editing resets the previous result and cancels the request. A late response from a dropped attempt is ignored. On integration the screen must create a separate instance of the scenario by the identifier of the selected point. After a break both addresses are kept for a retry. With a partial success each profile shows its own result. The recording and playback check follows on the administrator screen after the stream check.

The examples use deterministic responses without a camera connection. The automatic tests check keyboard input, a repeated submission, fixing the address during a request, independent profile results, accessibility and the layout at a width of 320 pixels and 200% text. [Form](../web/client/src/ui-kit/CameraConnectionForm.tsx) · [Check scenario](../web/client/src/workflows/useCameraConnectionCheck.ts) · [Storybook examples](../web/client/stories/CameraConnectionForm.stories.tsx) · [Browser tests](../web/client/tests/camera-connection.spec.mjs)

## Design review of whole screens

The automatic tests check the camera entry screen and the operator workplace in the context of a task, as well as the map, the plan, alarm handling, archive capacity and export. For each they keep full captures at widths of 1440, 768, 390 and 320 CSS pixels, and for asynchronous transitions the sequence of states before the response, after it and after a retry. The series includes real long point names, time with a time zone, a non-empty feed and a diagnostic error. For each theme and width the screen is compared with the saved image of the working client.

The parameters of the working client are body text of 1 rem with a line height of 1.45, work panel headings of 1.0625 rem and panel metadata of 0.875 rem. The form and the player use one spacing scale. To change these values, the reviewer compares the wrapping of labels, the visibility of the action and the readability of the state on the same full screens. [Workplace styles](../web/client/src/app/workspace.css)

| Review subject | What must be visible and how to check |
| --- | --- |
| Hierarchy and composition | On the camera screen the point, the purpose of both profiles, the next step and the playback result are readable. In the workplace the main object is the video of the selected point, and next to it the mode, the time, the timeline and the selected alarm are distinguishable. On a narrow screen the card and the actions are available in the natural reading order. On the captures check for overlaps, cut-off actions and horizontal page scrolling |
| Typography | The UI Kit sets the styles of the screen heading, a group, body text, a label and time. One level has the same size, weight and spacing on both screens. Units, the time zone and the uncertainty are readable next to the value. Time digits keep their width on updates. Check the wrapping of long labels and text zoom to 200% |
| UI Kit and semantics | Common tokens set the surfaces, spacing, text, action accent, warning, error and focus. The form, the button, the stream status, the player, the timeline and the alarm card use common variants and terms. On a pair of screens compare the same actions and states. Color is accompanied by text, and a gap and uncertainty on the timeline are also distinguishable by pattern and label |
| Accessibility | From a clean sign-in, go through both tasks with the keyboard in Chrome and Firefox. Check the visible focus order, the field and button names in the accessibility tree, the accessibility of an error and the announcements of a status change for a screen reader. On the return from the archive the focus is on the original card again. Measure a contrast of at least 4.5:1 for normal text and 3:1 for large text. Check that labels and actions are kept at a width of 320 CSS pixels. [WCAG contrast](https://www.w3.org/WAI/WCAG22/Understanding/contrast-minimum.html) · [Visible focus](https://www.w3.org/WAI/WCAG22/Understanding/focus-visible.html) · [Reflow](https://www.w3.org/WAI/WCAG22/Understanding/reflow.html) |

| State | How to cause it on the working client | What the reviewer accepts on the whole screen |
| --- | --- | --- |
| Loading and waiting | Delay getting the list, then the registration of an already recorded block | The running operation is visible. Waiting for the catalog differs from an empty archive. A repeated check opens the fragment of the same point that appeared |
| Empty data | Open a site without cameras, then a permitted point without alarms | The text names the empty set. The administrator can add a camera, the operator keeps live video of the own point |
| Error and recovery | Enter a wrong address, break the connection, revoke the permission during viewing | For a fixable input the focus and the message belong to the field, and the other form data is kept. On a break the state and the recovery action are visible. On revocation delivery stops and the forbidden information disappears |
| Incomplete archive | Prepare a known gap, uncertain coverage and an episode with an estimated time | The reason for a confirmed gap, unknown coverage and the time uncertainty are distinguishable. For a point with a recording the actual frame, the position and the "Archive" mode are visible |

The browser automation records the revision, the full captures and the differences from the reference, the contrast, the accessibility tree and the focus order with keyboard input. A difference from the reference requires a fix or a justified reference update with a repeated run. The reviewer assesses the visual hierarchy on the resulting screens. The clarity of messages, including those read by a screen reader, is checked in manual acceptance. [Design system decision](adr/007-ui-states.md)

## Verification tasks

In Storybook the scenarios use reproducible states of the working client. On the stand the same actions are checked with real point identifiers and decoded frames. [Browser scenarios](../web/client/tests/screens.spec.mjs) · [Scenarios with services](../tests/workspace/scenarios.py)

| Role | Actions | Observed result |
| --- | --- | --- |
| Administrator | In the "Camera connection error" scenario fill in the name and the addresses of both streams, connect the camera and repeat the check | The values are kept, after a successful check the focus moves to "Create a test recording" |
| Administrator | Create a test recording, open it, confirm the shown frame, then change the parameters | The confirmation is available after decoding. After the parameters change a new check is required |
| Operator | Open the motion alarm of 14:21:22, which has a recording | The archive frame of the selected point is shown, the focus moves to the player |
| Operator | Open the motion alarm of 14:22:12, which falls into a recording gap, from the keyboard and press "Back to the selected alarm" | A message about the recording gap is visible. On the return the focus moves to the same card |
| Operator | Open the motion alarm of 14:25:01, whose block is still cataloged, then check the "Empty feed" screen | The states of a recording waiting for cataloging and of an empty feed have separate messages |

## Observation protocol

The sheet is filled in only in manual acceptance. Give the role, the scenario, the browser, the screen width, the input method, the time, the errors, the requests for help and the result. Write down verbatim how the participant distinguished the connection, the recording, playback, a gap and a lost connection.

| Participant, role, scenario | Revision, browser, screen, input | Time and limit | Errors and hints | Participant's answer and comparison with the reference | Fix and repeat |
| --- | --- | --- | --- | --- | --- |
| | | | | | |

The observer also keeps unfinished attempts and explanations that differ from the reference.

## First-use check

Invite administrators with experience connecting surveillance cameras and operators on duty who work with Bacuri for the first time. Give the sign-in address, the credentials, the site data and the goal in plain words. An example for the administrator is "Connect the camera at the gate and show how you made sure the recording can be watched". For the operator - "Show what happened at the gate at the signal, and return to monitoring the alarms". The observer resets the data and the browser profile before each attempt, records the actions, stays silent and after completion asks to explain the result. Documentation and demonstrating the order of actions are excluded from the task. Any hint or intervention is recorded as a non-independent completion.

| Task | Start and measurement boundary | Success and critical error | Limit and basis |
| --- | --- | --- | --- |
| Administrator, first recording | From giving the goal on the sign-in screen. The camera is available, the point is not created yet, the addresses and access parameters are known. Stop the clock after the result is demonstrated | Connected both profiles independently, played a new archive frame of the own point and said why this confirms the recording. A critical error is taking live video or a successful connection check for a recording confirmation | 5 minutes to enter the data and check the new recording |
| Operator, alarm | From giving the goal on the sign-in screen. The point has an archive with a reference, one observation is fed automatically at the first live frame. The end is the return to the original card after the explanation | The participant opened live video, found one alarm, showed the frame of the right point and time, explained the result and returned to the same alarm. Critical errors are taking a neighboring episode for the one sought, taking a gap for a recording or seeing data outside the access scope | 2 minutes for the initial assessment of one alarm before returning to duty |
| Administrator variant, wrong address | From the input error message to playback. Correct data for the fix is given | Found the cause, fixed the address and confirmed the new recording independently. Creating the same camera again or a false confirmation is a critical error | 3 minutes to fix one error and check the recording again |
| Operator variant, known gap | From showing the next alarm with a gap interval to the explanation and the return | Correctly named the point, the interval and the reason the recording is missing. Substituting a neighboring frame for the gap or presenting a guess as the reason is a critical error | 1 minute to explain a known gap during duty |
| Administrator, discovery and replacement | From sign-in with an available ONVIF camera to opening the previous and the new frame of the point after the replacement | The device is bound to the right point, the previous archive is available. An error is binding another camera or losing the old archive | 10 minutes to maintain one point, including the check of both bindings |
| Operator, investigation from the plan | From the sign-in on the site to alarm handling, export and offline verification | The right point and episode are found, the available PTZ function is checked, the alarm is closed with history, the export integrity is confirmed. An error is closing someone else's alarm, exporting another interval or accepting damaged material | 10 minutes for one episode from the plan up to the offline verification of the export |
| Administrator, node maintenance | From a clean node and a signed kit to installation, upgrade and recovery by the delivered guide | The permissions, the retention, the previous and the new frame are checked. An error is losing the archive available before the failure contrary to the profile or opening forbidden material | Installation to the first permitted frame - 30 minutes. Upgrade from the start of the operation to the check of the previous frame and the permissions - 15 minutes. Recovery from presenting the failure to the check of the permissions, the old and the new frame - 30 minutes. These are separate maintenance windows of one local node with a ready package and backup |
| Full release administrator, camera group and a disconnected source | From choosing the group to checking the template result and the recording after the source recovers | The cameras with a partial failure and their actual settings are named. A new archive frame of the restored camera is opened | 12 minutes for the batch, the fix and the recording check. [Task conditions](release-profile.md#final-tasks) |
| Full release operator, an episode on several cameras of the site | From a video alarm to checking the protected selection and releasing a permitted copy | The right cameras and intervals are chosen, gaps and access restrictions are visible, the masks and audio tracks match the permissions | 12 minutes to search, protect and release a copy. [Task conditions](release-profile.md#final-tasks) |

The limits include the time of the system and of the user. For maintenance the clock starts again before each operation. In qualification the automation checks the completeness of the profile and the technical result. The human time is measured in manual acceptance, including sign-in and waiting for the system. [Numerical limits](../tests/cameras/qualification.json)

After the automatic qualification, on the same kit involve at least five new administrators and five new operators. Both groups cover both target browsers. The first task is performed at the first sign-in, then the participant goes through the additional tasks of the role and a failure variant. Include performing a key task with the keyboard and a screen reader. The administrator separately goes through maintenance by the delivered guide. For acceptance each participant reaches the result independently within the fixed time and explains it correctly, with no critical errors. On a defect keep the original protocol, add an automatic regression test, repeat the qualification on the fixed kit and the affected final tasks. For a repeated first-use check involve new participants. [ADR-008](adr/008-first-use.md)

## Browser checks

`task ui-kit:check` compares the icons with the source SVGs, builds the catalog and runs Playwright in the light and dark themes of Chromium and Firefox. The monitoring and camera connection screens are compared with 32 references at widths of 1440, 768, 390 and 320 CSS pixels. Live video stops on a fixed frame. The tests check accessibility with axe-core, keyboard transitions, styling and the number of columns. The path to the report with captures and diagnostics is printed at the start. [Screen tests](../web/client/tests/screens.spec.mjs) · [Reference captures](../tests/workspace/screens)

After a design change, rebuild the catalog in the web client image and update the references of the affected routes. The commands below update the monitoring and camera connection captures. Review the PNG changes and repeat `task ui-kit:check`.

```sh
task api:types
docker compose build ui-kit-check
mkdir -p build/ui-kit
BACURI_WORKSPACE_SCREEN_MODE=rw docker compose run --rm --no-deps -T --user "$(id -u):$(id -g)" ui-kit-check sh -ec 'npm run catalog && npm exec -- playwright test --grep "Full workspace screen" --update-snapshots=all'
```

## Camera materials

The frame was created by the built-in ImageGen. [Frame](../design/bacuri/assets/yard.webp) · [Archive fragment](../web/client/stories/yard.webm) · [Live fragment](../web/client/stories/yard-live.webm)

ImageGen prompt

> Photorealistic natural security camera still used as the demo camera feed inside an industrial video management dashboard. Wide horizontal 16:9 frame. Elevated fixed outdoor CCTV viewpoint from a corner of a mid-sized logistics warehouse, about 6 meters high, looking diagonally down over a loading yard. On the right, a long cool gray corrugated metal warehouse wall and 3 loading bays with dark rubber dock shelters; a small white delivery box truck reversing toward the nearest bay, small yellow safety bollards. On the left asphalt with yellow lane markings and a pedestrian zebra crossing, dark perimeter fence and sparse northern green trees far away. A single tiny high-visibility worker seen at a distance by the dock. Quiet overcast daylight, subdued blue-gray and moss tones, balanced muted saturation, realistic slightly compressed surveillance sensor texture, plausible logistics yard geometry. Leave the top center and the lower corners visually quiet for small UI overlays rendered later. Full-bleed clean image with no captions, interface, time stamps, signage letters, number plates, or logos. Documentary, not cinematic or stylized.

## Work scenario review

Status of the changes - Ready for acceptance.

| Problem in the scenario | Fix and check method |
| --- | --- |
| When the session check failed, the client showed the sign-in form | At the first opening of an unavailable site, the reason and a retry button are shown. After the connection returns, the client checks the existing session again. At the initial key enrollment the focus moves to the code field |
| When live video was compared with the recording, the handling context was lost | Open an alarm, enter a result and switch "Archive" -> "Live" -> "Archive". The selected alarm, the moment and the draft stay on the screen. The camera connection and the recording state are shown separately |
| On a phone the focus return could leave the card below the visible area | When the feed is below the video, the "To alarms" button scrolls the page to the feed and puts the focus on its heading. A state can be chosen, an event opened and the return made with "Back to the selected alarm". The focus and the scroll return to the event. If its state no longer matches the filter, the full feed opens |
| An empty player took most of the screen before a test recording was created | The wizard shows the sequence of the stream check, the recording and the frame. The player appears when the fragment opens. After the profiles are chosen the focus moves to the connection form, after a cancel to the search button |
| The log required going through monitoring, and there was no way back from the next page of results | The log opens from the main navigation. Search supports resetting the fields and returning to the previous page with the same conditions |
| When the export interval was fixed, the text of the previous error stayed | Open the export, change the bounds after a refusal and repeat. The error resets on editing. After a successful preparation with the keyboard the focus moves from the button to the download |
| The start screen loaded the archive playback module and exceeded the size limit | The module loads with the manifest when an archive recording opens. The initial load of monitoring and the camera form is checked separately in each browser |
| On the plan the zoom control pushed the image aside, and the calibration link had a small hit area | Zoom 100% or 200% is chosen next to the plan name. In the georeference editor zoom up to 400% is also available. In the calibration form the live button opens a new tab |

On the separate live, archive and alarm pages the "Workplace" item returns to the main client. The menu uses the common width threshold and the saved state of the side panel. In archive search the heading shows the site and the point of the result. An empty interval comes with a message.

The check on the software stand covered 120 browser tests in Chromium and Firefox, the light and dark themes, widths of 320, 390, 768 and 1440 CSS pixels. For the monitoring and camera routes the initial JS and CSS took 401104 bytes with a limit of 409600. The median time to an available action over three openings was 134 and 153 ms in Chrome and 154 and 179 ms in Firefox for cameras and monitoring respectively. The time a person needs for a task is measured separately.

[Browser scenarios and visual references](../web/client/tests/screens.spec.mjs) · [Test method with operators and administrators](#verification-tasks)

## Review against the product principles

Status - Accepted. The screens were compared with the [interface principles](product-principles.md#interface) on the reference captures of monitoring, the plan, the camera connection and the device catalog. After the fix is implemented, the rules of this document that contradict it are replaced: UTC time in the alarm card, the timeline only in the "Archive" subsection, ending viewing when switching between "Live" and "Archive", plan zoom with the 100% and 200% buttons.

Kept unchanged: the tokens and both themes, the contrast check, the reference captures from 320 to 1440 CSS pixels in two browsers, keyboard accessibility and the separation of the connection, recording and playback states.

| Screen | Discrepancy | Target solution |
| --- | --- | --- |
| Common | The layout repeats the template of an administration panel: a side menu, a header, cards in cards. The operator and the administrator use one navigation | A separate operator console and separate administrator settings. Grouping without nested frames |
| Monitoring | The video of one point takes about a quarter of a 1440x900 screen, with an empty area under the player | The video takes at least 70% of the area, panels collapse |
| Monitoring | One state is shown by four labels: "Camera connected", "Recording in progress", "Live video", "Live video connected" | One composite indicator with the age of the last recorded frame |
| Monitoring | The point is chosen with a native list | Selection through a command line, a grid, a plan and the keyboard |
| Monitoring | Live video shows a `0:00:01.000` counter instead of the time | The site time in live video and in the archive |
| Monitoring | "Live" and "Archive" are different menu items | One timeline, with live video as its right edge |
| Alarm feed | All alarms have the same "New" mark, importance is not distinguishable | Importance is distinguishable by the position, shape and color of the mark |
| Alarm feed | Time is shown in UTC in a monospaced font | The site time and relative time, UTC in the details |
| Alarm feed | Search and the state switch take the top of the feed | Filters are collapsed, the queue is processed from the keyboard |
| Camera connection | Three menu levels: "Cameras" -> "Connection" -> "Manual", "ONVIF discovery", "Batch" | One "Add cameras" action from the catalog with discovery by default |
| Camera connection | RTSP address entry is open by default | Discovery by default, address entry as a fallback |
| Camera connection | The "Connection check" block is a static instruction | A live list of checks filled in by the actual results |
| Device catalog | An empty catalog says "No connected devices yet." without an action | An empty catalog starts discovery and offers to add cameras |
| Plan | The plan takes a small part of the screen, and the right panel mixes operator and administrator actions | The plan takes the main area, the plan settings move to the administrator space |
| Plan | Zoom is chosen with the 100% and 200% buttons | Zoom by scrolling and gestures |
| Plan | Native expandable blocks and lists differ from the visual system | Common UI Kit components |

## Work screens

On a site with a local session the administrator opens `/cameras`, the operator `/monitor`. The Storybook catalog has the same components with reproducible states. The automatic design gate compares full screens with the reference captures for each browser and theme. Separate assertions check the colors, the typeface, the size of the work panel headings and the number of columns at each control width.

| Action | Result and basis for display |
| --- | --- |
| Connect a camera | The request has an idempotency key, a change of a previously created camera uses the current ETag. The applied revision must match the sent one. The main profile confirms a new full block in the manifest, the secondary one a decoded WebRTC frame. When a profile check did not complete, the profile has the "Not confirmed" state. |
| Check the recording | Continuous recording already works. The button waits for a full block recorded after the press. The next button opens the exact interval of the block through MSE. The confirmation is available after decoding and is withdrawn on a loss of access or a change of parameters. |
| Open an alarm | One action on the card opens the archive. The server sets the point, the interval and the UTC. The existence of an alarm by itself does not confirm the existence of video. While cataloging, a repeated check is available, on a gap the reason is visible. The common label "Priority not set" applies to all events of the feed. |
| Return to the feed | The URL keeps the point, the interval bounds, the UTC, the card identifier and the scroll position. The return moves the focus to the available card. Following a saved address checks the session and the permissions again. |

The operator opens the archive with one press on the card. The administrator has four actions after entering the parameters - connect the camera, create a test recording, open it and confirm the frame. The effect on independence and human time is checked in manual acceptance. [Verification protocol](test-profile.md#single-web-client-acceptance)

## Camera control and sound

In "Monitoring" -> "Live" choose a point. The console is under the frame. A touch of an arrow turns the camera by one step. Holding repeats the steps after the camera confirms, releasing stops the movement. The central key with a square stops pan and zoom. The pair of keys with a magnifier controls zoom, the pair with a focus mark moves the focus nearer or farther. With the keyboard focus on the console, the arrows, the plus and minus keys and Escape for a stop work. To go to a saved preset, press its name. A patrol route has its own start and stop buttons. The button in the header expands the frame with the console to full screen.

The equipment icon opens the illuminator, the wiper, the relays and the digital inputs. The clock icon opens the log. The console shows the actions available for the selected camera. Hover over a button for a tooltip. The permitted audio channels are in the top row of the console.

The "Stop" button stops the supported axes. Focus and patrol have their own stop commands. While ownership applies, another operator sees that control is taken, and the administrator can take over. When a response is lost, the command log shows "Outcome unknown". Check the image or the input reading before a new command. The "Activity journal" keeps the dispatch, the response and the refusals.

"Listen" turns on the camera sound. "Record audio" keeps the incoming sound together with the video until the recording stops or the audio recording session ends. In "Archive" sound plays from the selected time when there is a permission and a recording. "Talk" passes the browser microphone signal to the camera output for up to 30 seconds. "Turn off the microphone" ends the speech transmission. Listening and recording are controlled independently. When the screen is left, the browser releases the microphone.

## Region map and plans

The web client uses WebGL 2 to display the region map. In the "Maps and plans" section the administrator chooses a surface and an existing observation point, opens "Change the point position", marks the place with a click or enters the coordinates from the keyboard, then presses "Save the position". The map uses WGS 84 longitude and latitude, a plan uses fractions of the width and height from the top left corner. One point can have a position on the map and on several plans.

The "Add a plan" menu sets the name and a PNG image. The maximum for one file is 4 MiB, 4096 pixels per side and 8,388,608 pixels in total. The image limit of the site is 16 MiB. The server checks the signature, the dimensions and the decoding of the image and drops the auxiliary PNG chunks. Images and coordinates are part of the configuration backup.

In the "Maps and plans" section choose "Region map" or "Plans". When several plans are available, the needed one is chosen in the "Plan" field. The operator sees the plans with at least one point available to the operator. The same check applies to a direct link to an image. After a point is chosen, live video, the archive or a specific alarm can be opened. The "Maps and plans" link in the navigation returns the selected surface, the position and zoom of the map or the scroll of the plan. The focus returns to the selected point.

### Checking an event on the map

With the feed connected, the number next to a camera shows the new alarms of this point in the current feed. On the building overview the cameras of its floors are counted. Choose a camera and open "Camera events". The latest events by observation time are at the top, and each row gives the UTC time and the known handling state. "Show more" expands earlier events from the same feed.

Press "Check" to watch the recording at the time of the event right on the map. The "Archive" and "Live" switches let you compare the recording with what is happening now. When the recording is still being added to the catalog, the panel waits for it and opens it automatically. With a recording gap or uncertain time the operator sees the reason. New alarms enter the list, and the current view keeps showing the selected event. "Open the alarm in monitoring" leads to the archive and the handling card. On the return the camera, the plan and the selected event are kept while it is available in the feed.

### Georeferencing a plan to the map

Open the needed plan and press "Georeference the plan to the map". A standalone plan can be used right after the upload.

1. Choose two landmarks visible on both images, for example opposite corners of a building. Mark A on the plan and the map, then B. From these pairs Bacuri determines the position, the scale and the direction of the plan top. Place the landmarks as far apart as possible.
2. When the real width of the whole image is known, choose "One landmark and a known width". Mark the corresponding point, the width in meters and the azimuth of the up direction of the image. North is 0°, east is 90°. The width includes the margins of the drawing.
3. For the floors of one building give a common building name. Press "Preview" and change the opacity. Check that another recognizable corner or entrance matches. When the image is stretched or it is a diagram without a scale, use the original drawing with the correct proportions.
4. For cameras with an existing geoposition the wizard shows the distance to the proposed position. Choose the source separately for each point and save the georeference. Without an installed region, numeric input and the calculation are available. For visual alignment install a local map.

On the area overview press the building name or choose a floor in the "Plan on the geographic map" list. Changing the floor switches the image and the set of cameras. From the card of a camera with computed coordinates, the original plan can be opened and the point moved on it. The direction of its view on the geographic map takes into account the plan rotation and the last available PTZ measurement. An update of the region maps keeps the georeference.

To return a standalone plan, open "Configure the map georeference", choose "Remove the georeference", check the action and confirm it. The saved manual camera coordinates are available on the geographic map again.

### Cameras and view sectors

The administrator places a point on the map or a plan, selects it and expands "Configure the field of view". Open live video in a neighboring tab. Choose a noticeable landmark at the center of the frame, press "Mark a landmark on the map" and give its position. The direction can be entered manually in degrees clockwise. On the map it is counted from geographic north, on a plan from the top edge of the image.

For a fixed camera enter the horizontal field of view of the lens. For a PTZ camera stop the movement before saving. With angular ONVIF coordinates the reading is bound to the landmark automatically. When the camera returns relative coordinates, give the number of degrees of rotation per pan unit for this model and installation. It can be obtained from two landmarks with a known angular difference: divide the change of direction by the change of the pan reading on a segment without crossing the range boundary. A negative sign corresponds to the reverse direction of the axis.

PTZ uses the main ONVIF profile chosen at connection. The field of view received from the camera has priority. For cameras with normalized zoom, angles at the wide and the tele end of the lens can be set. Between them Bacuri shows a linear estimate. Without zoom data the set approximate angle is used. The sector length on the screen is nominal. Indoors it does not take walls into account, and for an outdoor camera the terrain and the recognition distance.

The operator chooses the camera icon and sees the direction, the angle and the time of the last PTZ measurement. On a lost connection the sector is shown dashed. Without an absolute position or after a device replacement, only the camera icon remains. For a new device repeat the calibration. A pan from another console or a patrol is detected at the next camera poll. After a command from Bacuri the polling is temporarily more frequent.

### Installing and updating regions

Open "Maps and plans" under an administrator account. Press "Install regions". Choose the map source.

- **From the internet.** Give the address `https://.../catalog.json` and press "Show the catalog". The address is saved for the site. The list shows the name, the size and the data date of each region.
- **From a file.** Choose a ZIP on your computer or media. The archive can contain MBTiles in nested directories. A single MBTiles can be chosen as well. Wait for the file to load in the browser and for the list to appear.

Mark the needed regions or "Select all", then press "Install the selected ones". The state of each region is shown separately. After queueing the tab can be closed. On a connection break Core automatically requests the remaining bytes from the position saved on disk. The "Pause the download" button pauses the selected region. "Resume the download" removes the pause. Closing the browser and restarting Core keep the queue, the downloaded data and the pause set by the administrator. Several installed maps are available to the operator in the "Region" list.

To update, press the circular arrow button "Update the installed ones". Press "Check for updates" for the catalog or choose a local archive in the "Update file" field. The list shows only the installed regions for which the source has newer data. When the coverage shrinks, the interface shows the number of cameras outside the new bounds. The active version is available to operators until the new one is installed successfully, and the point coordinates are kept. For a region with an error, "Retry" is available.

### Preparing and importing a region

1. On the preparation machine, get a regional OpenStreetMap extract in the OSM PBF format. Keep the source file together with the delivery package. [Geofabrik extracts](https://download.geofabrik.de/)
2. Open the extract layers in a GIS and set the style and labels. For example, in QGIS use the "Generate XYZ tiles (MBTiles)" algorithm with the PNG format, a tile size of 256 pixels, the needed area and zoom range. The import supports packages up to 8 GiB and zoom levels from 0 to 22. The export result is a raster MBTiles with TMS tile coordinates and a `bounds` field in the order west, south, east, north. [QGIS tile export](https://docs.qgis.org/latest/en/docs/user_manual/processing_algs/qgis/rastertools.html)
3. On a machine with Python and osmium-tool run `python3 tools/map-metadata.py region.osm.pbf region.mbtiles "Region name"`. The command carries over the replication time from the header of the source extract. When preparing the style, follow the license terms of the source data. [OpenStreetMap and ODbL](https://www.openstreetmap.org/copyright)
4. To distribute maps, build a catalog with `python3 tools/map-catalog.py output north.mbtiles south.mbtiles --zip regions.zip`. Put the content of `output` on an HTTPS server with HTTP Range support. The `regions.zip` archive can be passed to an isolated site. The index file contains the region information, the sizes and the SHA-256 of the packages. The publisher sets a stable identifier in the `region_id` MBTiles metadata field. For packages without this field, the exact `name` value is the identifier. Keep the identifier when publishing updates. One region can take up to 8 GiB, the archive and the sum of its unpacked regions up to 32 GiB. [Catalog generator](../tools/map-catalog.py)
5. To import from the local console after the database migration, run `bacuri-vms-core import-map /import/region.mbtiles` with the connection settings of the service and `BACURI_SITE_ID`. For the Compose stand the command is given below.

```sh
docker compose run --rm --no-deps -T \
  -v "$PWD/region.mbtiles:/import/region.mbtiles:ro" \
  core /opt/bacuri/bacuri-vms-core import-map /import/region.mbtiles
```

The import checks the package and publishes it in one transaction. On an error the previous version stays available. On an update the version identifier and the resource addresses change, while the camera coordinates are stored separately. A region is restored by importing the saved MBTiles again.

### Client contract

`GET /api/v1/sites/{siteId}/maps` returns the list of installed regions, the first region for compatibility with early clients, the style address, the plans, the coordinates of the permitted points and the local addresses of fonts and icons. A missing region is `null`. The map style sets local XYZ PNG tiles. The base map labels are already included in the raster tiles. The web client and the future Qt client get the resources from the same HTTPS site with the session taken into account. A placement change goes through `PUT /api/v1/sites/{siteId}/maps/placement`, a plan upload through `POST /api/v1/sites/{siteId}/maps/plans`. Both operations require the administrator role and the CSRF token. [OpenAPI contract](../contracts/v1/openapi.yaml) · [ADR-017](adr/017-map-sources.md)

## Alarm handling and the log

In monitoring an alarm appears when an event from the source is received. The first record of its handling appears in the log when an operator takes the alarm into work. The log shows the action, the time, the actor and the check result. Expand an event to see the state transition, the identifiers and the information about an export or a delegation.

1. In monitoring choose an alarm. Next to the archive frame a card with the state, the owner and the history opens.
2. Press "Take into work". When two operators press the button at the same time, the card shows the one whose acceptance the server received first.
3. Enter the check result and press "Record the result". The owner can refine the result before closing. Each saved edition is visible in the history.
4. Enter the reason and press "Close the alarm". A closed alarm stays in the log. When a response is lost, "Send again" uses the same command key.
5. Open "Activity journal" in the main navigation or "Open the log of this alarm" from the card. In the panel above the records, enter part of the text of the result or the reason. To keep the records of one alarm, give its identifier. Press the magnifier button or Enter. The cross button returns the full list of available records. Time is given in UTC. From a found record you can return to the archive.

In the "Service delegations at this point" section, give the service name, one permitted action and the term in minutes. Copy the token shown at issue and pass it to the service over a protected channel. A command of the service runs on behalf of the user who issued the delegation. Recording the result and closing are available when that user is the owner. To end access before the term, press "Revoke". The card shows the active delegations of the current user on the selected point.

## Fragment export

In "Monitoring" choose a point and open the archive, or go to the archive from an alarm. Expand "Export a fragment", set the start and end in UTC, press "Prepare the package", then "Save the package". The end of the interval is excluded. When coming from an alarm, the recording of its binding and a window of 15 seconds on each side of the alarm moment are available.

The administrator grants the permission to a user who already has access to the point. In the commands, `start_utc` and `end_utc` are the bounds of the permitted UTC interval in RFC 3339 with the `Z` suffix. A repeated grant replaces the previous bounds. During the transfer the core periodically checks the current permissions, the session and the retention of the original blocks. Revocation interrupts an active transfer. A copy saved by the recipient can be read offline. The log shows the user, the point, the bounds, the package identifier, the alarm when it was chosen and the result of the release request.

```sh
docker compose exec -T core /opt/bacuri/bacuri-vms-core grant-export "$user" "$point" "$start_utc" "$end_utc"
docker compose exec -T core /opt/bacuri/bacuri-vms-core revoke-export "$user" "$point"
```

One request can get up to an hour of recording, up to 256 original blocks and up to 256 MiB of media. A whole block is kept in the original MP4. For a part of a block the core selects frames by presentation time and encodes them again in H.264. Each continuous segment is a separate MP4. The description contains the intervals, the reasons of known gaps, the segments with unknown coverage, the identifiers of the point, the camera, the channel, the profile and the block, the time basis and the checksums.

### Offline verification

The administrator passes the public site key to the recipient in advance over a trusted channel. Get the key with the command

```sh
docker compose exec -T core /opt/bacuri/bacuri-vms-core export-public-key > trusted-key.json
```

The verifying machine needs `tar` and `bacuri-vms-verify-export` from a trusted build for the target system. Unpack the package without elevated privileges into a new empty directory. Before viewing, check the package and keep the verified description.

```sh
mkdir episode
tar --no-same-owner --no-same-permissions -xf "$package" -C episode
bacuri-vms-verify-export trusted-key.json episode > verified.json
```

Exit code 0 confirms the Ed25519 signature, the match of the site with the trusted key, the set of files, the SHA-256 of each MP4 and the division of the selected interval. For an automatic comparison with the task, pass three more arguments `POINT_ID BEGIN_UTC_US END_UTC_US`. A mismatch of the point or of any bound ends the check with an error. Open the `clip-*.mp4` files in the order given in `periods`. Time in the description is given in UTC microseconds since the Unix epoch.

## Recording and storage

Open **Cameras -> Recording and storage**. Choose a camera to see the active modes, the policy source and the boundary moments of the available recording. The depth shows the distance between the oldest and the newest available blocks. Gaps inside this interval are visible on the archive timeline.

When the camera inherits a group policy, press the group name in the **Settings source** field to open the common policy. The **Configure separately** button opens the individual camera settings with the active values. Changes take effect after saving.

On the **Groups** tab set a common policy. Expand **Group members** to see which cameras use this policy and which have other settings in effect. Pressing a camera opens its summary. The camera's own policy has priority. Among several groups the policy with the higher priority applies, and with equal priority the smaller policy identifier is chosen. Deleting the own policy returns the group settings to the camera. By default the main stream is recorded continuously.

Continuous recording, manual start, motion and a weekly schedule can be combined. For the schedule set the local time and the time zone. An interval can end on the next day. The hour repeated in autumn enters the schedule twice, the missing spring hour is skipped. One frame is stored once with any number of matching conditions.

For motion recording, set the margin before the event (0-30 s) and after it (1-300 s). The preceding keyframe can be included at the start of the video. Pre-event recording is available for the time the recorder has been receiving the stream. Sound is enabled in the policy separately. Starting manual recording in **Monitoring -> Live** needs the viewing and recording permissions, and with sound enabled also the audio recording permission. The record button starts a 15-minute session, pressing the stop button again ends it early.

The **Volume** tab shows the available space and the recording state. Here the space reserve and the common retention term are set. A camera or a group can have a shorter term. After the term is shortened, increasing the value applies only to the preserved blocks. Setting changes and manual commands are visible in the activity journal.

## Archive retention

The administrator sets the site policy with `bacuri-vms-core set-retention DAYS [RESERVE_PERCENT]`. The term is an integer number of days from 1 to 36500. The free space reserve is a percentage from 1 to 90, 20 by default. The Compose stand applies 30 days and a 20% reserve.

```sh
docker compose exec -T core /opt/bacuri/bacuri-vms-core set-retention 30 20
docker compose exec -T recorder /opt/bacuri/bacuri-vms-recorder archive-status
```

At the first start and after an upgrade, deletion by age starts with an explicit term setting. Shortening the term immediately closes access to the blocks whose end reached the new boundary. Extending the term applies to the preserved archive. The policy is part of the configuration backup.

`archive-status` shows the applied parameters and the volume state. `available_bytes` is the free space available to the recorder, `reserve_bytes` the reserve. With the remaining space within five percentage points above the reserve, `low_space` appears. When the space available for a new block runs out, `recording_stopped` appears, the recorder records a `storage_failure` gap and repeats the write at the next block. State transitions go to the recorder log. On a shortage of space, free space outside the active recordings or extend the volume.

`capacity_depth_seconds` shows how many seconds of recording the volume holds at the measured total stream. The calculation takes into account the space of the active media files and the free space above the reserve. `estimated_depth_seconds` limits the estimate by the set term, `remaining_seconds` shows the time until the reserve is reached at the current recording rate without later cleanup. The rate is measured by the data received over the last five minutes. When there is no data for an estimate in this interval, the forecast is `null`. `cutoff_utc_us` and `removed_blocks` show the last confirmed boundary and the number of blocks removed during the lifetime of the local journal.

## Camera archive recovery download

Open the catalog of connected devices. In the camera card choose "Check the device archive". The client shows the discovered recordings and the gaps of the current binding that can be requested. Choose the needed gap. On an interruption repeat the request. The already published blocks are available on the common timeline. A segment that the camera did not keep or that has no reliable timestamps stays a gap.

## Video rules

In the "Cameras" section open "Video rules", choose a point and a source. For the local detector, select an inclusion or exclusion area on the frame. To set a crossing line, drag its ends. For precise setting from the keyboard, expand "Line coordinates". An available archive frame serves as the background. The brightness threshold and the minimum number of active cells determine the sensitivity. The schedule uses the time zone of the rule. The interval between detections limits a repeated reaction to one movement.

Check the rule on a selected archive interval. The result contains the match times and a warning when part of the recording or the index is missing. The check works with the draft. Save and enable the rule to apply it to the stream. The available reactions include recording by the point policy, an alarm at the post and a move to a camera preset. The alarm card keeps the rule revision and the point binding. The false alarm mark enters the handling history.

## Investigations

In the "Investigations" section choose up to four cameras and set the search interval. In the "Motion" mode the frame area and the threshold are set in the episode filters. There the search can also be limited to plan points, or a fragment of the event topic, the source or the metadata can be entered. The results and thumbnails take the active masks and archive permissions into account.

Choose a thumbnail to go to the moment on all tracks. The common play, pause and frame step buttons control the selected cameras. Each track shows the index state and the precision of the UTC binding. With an unknown uncertainty "Receive time" is shown. On a gap the corresponding track shows that the recording is missing.

The bookmark button adds the exact time and the intervals of the available cameras to the materials. In the "Cases" subsection give the number, the owner, the reason and the protection term. Saving holds the existing blocks and later arrivals for the same interval. Passing a case to a recipient requires permissions for its materials. Opening a bookmark returns the selected points to the saved time. Overlapping holds apply independently.

When releasing a package, choose the original or the derivative and give the reason. Including sound requires a separate permission for each point. The original keeps the source codec. To decode an interval inside a GOP, the original file can include frames before and after the selected bounds. The service checks the permissions for the whole released block, and the signed manifest gives the presentation interval. For a restricted release choose the derivative: it contains the permitted frames of the selected interval, the applied masks and the actual codec. Revoking access stops an active transfer.

Unpack the package into a separate directory. Pass the trusted public key to the verifier over an independent channel. The command checks the signature of the case, the signatures of the individual materials, the sizes and checksums of the video and sound without a connection to the VMS.

```sh
bacuri-vms-verify-export trusted-key.json unpacked-case
```
