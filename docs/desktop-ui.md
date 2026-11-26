# Desktop client screens

Mockups of the operator workstation of the desktop client. The screens use the [common color tokens](../design/bacuri/tokens.css) in the dark theme and show the Russian interface. Each file is a Design Component source for the design canvas: an HTML page with `<x-dc>` markup and a `DCLogic` class. The local [support.js](desktop-ui/support.js) renders the files without network access: open [index.html](desktop-ui/index.html) in a browser. Links between screens and the command line work, fonts come from the design kit. Video frames are drawn as placeholder scenes. [Русская версия](ru/desktop-ui.md)

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
