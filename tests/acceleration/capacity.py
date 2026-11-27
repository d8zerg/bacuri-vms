"""Measure concurrent real-time delivery and unpaced archive work on identical inputs."""
from collections import Counter
from concurrent.futures import ThreadPoolExecutor
from statistics import median
import json
import os
import subprocess
import time

from qualification import FRAMES, FPS, command, control, make_input, save


def run_job(kind, file, index=0):
    destination = control.ROOT / f"export-{index}.mp4" if kind == "export" else os.devnull
    start = time.monotonic()
    run = subprocess.run(command(kind, file, output=destination), capture_output=True, text=True, timeout=60)
    end = time.monotonic()
    paths = [json.loads(line) for line in run.stderr.splitlines() if line.startswith('{"backend"')]
    reports = [json.loads(line) for line in run.stdout.splitlines() if line.startswith("{")]
    duration = end - start
    result = {"wall_s": round(duration, 3), "paths": [[p["kind"], p["backend"]] for p in paths],
              "devices": [[p["kind"], p["running_device_id"]] for p in paths], "fps": round(FRAMES / duration, 2)}
    if run.returncode or not paths:
        result["error"] = run.stderr[-1000:] or "Missing codec telemetry"
    elif kind.startswith("live"):
        if not reports or reports[-1]["frames"] != FRAMES:
            result["error"] = "Incomplete live delivery"
        else:
            result.update(reports[-1], input_start=(reports[0]["clock_ms"] - reports[0]["first_frame_ms"]) / 1000)
    return result


def cohort(kind, file, count):
    with ThreadPoolExecutor(max_workers=count) as pool:
        jobs = list(pool.map(lambda i: run_job(kind, file, i), range(count)))
    ok = all("error" not in job and job["steady_lag_ms"] <= 1000 for job in jobs)
    paths = Counter(job.get("backend", "failed") for job in jobs)
    first = [job.pop("input_start") for job in jobs if "input_start" in job]
    if first and max(first) - min(first) > 1:
        ok = False
    return {"count": count, "delivered": ok, "last_backends": dict(paths), "workers": jobs}


def density(kind, file, limit, record):
    lower, upper = 0, limit + 1
    count = 1
    paths = {}
    while lower + 1 < upper:
        trial = cohort(kind, file, count)
        record(trial)
        if trial["delivered"]:
            lower = count
            paths = trial["last_backends"]
        else:
            upper = count
        count = limit if upper == limit + 1 else (lower + upper) // 2
    return {"streams": lower, "at_test_ceiling": lower == limit, "last_backends": paths}


def measure(node):
    limit = int(os.environ.get("BACURI_DENSITY_LIMIT", "8"))
    if not 1 <= limit <= 64:
        raise ValueError("Density limit must be between 1 and 64")
    report = []
    modes = ["cpu_only"] if node.backend == "cpu" else ["cpu_only", "auto", "manual"]
    for width, height in ((1920, 1080), (2560, 1440)):
        for codec in ("h264", "h265"):
            file = make_input(control.ROOT, codec, width, height)
            for mode in modes:
                for kind in ("export", "archive", "live_mask", "live_profile"):
                    task = "archive_analysis" if kind == "archive" else kind
                    row = {"input": file.stem, "mode": mode, "task": task}
                    if not node.apply(mode, task):
                        row["state"] = "unavailable"
                    elif kind.startswith("live"):
                        def record(trial):
                            with (node.output / "density.jsonl").open("a") as log:
                                print(json.dumps({**row, **trial}), file=log, flush=True)
                        row.update(density(kind, file, limit, record))
                    else:
                        runs = [run_job(kind, file) for _ in range(3)]
                        if any("error" in run for run in runs):
                            raise RuntimeError(runs)
                        row.update(median_fps=median(run["fps"] for run in runs), runs=runs)
                    report.append(row)
                    save(node.output / "capacity.json", report)
                    save(node.output / "status.json", {"state": "running", "phase": "capacity", "backend": node.backend,
                                                       "completed": len(report), "total": 4 * 4 * len(modes)})
                    print(file.stem, mode, task, row.get("streams", row.get("median_fps", row.get("state"))), flush=True)
