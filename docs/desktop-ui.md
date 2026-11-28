# Desktop client screens

## Linux workstation

Run `task desktop:run` from a Linux graphical session. The Docker launcher uses the host’s `xdg-dbus-proxy` for the browser portal and desktop settings. Enter the HTTPS origin of the site and confirm sign-in in the system browser. For a site with a private CA, use `task desktop:run -- --site https://vms.example --ca /workspace/build/site-ca.pem` with the site's trusted CA at that path. After sign-in the workstation lists the observation points returned by the site. Open a point to read its full name and its separate connection and recording states. Refresh reloads the list. Sign out applies to all windows.

Use the overlapping windows icon or Ctrl+N to keep an observation point open while browsing other points in a second window. All windows share the site session and appearance preferences. Each window keeps its own selection and input. Tab and Shift+Tab move focus, the arrow keys select a point, Enter opens it, and the back arrow or Escape returns to the selection.

Open **Settings** to choose the interface language (EN or RU), theme and text size, before or after sign-in. Each setting occupies a separate row. Its switch sits beside the label and moves below it in a narrow window. The monitor, sun and moon select the system, light and dark themes. Text size is shown as a percentage. Tab moves to the selected option in the next switch. Arrow keys change the selection. Hover over an icon or focus it with the keyboard to see its name. Screen readers announce the same names. The first launch uses English and the system theme. Explicit choices are stored in the Linux user's configuration directory.

In the observation point list, press Ctrl+F to search by name or ID. The filter icon opens the connection and recording selectors. The list updates as you type or select a state. The result count and selected criteria stay visible when you collapse the filters. The clear icon resets the search and both selectors. Each window keeps its own filters.

Run `task desktop:catalog` for the separate Qt Quick component catalog. Choose a state, filter the sample points, open a point or retry a failed load. The catalog uses the production panels, controls, status chips and point rows. Their colors, spacing and type sizes come from the shared design tokens. [Qt Quick components](../desktop/src/ui) · [Catalog](../desktop/tools/catalog)

### Workstation verification

Run `task desktop:test` for the Docker suite. The client loads every site in the session, keeps rows, keyboard focus and scroll position during refresh, and searches names and IDs across English and Russian keyboard layouts. The local HTTPS test checks recovery from a failed refresh and signing out from two windows. In the component catalog, **Component preview** opens one control at a time in ready, loading, empty, failure and permission-denied states.

The suite compares every full-window capture with its stored PNG at 100%, 125%, 150% and 200% and on two virtual screens. The accessibility tree and keyboard traversal order must match exactly. Per-channel pixel differences up to 12 are tolerated. At most 0.2% of pixels may exceed this tolerance, with a mean channel error of 0.25 and a local tile limit of 18%. During playback only the moving video rectangle is excluded from the pixel comparison. Its position and size are checked. The EN and RU settings screenshots also appear in the help portal. To replace references for an intentional UI change, run the same shell test in the desktop image with `--update` as the last argument, inspect the changes and rerun the normal suite. [References](../tests/desktop/references) · [Comparison](../tests/desktop/references.py)

An external AT-SPI client opens Settings, changes the language and reads back the selected radio button and localized control names. Two live windows share the session. The latency test measures 100 keystrokes per window up to Qt's frame-swapped signal, with limits of 100 ms at p95 and 150 ms at p99. The two-window playback test verifies focus, unsaved input and advancing timestamps during language and system theme changes. Reports are written to `build/desktop/reports/shell/`. [Window tests](../desktop/tests/shell_test.cpp)

The normal and high-contrast palettes are tested under standard vision, protanopia, deuteranopia and tritanopia. All six pairs of status foregrounds must reach 8 ΔE00. The observed minimum was 11.17 ΔE00 and the lowest simulated status text contrast was 5.35:1. [Contrast contract](../design/bacuri/contrast.csv)

Run `task desktop:shell:stand` for the physical Ubuntu display and reference H.264 recording. At system text scales of 100% and 125%, Xft.dpi was 192 and 240, Qt window DPR was 2 and 2.5, and Qt reported 96 logical DPI and a 9-point font in both cases. The sign-in field stayed at 16 logical pixels and grew from 32 to 40 physical pixels. The client applies the X11 scale once. Transfer between 96 and 192 DPI is tested with virtual screens. Physical transfer requires a second monitor.

## Screen mockups

Mockups of the operator workstation of the desktop client. The screens use the [common color tokens](../design/bacuri/tokens.css) in the dark theme. The interface is in English by default, the EN and RU buttons in the header of each screen switch it to Russian and back, the choice is kept, and `?lang=ru` opens a screen in Russian. Each file is a Design Component source for the design canvas: an HTML page with `<x-dc>` markup and a `DCLogic` class. The local [support.js](desktop-ui/support.js) renders the files without network access: open [index.html](desktop-ui/index.html) in a browser. Links between screens and the command line work, fonts come from the design kit. Video frames are drawn as placeholder scenes. [Русская версия](ru/desktop-ui.md)

| Screen | Content | Source |
| --- | --- | --- |
| Monitoring wall | Cameras with different aspect ratios packed without cropping, an alarm slot that does not move other cells, the event feed, the shared live timeline with an activity strip, the "no connection" and "secondary stream" cell states and a privacy mask | [main.dc.html](desktop-ui/main.dc.html) |
| Camera in focus | PTZ by dragging in the frame, zoom to an area with Shift and a rectangle, pan, tilt and zoom readout, links to neighbor cameras at the frame edges, a strip of layout cameras in a fixed order, presets on keys 1-6 and stream diagnostics | [focus.dc.html](desktop-ui/focus.dc.html) |
| Synchronized archive | Four cameras at one UTC moment, a lagging camera and a recording gap, per-camera timeline tracks from day to frame scale with activity, alarms, bookmarks and a protected interval, the day overview, the keyframe cache and case materials | [archive.dc.html](desktop-ui/archive.dc.html) |
| Alarm handling | The alarm moment replayed from -10 s next to live video of the same camera, neighbor cameras from the plan at the same moment, handling stages, actions with keys and the alarm log | [alarm.dc.html](desktop-ui/alarm.dc.html) |
| Command line | Ctrl+K search over cameras, layouts, alarms and actions within the user's rights, a time in the query opens the archive, keyboard layout correction, arrow keys and Enter | [command.dc.html](desktop-ui/command.dc.html) |
| Plan | View sectors, the sector of a camera with an alarm, the path across camera transitions, a live preview on hover and scale from region to floor | [map.dc.html](desktop-ui/map.dc.html) |
| Cell anatomy | Cell elements, eight cell states, color roles and the radial menu with directional gestures | [anatomy.dc.html](desktop-ui/anatomy.dc.html) |
| Three-monitor workstation | Screen roles, a programmable key panel with labels for the selected camera, a joystick and workstation rules | [workstation.dc.html](desktop-ui/workstation.dc.html) |

Common rules of the screens: video is shown whole and in its original color without effects over the frame, cell positions change only by an operator action, color marks only states and each state also has a shape or a label, every frequent action is available from the keyboard, the command line, the radial menu and the control panel.
