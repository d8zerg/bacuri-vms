# ADR-023. Architecture of Bacuri VMS Client

[Русская версия](../ru/adr/023-client-architecture.md)

**Status** - Accepted.

**Context.** Bacuri VMS Client is the desktop workstation of Bacuri VMS for operators and administrators. It reaches parity with the web client and adds a multi-monitor post, a video wall, a synchronized archive of a whole layout, direct PTZ control, a physical console, spatial navigation between cameras and offline export verification. The measured video and map paths of [ADR-022](022-desktop-video-path.md) and the desktop session of [ADR-001](001-api-contracts.md#local-session-and-access-rules) are its first parts. The client runs for weeks without a restart, shows up to 16 and more video streams next to a map and must stay responsive under load, so its structure has to keep the threads, the state and the visual system under control from the start.

**Decision.** The client is a C++23 application on Qt 6 Quick. C++ owns the state, the media, the network and the work. QML owns the presentation. The code is split into layers and modules with declared dependencies, like the Core modules of the site.

| Layer | Modules | Responsibility |
| --- | --- | --- |
| Application | `app` | Windows per screen, navigation between workspaces, the command registry, saving and restoring the post, the language and the theme. The Help action opens the site's public HTTPS portal in the system browser with the client language and theme |
| Features | `live`, `archive`, `alerts`, `maps`, `investigations`, `export`, `administration` | One module per work area of the parity register. C++ models with typed properties and QML views. A feature depends on core services and never on another feature: it opens another area through a navigation intent of the application |
| Core services | `site`, `media`, `map`, `input`, `settings`, `diagnostics` | The API client, the session, the event feed and the error catalog of the site. Video streams, decoder choice, fallback, shared subscriptions and the frame cache. The map view and the gateway for resources of the site. Joysticks and key panels. Settings per user and site. Logs, metrics and frame timing |
| Design system | `ui` | Tokens, components and scene graph items of the visual system as the QML module `Bacuri.Vms.Client.Ui` with its own catalog and screenshot tests |

Each module is a library under `desktop/src/<module>` with its own tests. Tools that measure or check the client, such as the video and map bench and the sign-in check, live under `desktop/tools` and use the same libraries.

| Rule | Content |
| --- | --- |
| Threads | The GUI thread runs QML and models only. The render thread draws the scene graph, video textures and the map. Each media pipeline and its decoder run on their own threads, network requests are asynchronous. Nothing blocks the GUI thread. Threads exchange data through queued signals and bounded queues that drop stale frames and count them |
| State | Each object of the site has one owner in the core services: a cache keyed by identifier with its version or ETag, updated from responses and the event feed. Features keep only view state. The post state, such as windows on screens, layouts, open alarms, archive positions and focus, is one serializable structure saved atomically and restored with a fresh permission check |
| Commands | Every user action is a command with an identifier, a name in both languages, a key binding, an icon and an availability rule from permissions and site capabilities. Menus, the command line, the radial menu, key bindings and the physical console call the same command registry |
| Contracts | C++ types and JSON codecs of the API are generated from the OpenAPI contract. Error codes, retry classes and messages come from the error catalog shared with the web client. A function that needs a newer site checks the API version and the capabilities of the site and hides itself on an older one |
| Language | English and Russian messages are kept by key like the web client. Switching the language changes all windows without a restart and without losing input, focus or playback |
| Failures | Every state that the person sees has a cause from the error catalog or a local cause of the client. The client never closes a window because of a failure of the network, a camera, a decoder or the graphics device: it degrades and recovers as described in ADR-022 |

**User interface architecture.**

| Part | Decision |
| --- | --- |
| Visual system | The tokens of `design/bacuri/tokens.css` are generated into a QML singleton: colors of both themes, the type scale, spacing, radii, line widths, motion durations and spring parameters. The web and desktop clients therefore share one palette and one set of state colors |
| Components | Small QML components with an explicit API for panels, fields, buttons, chips, tables and dialogs. A component has states for loading, empty data, failure and lack of permission, a visible keyboard focus and an accessible name |
| Heavy visuals | The video grid, the timeline, overlays in the frame, view sectors and the spatial scene are C++ items of the scene graph with shaders, not hundreds of QML objects per frame |
| Interaction | The workstation principles are requirements to every component: the image is shown whole and in its original color, cell positions change only by an action of the operator, input has priority over video, color marks states and is backed by shape or text, motion is short and can be turned off, every frequent action is reachable without searching for a button |
| Screens | The screens follow the [mockups of the operator workstation](../desktop-ui.md): the monitoring wall, the camera in focus, the synchronized archive, alarm handling, the command line, the plan, the cell anatomy and the three-monitor post |
| Accessibility | Keyboard work for every scenario, roles and names for screen readers, text scaling, per-monitor DPI and a high contrast check of both themes |

**Naming.** The product is Bacuri VMS Client, the executable `bacuri-vms-client`, the C++ namespace `bacuri::vms::client`, the QML modules `Bacuri.Vms.Client.*` and the libraries `bacuri-vms-client-<module>`.

**Rationale and consequences.** Separating features from core services keeps one owner for each object and one path for each kind of work, so two windows never show different versions of the same alarm and a slow camera never blocks the interface. A command registry gives the keyboard, the console and the menus one behavior and one place to check permissions. Generating API types and tokens removes hand-kept copies of the contract and of the palette. The structure costs explicit module boundaries and generated code, which pay off once the parity scenarios and the workstation functions grow. [Core module boundary](018-core-module-boundary.md) · [Error catalog](020-error-catalog.md) · [Video and map path](022-desktop-video-path.md)

**Verification.** The build links every module only with its declared dependencies, a feature that includes another feature fails the architecture check. Each module keeps at least 75% line coverage from its tests. Components are checked by screenshots in both themes and languages, by keyboard scenarios and by accessibility names. The bench and the stand checks measure the frame time, the frame path and the recovery after failures on the stations of the passport.
