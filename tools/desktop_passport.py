#!/usr/bin/env python3
"""Compare the desktop workstation passport with the actual stand.

Run on the stand host. The script reads the operating system, the CPU, the memory, the GPU
and its driver and the displays of the host, then the Qt, GStreamer and SDL versions, the
decoders and the OpenGL renderer of the desktop image with the GPU passed through. Any
difference means the passport or the stand changed and the affected measurements must be
repeated.
"""

from __future__ import annotations

import argparse
import json
import os
import platform
import re
import subprocess
import sys
import xml.etree.ElementTree as ElementTree
from pathlib import Path

import yaml

ROOT = Path(__file__).resolve().parent.parent
PASSPORT = ROOT / "desktop/workstation.yaml"
IMAGE = "bacuri-vms-desktop"
DECODERS = ("nvh264dec", "nvh265dec", "nvav1dec", "avdec_h264", "avdec_h265")


def run(command: list[str]) -> str:
    try:
        return subprocess.run(command, check=True, capture_output=True, text=True, timeout=120).stdout
    except (OSError, subprocess.SubprocessError):
        return ""


def os_release() -> dict[str, str]:
    values = {}
    for line in Path("/etc/os-release").read_text(encoding="utf-8").splitlines():
        if "=" in line:
            key, value = line.split("=", 1)
            values[key] = value.strip('"')
    return values


def cpu() -> tuple[str, int]:
    text = Path("/proc/cpuinfo").read_text(encoding="utf-8")
    model = re.search(r"^model name\s*:\s*(.+)$", text, re.MULTILINE)
    return (model.group(1).strip() if model else ""), os.cpu_count() or 0


def memory_gib() -> int:
    text = Path("/proc/meminfo").read_text(encoding="utf-8")
    kib = int(re.search(r"^MemTotal:\s*(\d+)", text, re.MULTILINE).group(1))
    return round(kib / 1024 / 1024)


def gpu() -> dict[str, object]:
    line = run(["nvidia-smi", "--query-gpu=name,memory.total,driver_version", "--format=csv,noheader,nounits"])
    if not line:
        return {}
    name, memory, driver = (part.strip() for part in line.splitlines()[0].split(","))
    return {"model": name, "memory_mib": int(memory), "driver": driver}


def displays() -> list[dict[str, object]]:
    """Read the logical monitors of the GNOME session, falling back to xrandr."""
    config = Path.home() / ".config/monitors.xml"
    found = []
    if config.exists():
        tree = ElementTree.parse(config)
        configuration = tree.getroot().find("configuration")
        for logical in configuration.findall("logicalmonitor") if configuration is not None else []:
            scale = float(logical.findtext("scale", "1"))
            for monitor in logical.findall("monitor"):
                mode = monitor.find("mode")
                found.append({
                    "output": monitor.findtext("monitorspec/connector"),
                    "resolution": f"{mode.findtext('width')}x{mode.findtext('height')}",
                    "refresh_hz": round(float(mode.findtext("rate"))),
                    "scale": int(scale) if scale.is_integer() else scale,
                })
    if found:
        return found
    for match in re.finditer(r"^(\S+) connected.*?\n\s+(\d+x\d+)\s+([\d.]+)\*", run(["xrandr"]), re.MULTILINE | re.DOTALL):
        found.append({"output": match.group(1), "resolution": match.group(2),
                      "refresh_hz": round(float(match.group(3))), "scale": 1})
    return found


def image() -> dict[str, object]:
    script = (
        "dpkg-query -W -f='${Version}\\n' qt6-base-dev; dpkg-query -W -f='${Version}\\n' libsdl3-dev; "
        "gst-inspect-1.0 --version | sed -n 2p; "
        + "; ".join(f"gst-inspect-1.0 {name} >/dev/null 2>&1 && echo decoder {name}" for name in DECODERS)
        + "; GST_GL_PLATFORM=egl GST_DEBUG=glcontext:4 gst-launch-1.0 -q gltestsrc num-buffers=1 ! fakesink 2>&1"
        " | sed -n 's/.*GL_RENDERER: //p'"
    )
    output = run(["docker", "run", "--rm", "--network=none", "--gpus", "all",
                  "-e", "NVIDIA_DRIVER_CAPABILITIES=all", IMAGE, "sh", "-c", script])
    lines = [line.strip() for line in output.splitlines() if line.strip()]
    if len(lines) < 3:
        return {}
    version = lambda value: re.match(r"[\d.]+", value).group(0).rstrip(".")
    return {
        "qt": version(lines[0]),
        "sdl": version(lines[1]),
        "gstreamer": lines[2].split()[-1],
        "decoders": sorted(line.split()[1] for line in lines if line.startswith("decoder ")),
        "renderer": next((line for line in lines if "/" in line and not line.startswith("decoder")), ""),
    }


def collect() -> dict[str, object]:
    release = os_release()
    model, threads = cpu()
    return {
        "os": {
            "name": release.get("PRETTY_NAME", ""),
            "kernel": platform.release(),
            "session": os.environ.get("XDG_SESSION_TYPE", ""),
        },
        "cpu": model,
        "threads": threads,
        "memory_gib": memory_gib(),
        "gpu": gpu(),
        "displays": displays(),
        "image": image(),
    }


def compare(passport: dict, actual: dict) -> list[str]:
    stand = passport["stations"][passport["targets"]["linux"]["stand"]]
    build = passport["build"]
    differences = []

    def check(name: str, expected: object, found: object) -> None:
        if expected != found:
            differences.append(f"{name}: passport {expected!r}, stand {found!r}")

    for key in ("name", "kernel", "session"):
        check(f"os.{key}", stand["os"][key], actual["os"][key])
    check("cpu", stand["cpu"], actual["cpu"])
    check("threads", stand["threads"], actual["threads"])
    check("memory_gib", stand["memory_gib"], actual["memory_gib"])
    for key in ("model", "memory_mib", "driver"):
        check(f"gpu.{key}", stand["gpu"][key], actual["gpu"].get(key))
    check("displays", stand["displays"], actual["displays"])
    found = actual["image"]
    if not found:
        differences.append(f"image: the {IMAGE} image does not start with the GPU, run docker compose build desktop")
        return differences
    for key in ("qt", "gstreamer", "sdl"):
        check(f"build.{key}", build[key], found[key])
    expected = sorted(stand["decoders"]["hardware"] + stand["decoders"]["software"])
    check("decoders", expected, found["decoders"])
    check("graphics.renderer", stand["graphics"]["renderer"], found["renderer"])
    return differences


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--show", action="store_true", help="print the collected facts as JSON")
    arguments = parser.parse_args()
    passport = yaml.safe_load(PASSPORT.read_text(encoding="utf-8"))
    actual = collect()
    if arguments.show:
        print(json.dumps(actual, indent=2, ensure_ascii=False))
    differences = compare(passport, actual)
    for difference in differences:
        print(difference, file=sys.stderr)
    if differences:
        print("The stand differs from the passport: update desktop/workstation.yaml and repeat the affected measurements",
              file=sys.stderr)
        return 1
    stand = passport["stations"]["stand"]
    print(f"Desktop stand matches the passport: {stand['os']['name']}, {stand['gpu']['model']} "
          f"driver {stand['gpu']['driver']}, Qt {passport['build']['qt']}, GStreamer {passport['build']['gstreamer']}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
