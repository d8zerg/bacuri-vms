# Bacuri VMS color and icons

[Русская версия](README.ru.md)

A proposal for design review. Open the [theme comparison](preview.html) in a browser. In the "Light" and "Dark" modes the workspace takes the full width. The "Open archive" button shows the cursor position at the moment of the alarm. One local camera frame is used for the comparison. The EN and RU buttons switch the language of the page.

## Color

The catalog and the working client use the common [tokens](tokens.css). They define the local Inter files for Cyrillic and Latin, the text roles, the spacing scale and the panel radius. The reference work screens use the same fonts when started without internet access.

The cream background and the milky panels of the light theme follow the shades of the bacuri pulp. The main text of the light theme is almost black, the secondary text is neutral gray. The main action gets a muted sand-yellow fill, the selected section and the alarm get a thin side bar on a slightly tinted background. The saturated gold of the rind is used in the brand mark. Terracotta, an icon and a label mark a warning. Muted olive marks a healthy connection and a confirmation. The color and the proportions of the video frame are kept in both themes. Labels over video use their own opaque backing. Times and identifiers in dense rows are aligned with tabular digits.

The dark theme uses the neutral colors of the classic Dark+ theme of VS Code. The main background and text match `editor.background` and `editor.foreground`, panels `menu.background`, secondary text `input.placeholderForeground`. On a full screen the computed colors of the background, the panel, the heading and the camera profile label are compared. [Dark+ base colors](https://github.com/microsoft/vscode/blob/main/extensions/theme-defaults/themes/dark_vs.json)

| Role | Light | Dark | Use |
| --- | --- | --- | --- |
| Main background | `#F7F4ED` | `#1e1e1e` | Space between the work panels |
| Panel | `#FFFDF8` | `#252526` | Event feed, forms, archive panel |
| Nested surface | `#EFEADF` | `#303031` | Field groups and secondary elements |
| Main text | `#202020` | `#D4D4D4` | Camera name, event and description |
| Secondary text | `#575757` | `#A6A6A6` | Time, profile names, service labels |
| Field or control border | `#827D6F` | `#8D8776` | The outline that identifies an element. A thin decorative divider uses the separate `--line` token |
| Main action | `#E1D8BB` | `#504A37` | Muted sand-yellow fill. The button text is `#202020` in the light theme and `#F0ECE6` in the dark one |
| Link and selection | `#696046` | `#B4A989` | Link text and the side bar of the selected section or alarm |
| Selection background | `#EEE9DA` | `#373428` | Neutral backing of the selected section or alarm |
| Recorded interval | `#8B8062` | `#9B8D6F` | Solid segment on the archive timeline and in the legend |
| Keyboard focus | `#6B6044` | `#BBB08E` | Outer outline of 3 CSS pixels with a gap of 3 CSS pixels |
| Confirmation | `#56603F` | `#BFCBA4` | A circle with a check mark and the exact name of the result |
| Warning | `#94421E` | `#FFAE86` | Estimated time or an action that needs attention |
| Critical alarm or failure | `#A13239` | `#FFA3A1` | A bell for an alarm, a plug for a lost connection, a circle with a cross for an error |
| Information | `#63594F` | `#CFC2B1` | Explanation of a state, cataloging |
| Brand pair | `#F2CC61` and `#33271D` | The same pair | The rind ring and the seed-colored backing |

The tokens of hover, press, selected state, status backings and video are part of [tokens.css](tokens.css). The rules apply to a container through `data-theme="light"` or `data-theme="dark"`. On the first start the working client uses the system theme. The switch in the top bar selects the system, dark or light theme. The browser keeps the choice.

84 combinations were checked by the relative luminance of sRGB. The threshold is 4.5:1 for text and 3:1 for required borders and the focus indicator. The text on the main button has a contrast of 11.44:1 in the light theme and 7.51:1 in the dark theme. The main text on a panel has a contrast of 16.03:1 in the light theme and 10.33:1 in the dark theme. These values refer to the listed pairs and opaque backings. [Results by pair](contrast.csv) · [Text contrast](https://www.w3.org/WAI/WCAG22/Understanding/contrast-minimum.html) · [Control contrast](https://www.w3.org/WAI/WCAG22/Understanding/non-text-contrast.html)

## Brand mark

Bacuri has a yellow or yellowish-green rind and light pulp around the seeds. The main mark shows a cross-section. Inside the golden ring three cream segments with dark seeds surround a triangular opening. It resembles a diaphragm aperture and is kept in the single-color mark and in the 16-pixel favicon. [Description of the fruit, Embrapa](https://www.alice.cnptia.embrapa.br/alice/bitstream/doc/1164519/1/Correa-1994.pdf)

The header and the browser tab use the "Section aperture" mark. [The mark at sizes from 16 to 48 pixels](preview.html#brand)

| Use | Files |
| --- | --- |
| Interface and single-color print | [Main mark](brand/mark.svg) · [Monochrome mark](brand/mark-mono.svg) |
| Browser tabs | [Compact SVG](brand/favicon.svg) · [ICO with sizes 16, 32 and 48](brand/favicon.ico) |
| Desktop icons | [Apple touch 180](brand/apple-touch-icon.png) · [PNG 192](brand/icon-192.png) · [PNG 512](brand/icon-512.png) |

After an SVG change regenerate the derived files from the repository root. The regular check `task ui-kit:check` compares the PNG files and the ICO frames with the rendering of the same Chromium.

```sh
docker compose build ui-kit-check
docker compose run --rm --no-deps -T --user "$(id -u):$(id -g)" -v "$PWD/design/bacuri/brand:/brand" ui-kit-check node tests/render-brand.mjs --write /brand
```

When the monochrome SVG is inserted directly, its fill inherits `currentColor`. The application icon uses one mark consistently in both themes. Notifications and the connection state are shown in the working interface.

## Icons

The [catalog](icons/) contains 39 separate Lucide SVG files and a [common SVG sprite](icons/sprite.svg). The sprite variant is meant to be served from the same origin as the client. All sources are part of the offline delivery kit. The Lucide and Feather license notice is kept. [Kit licenses](icons/LICENSE.txt)

| Group | Purposes |
| --- | --- |
| Workspace | Monitoring, cameras, archive, alarms, map, settings |
| Camera and network | Camera discovery, observation point, PTZ, connection, lost connection, retry |
| Video and archive | Live video, playback, pause, next frame, full screen, export |
| States | Confirmation, estimated time, cataloging, gap, warning, error |
| System | Storage, permissions, filter, search, light and dark themes |

The icon geometry uses a 24 × 24 grid with a stroke of 2 and rounded ends. Navigation uses 20 or 24 CSS pixels, a status next to text uses 16. A standalone icon button has a hit area of 40 × 40 CSS pixels at a workstation and 44 × 44 for touch input. A decorative icon with a visible label is hidden from the screen reader. A button without text gets the accessible name of its action, for example "Next frame".

On the archive timeline a recording is a solid segment, a gap is diagonal hatching, estimated time is vertical strokes and cataloging is dots. For a design review compare both themes on one frame, then in grayscale. During implementation repeat the keyboard and contrast check on whole operator and administrator screens, including loading, no data and an error.
