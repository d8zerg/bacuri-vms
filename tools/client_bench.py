#!/usr/bin/env python3
"""Run the measurement series of the Bacuri VMS Client bench on the stand.

Each run starts the bench in the desktop-gpu Compose service on the stand display,
samples the GPU and the NVDEC load on the host, checks the first frame and the frame after
a seek against the reference recording and writes the reports to build/desktop/reports/video.

Live sources: the synthetic camera of the Compose stand when it is running, and a physical
camera when BACURI_CAMERA_URL, BACURI_CAMERA_USER and BACURI_CAMERA_PASSWORD are set. The
address and the credentials are passed to the container through the environment and are
never written to the reports.
"""

from __future__ import annotations

import argparse
import json
import os
import statistics
import subprocess
import sys
import threading
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUTPUT = ROOT / "build/desktop/reports/video"
BENCH = "build/desktop/gcc/bacuri-vms-client-bench"
REFERENCE = "build/desktop/reference"
SEEK_FRAME = 537  # Not a keyframe: the decoder starts from frame 500.


def sample_gpu(stop: threading.Event, samples: list[dict]) -> None:
    query = "utilization.gpu,utilization.decoder,memory.used"
    while not stop.is_set():
        line = subprocess.run(["nvidia-smi", f"--query-gpu={query}", "--format=csv,noheader,nounits"],
                              capture_output=True, text=True).stdout.strip()
        if line:
            gpu, decoder, memory = (float(value) for value in line.split(","))
            samples.append({"gpu": gpu, "decoder": decoder, "memory_mib": memory})
        stop.wait(0.5)


