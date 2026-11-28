"""Generate the web stylesheet and Qt singleton from the common design tokens."""

import argparse
import json
import math
import re
from pathlib import Path


THEMES = ("light", "dark", "contrastLight", "contrastDark")


def token_name(key):
    if not re.fullmatch(r"[a-z][a-z0-9]*(?:-[a-z0-9]+)*", key):
        raise ValueError(f"Invalid token name: {key}")
    first, *rest = key.split("-")
    return ("textOn" if first == "on" else first) + "".join(part.title() for part in rest)


def number(value):
    return type(value) in (int, float) and math.isfinite(value)


def color(key, token):
    value = token["$value"]
    rgb = value["components"]
    if (token.get("$type", "color") != "color" or value["colorSpace"] != "srgb"
            or len(rgb) != 3 or not all(number(v) and 0 <= v <= 1 for v in rgb)
            or value.get("alpha", 1) != 1):
        raise ValueError(f"Unsupported color {key}: {token}")
    return "#" + "".join(f"{round(n * 255):02x}" for n in rgb)


def dimension(key, token):
    kind, value = token["$type"], token["$value"]
    if kind == "fontFamily" and isinstance(value, list) and value and all(isinstance(v, str) and v for v in value):
        css = ", ".join(json.dumps(v) if " " in v else v for v in value)
        return css, "string", json.dumps(value[0])
    if kind == "number" and number(value):
        return f"{value:g}", "real", f"{value:g}"
    if kind in ("dimension", "duration") and isinstance(value, dict):
        unit, size = value["unit"], value["value"]
        units = {"px", "rem"} if kind == "dimension" else {"ms"}
        if unit in units and number(size) and size >= 0:
            logical = size * 16 if unit == "rem" else size
            return f"{size:g}{unit}", "real", f"{logical:g}"
    raise ValueError(f"Unsupported token {key}: {token}")


def render(source):
    data = json.loads(source.read_text())
    if set(data) != {"dimensions", *THEMES}:
        raise ValueError("Unexpected token groups")
    palettes = {}
    for theme in THEMES:
        group = data[theme].copy()
        if group.pop("$type") != "color":
            raise ValueError(f"Expected a color palette: {theme}")
        palettes[theme] = {key: color(key, token) for key, token in group.items()}
        if group.keys() != palettes["light"].keys():
            raise ValueError(f"Incomplete palette: {theme}")

    names = [token_name(key) for group in (data["dimensions"], palettes["light"]) for key in group]
    if len(set(names)) != len(names):
        raise ValueError("Multiple tokens generate the same QML property")
    css, props, contrast = [], [], []
    for key, token in data["dimensions"].items():
        value, qt_type, qt_value = dimension(key, token)
        css.append(f"  --{key}: {value};")
        props.append(f"    readonly property {qt_type} {token_name(key)}: {qt_value}")
    for key in palettes["light"]:
        light, dark, light_high, dark_high = (palettes[theme][key] for theme in THEMES)
        css_value = light if light == dark else f"light-dark({light}, {dark})"
        css.append(f"  --{key}: {css_value};")
        contrast.append(f"    --{key}: light-dark({light_high}, {dark_high});")
        props.append(f'    readonly property color {token_name(key)}: Preferences.highContrast\n'
                     f'        ? (Preferences.dark ? "{dark_high}" : "{light_high}")\n'
                     f'        : (Preferences.dark ? "{dark}" : "{light}")')
    template = source.with_name("tokens.template.css").read_text()
    for marker in ("@TOKENS@", "@CONTRAST@"):
        if template.count(marker) != 1:
            raise ValueError(f"Expected one {marker} placeholder")
    stylesheet = template.replace("@TOKENS@", "\n".join(css)).replace("@CONTRAST@", "\n".join(contrast))
    qml = "\n".join(["pragma Singleton", "import QtQuick", "import Bacuri.Vms.Client.Settings", "", "QtObject {",
                     "    readonly property real scale: Preferences.textScale * Preferences.systemTextScale",
                     "    function scaled(base: real): int { return Math.round(base * scale) }", *props, "}", ""])
    return stylesheet, qml, {"scale", "scaled", *names}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("--css", type=Path)
    parser.add_argument("--qml", type=Path)
    parser.add_argument("--check-css", type=Path)
    parser.add_argument("--check-uses", type=Path, nargs="+", default=[])
    args = parser.parse_args()
    try:
        css, qml, available = render(args.source)
    except (ValueError, KeyError, TypeError) as error:
        parser.error(f"{args.source}: {error}")
    for path in args.check_uses:
        used = set(re.findall(r"\bTokens\.(\w+)", path.read_text()))
        if missing := used - available:
            parser.error(f"{path}: undefined design tokens: {', '.join(sorted(missing))}")
    if args.check_css and args.check_css.read_text() != css:
        parser.error(f"Stale generated stylesheet: {args.check_css}")
    for path, contents in ((args.css, css), (args.qml, qml)):
        if path and (not path.exists() or path.read_text() != contents):
            path.write_text(contents)


if __name__ == "__main__":
    main()