def run(name: str, arguments: list[str], seconds: int, environment: dict[str, str], service: str = "desktop-gpu") -> dict:
    report = OUTPUT / f"{name}.json"
    report.unlink(missing_ok=True)
    command = ["docker", "compose", "run", "--rm", "--no-deps", "-T", "--user", f"{os.getuid()}:{os.getgid()}"]
    for key in environment:
        command += ["-e", key]
    command += [service, BENCH, *arguments, "--seconds", str(seconds),
                "--report", str(report.relative_to(ROOT))]
    samples: list[dict] = []
    stop = threading.Event()
    sampler = threading.Thread(target=sample_gpu, args=(stop, samples))
    time.sleep(1)
    sampler.start()
    completed = subprocess.run(command, cwd=ROOT, env={**os.environ, **environment}, capture_output=True, text=True)
    stop.set()
    sampler.join()
    if not report.exists():
        return {"name": name, "failed": True, "error": completed.stderr.strip().splitlines()[-3:]}
    result = json.loads(report.read_text())
    # The first seconds start the pipelines, the load is taken from the rest of the run.
    steady = samples[len(samples) // 4:] or samples
    result["gpu_percent"] = round(statistics.mean(sample["gpu"] for sample in steady), 1) if steady else None
    result["nvdec_percent"] = round(statistics.mean(sample["decoder"] for sample in steady), 1) if steady else None
    result["gpu_memory_mib"] = max((sample["memory_mib"] for sample in steady), default=None)
    result["name"] = name
    # Addresses of live sources may name internal equipment and stay out of the reports.
    result["arguments"] = " ".join("rtsp" if value.startswith("rtsp://") else value for value in arguments)
    report.write_text(json.dumps(result, indent=2))
    return result


def verdict(result: dict, reference: bool, seek: bool) -> list[str]:
    problems = []
    if result.get("failed"):
        return [f"did not finish: {result.get('error')}"]
    if result.get("graphics", "").startswith("NVIDIA") and not result.get("presentation_devices"):
        problems.append("no DRI devices: the window is not presented to the screen")
    for cell in result["cells_report"]:
        if cell.get("error"):
            problems.append(f"cell {cell['cell']}: {cell['error']}")
        if cell.get("frames", 0) == 0:
            problems.append(f"cell {cell['cell']}: no frames")
        if reference and cell.get("first_frame") != 0:
            problems.append(f"cell {cell['cell']}: first frame {cell.get('first_frame')} instead of 0")
        if seek and cell.get("seek_frame") != cell.get("seek_target"):
            problems.append(f"cell {cell['cell']}: frame {cell.get('seek_frame')} after the seek instead of "
                            f"{cell.get('seek_target')}")
        fallback = cell.get("fallback")
        if "--fault-decoder-at" in result.get("arguments", "") and not fallback:
            problems.append(f"cell {cell['cell']}: no fallback after the decoder failure")
        if fallback and fallback.get("first_frame_after") is not None and \
                fallback.get("last_frame_before") is not None and \
                fallback["first_frame_after"] - fallback["last_frame_before"] not in (0, 1):
            problems.append(f"cell {cell['cell']}: resumed at frame {fallback['first_frame_after']} after "
                            f"{fallback['last_frame_before']}")
        if result["decoder"] == "hardware" and cell.get("system_memory_copies_per_frame") != 0:
            problems.append(f"cell {cell['cell']}: decoder output in {cell.get('decoder_output')}")
    # The cells of a grid show the same frame of a recorded source. The cells are read one after another,
    # so a sample may fall on a frame change: p95 of the spread stays within one shown frame, which is
    # two source frames when a station without a GPU limits the cells to 15 frames per second.
    spread = result.get("grid", {}).get("sync_spread_frames", {})
    shown_frame = 2 if result.get("software_limits") else 1
    if reference and spread.get("count") and spread["p95"] > shown_frame:
        problems.append(f"cells {spread['p95']:.0f} frames apart at p95")
    return problems


def summary_row(result: dict, problems: list[str]) -> str:
    if result.get("failed"):
        return f"| {result['name']} | - | - | - | - | - | - | - | - | - | {'; '.join(problems)} |"
    cells = result["cells_report"]
    shown = sum(cell["frames"] for cell in cells) / result["seconds"]
    grid = result.get("grid", {})
    spread = grid.get("sync_spread_frames", {})
    spread_text = f"{spread['p95']:.0f} / {spread['max']:.0f}" if spread.get("count") else "-"
    ui = result["ui_frame_interval_ms"]
    return (f"| {result['name']} | {cells[0]['decoder']} | {cells[0]['system_memory_copies_per_frame']} | "
            f"{shown:.0f} | {grid.get('grid_start_ms', 0):.0f} | {grid.get('grid_seek_ms', 0):.0f} | {spread_text} | "
            f"{ui['p50']:.1f} / {ui['p99']:.1f} / {ui['max']:.0f} | {result['cpu_percent_of_one_core']:.0f} | "
            f"{result['gpu_percent']} / {result['nvdec_percent']} | {map_cost(result)}{fallback_note(cells)}"
            f"{'ok' if not problems else '; '.join(problems)} |")


def map_cost(result: dict) -> str:
    if not result.get("map"):
        return ""
    return f"map render p50 {result['map']['render_ms']['p50']:.2f} ms, "


def fallback_note(cells: list[dict]) -> str:
    resumed = [cell["fallback"]["resumed_after_ms"] for cell in cells if "resumed_after_ms" in cell.get("fallback", {})]
    if resumed:
        return f"software after {max(resumed):.0f} ms, "
    if any(cell.get("fallback") for cell in cells):
        return "software from the start, "
    return ""


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--seconds", type=int, default=20)
    parser.add_argument("--cells", default="1,4,9,16")
    parser.add_argument("--live", action="store_true", help="also measure the live sources")
    parser.add_argument("--only", help="run only the runs whose name contains this text")
    parser.add_argument("--repeat", type=int, default=1, help="repeat each run")
    arguments = parser.parse_args()
    OUTPUT.mkdir(parents=True, exist_ok=True)

    runs = []
    for codec in ("h264", "h265"):
        for decoder in ("hardware", "software"):
            for cells in (int(value) for value in arguments.cells.split(",")):
                runs.append((f"reference-{codec}-{decoder}-{cells}",
                             ["--source", f"{REFERENCE}/{codec}-1080p25.mp4", "--codec", codec, "--decoder", decoder,
                              "--cells", str(cells), "--seek-frame", str(SEEK_FRAME)], {}, True, True))
    # The work without a GPU: a decoder failure during playback and a workstation without GPU access.
    # The map comes from the site and is measured by task desktop:site-map.
    h264 = ["--source", f"{REFERENCE}/h264-1080p25.mp4", "--codec", "h264"]
    runs.append(("fallback-h264-auto-4", [*h264, "--decoder", "auto", "--cells", "4", "--fault-decoder-at", "6"], {}, True,
                 False))
    # Without a GPU the client decodes in software and limits each cell to 640x360 at 15 frames/s.
    for cells in (4, 9, 16):
        runs.append((f"cpu-h264-auto-{cells}",
                     [*h264, "--decoder", "auto", "--cells", str(cells), "--seek-frame", str(SEEK_FRAME)], {},
                     True, True, "desktop-cpu"))
    if arguments.live:
        stand = subprocess.run(["docker", "compose", "ps", "--status", "running", "-q", "rtsp-camera"], cwd=ROOT,
                               capture_output=True, text=True).stdout.strip()
        if stand:
            for path, codec in (("main", "h264"), ("hevc", "h265")):
                for cells in (1, 4):
                    runs.append((f"stand-{codec}-hardware-{cells}",
                                 ["--source", f"rtsp://rtsp-camera:8554/{path}", "--codec", codec, "--decoder",
                                  "hardware", "--cells", str(cells)], {}, False, False))
        else:
            print("The stand camera is not running: start it with task stand:up", file=sys.stderr)
        camera = {key: os.environ[key] for key in ("BACURI_CAMERA_URL", "BACURI_CAMERA_USER", "BACURI_CAMERA_PASSWORD")
                  if key in os.environ}
        if len(camera) == 3:
            url = camera.pop("BACURI_CAMERA_URL")
            for decoder in ("hardware", "software"):
                runs.append((f"camera-h264-{decoder}-1",
                             ["--source", url, "--codec", "h264", "--decoder", decoder, "--cells", "1"],
                             camera, False, False))

    rows = []
    failed = False
    if arguments.only:
        runs = [entry for entry in runs if arguments.only in entry[0]]
    runs = [(f"{name}-{attempt}" if arguments.repeat > 1 else name, *rest)
            for name, *rest in runs for attempt in range(1, arguments.repeat + 1)]
    for name, bench_arguments, environment, reference, seek, *service in runs:
        result = run(name, bench_arguments, arguments.seconds, environment, *service)
        problems = verdict(result, reference, seek)
        failed |= bool(problems)
        rows.append(summary_row(result, problems))
        print(rows[-1], flush=True)

    header = ("| Run | Decoder | Copies via RAM | Frames/s shown | Grid start, ms | Grid seek, ms "
              "| Cell spread p95 / max, frames | UI frame p50 / p99 / max, ms "
              "| CPU, % of a core | GPU / NVDEC, % | Result |\n|" + " --- |" * 11)
    (OUTPUT / "summary.md").write_text(header + "\n" + "\n".join(rows) + "\n")
    print(f"Reports: {OUTPUT.relative_to(ROOT)}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
