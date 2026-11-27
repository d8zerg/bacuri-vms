"""Keep one production mask worker alive across alternating idle and busy periods."""
from collections import deque
from contextlib import ExitStack
from pathlib import Path
from statistics import median
import json
import os
import subprocess
import time

from qualification import command, control, FPS, make_input, save


class Live:
    def __init__(self, stack, kind, source, frames, directory, env=None):
        self.path = directory / (kind + ".jsonl")
        writer = stack.enter_context(self.path.open("w"))
        self.reader = stack.enter_context(self.path.open())
        self.codec_log = directory / (kind + "-codecs.log")
        codecs = stack.enter_context(self.codec_log.open("w"))
        self.process = subprocess.Popen(command(kind, source, frames), stdout=writer, stderr=codecs, env=env)
        stack.callback(control.stop, self.process)
        self.last = {}
        self.paths = []
        self.target = frames

    def progress(self):
        while True:
            offset = self.reader.tell()
            line = self.reader.readline()
            if not line.endswith("\n"):
                self.reader.seek(offset)
                break
            self.last = json.loads(line)
            if not self.paths or self.paths[-1] != self.last["backend"]:
                self.paths.append(self.last["backend"])
        if self.process.poll() not in (0, None):
            raise RuntimeError(f"{self.path.name} exited with {self.process.returncode}")
        return self.last

    def finish(self):
        if self.process.wait(timeout=12) or self.progress().get("frames") != self.target:
            raise RuntimeError(f"Incomplete live output: {self.last}")
        devices = []
        with self.codec_log.open() as log:
            for line in log:
                if line.startswith('{"backend"'):
                    device = json.loads(line)["running_device_id"]
                    if not devices or devices[-1] != device:
                        devices.append(device)
        if not devices:
            raise RuntimeError(f"Missing device reports in {self.codec_log}")
        self.last.update(devices=devices, device_switches=len(devices) - 1)
        return self.last


def rss(pid):
    status = Path(f"/proc/{pid}/status").read_text().splitlines()
    return int(next(line.split()[1] for line in status if line.startswith("VmRSS:"))) / 1024


def vram(pids, backend):
    if backend == "nvidia":
        output = subprocess.check_output(["nvidia-smi", "--query-compute-apps=pid,used_gpu_memory",
                                          "--format=csv,noheader,nounits"], text=True, timeout=5)
        readings = {}
        for line in output.splitlines():
            process, mib = (v.strip() for v in line.split(",", 1))
            if process.isdigit() and int(process) in pids and mib.isdigit():
                pid = int(process)
                readings[pid] = readings.get(pid, 0) + int(mib)
        return readings
    return {pid: value for pid in pids if (value := drm_memory(pid)) is not None}


def drm_memory(pid):
    # A DRM client can own several file descriptors. Count its resident local memory once.
    clients = {}
    for desc in Path(f"/proc/{pid}/fdinfo").glob("*"):
        try:
            fields = dict(line.split(":", 1) for line in desc.read_text().splitlines() if ":" in line)
        except FileNotFoundError:
            continue
        if "drm-client-id" not in fields:
            continue
        values = [v.split() for k, v in fields.items()
                  if k.startswith(("drm-resident-vram", "drm-resident-local"))]
        if values:
            units = {"B": 1, "KiB": 1024, "MiB": 1024 ** 2}
            clients[fields.get("drm-pdev"), fields["drm-client-id"]] = sum(int(n) * units[u] for n, u in values)
    return sum(clients.values()) / (1024 ** 2) if clients else None


def measure(node):
    if node.backend == "cpu" or not node.apply("auto"):
        raise RuntimeError("Auto requires a verified hardware device")
    source = make_input(control.ROOT, "h264", 1920, 1080)
    extra = make_input(control.ROOT, "h265", 2560, 1440)
    seconds = min(node.duration, int(node.deadline - time.monotonic()) - 3)
    if seconds < 48:
        raise TimeoutError("Insufficient test time for alternating load after the node probe")
    phase = seconds // 8
    limits = {name: int(os.environ.get("BACURI_" + name.upper(), default)) for name, default in
              (("rss_growth_mib", "128"), ("vram_growth_mib", "128"), ("switch_limit", str((seconds + phase - 1) // phase // 2)))}
    if min(limits.values()) < 0:
        raise ValueError("Negative soak limit")
    ready = {"phase": "soak", "backend": node.backend, "requested_s": node.duration, "target_s": seconds, "limits": limits,
             "phase_s": phase, "run_limit_s": 600}
    start = time.monotonic()
    boot_start = time.clock_gettime(time.CLOCK_BOOTTIME)
    target = seconds * FPS
    first, last = [], deque(maxlen=3)
    warmup = phase
    batch = -1
    busy = 0
    background_switches = 0

    def finish_profile(worker):
        result = worker.finish()
        with (node.output / "background.jsonl").open("a") as log:
            print(json.dumps({**result, "paths": worker.paths}), file=log)
        if (worker.paths not in (["cpu"], [node.backend], ["cpu", node.backend])
                or result["device_switches"] != result["switches"]):
            raise RuntimeError(f"Unexpected live profile transitions: {worker.paths}")
        if result["steady_lag_ms"] > 1000:
            raise RuntimeError(f"Live profile delivery exceeded one second: {result}")
        return result["device_switches"]

    with ExitStack() as stack, ExitStack() as load, (node.output / "samples.jsonl").open("w") as samples:
        live = Live(stack, "live_mask", source, target, node.output)
        background = None
        while True:
            now = time.monotonic() - start
            if time.clock_gettime(time.CLOCK_BOOTTIME) - boot_start - now > 1:
                raise RuntimeError("The host suspended during the continuous run")
            state = live.progress()
            if any(child.poll() is not None for child in node.services):
                raise RuntimeError("Service exited during soak")
            if now > 12 and (not state or now - state["frames"] / FPS > 10):
                raise RuntimeError(f"Missing live heartbeat: {state}")
            if live.process.poll() is not None:
                live.finish()
                break
            if now > seconds + 12:
                raise RuntimeError("Live delivery exceeded the duration")
            interval = int(now // phase)
            if interval != batch:
                if background:
                    background_switches += finish_profile(background)
                    load.close()
                    background = None
                batch = interval
                duration = min(phase, int(seconds - now)) - 3
                if interval % 2 and duration > 0:
                    background = Live(load, "live_profile", extra, duration * FPS, control.ROOT)
                    kind = "export" if busy % 2 == 0 else "archive"
                    dest = control.ROOT / "soak-export.mp4" if kind == "export" else os.devnull
                    subprocess.run(command(kind, extra, output=dest), check=True, timeout=45,
                                   stdout=subprocess.DEVNULL)
                    busy += 1
            if state:
                pids = [child.pid for child in [*node.services, live.process]]
                memory = vram(pids, node.backend)
                row = {**state, "elapsed_s": round(now, 2), "load": "busy" if interval % 2 else "idle",
                       "rss_mib": round(sum(rss(pid) for pid in pids), 2),
                       "worker_vram_mib": memory.get(live.process.pid), "vram_mib": sum(memory.values())}
                for device in node.probe["devices"]:
                    if device["backend"] == node.backend:
                        name = 'bacuri_acceleration_observed_memory_bytes{device="' + device["id"] + '"}'
                        value = control.metric("media", name)
                        row.setdefault("device_used_mib", {})[device["id"]] = None if value is None else round(value / 2**20, 2)
                if row["backend"] == node.backend and row["worker_vram_mib"] is None:
                    raise RuntimeError("Resident worker video memory is unavailable")
                print(json.dumps(row), file=samples, flush=True)
                if state["backend"] != node.backend and not state["switches"]:
                    raise RuntimeError(f"Permanent worker did not start on {node.backend}")
                if row["load"] == "idle" and now >= warmup and now % phase >= phase / 2:
                    if len(first) < 3:
                        first.append(row)
                    else:
                        last.append(row)
                save(node.output / "status.json", {**ready, **row, "state": "running", "busy_periods": busy})
            time.sleep(min(10, phase / 5, max(0.2, seconds - now)))
        if background:
            background_switches += finish_profile(background)
        # The target includes one picture at time zero.
        time.sleep(max(0, seconds - (time.monotonic() - start)))
        if not busy or len(first) < 3 or len(last) < 3:
            raise RuntimeError("Missing alternating load or comparable idle observations")
        growth = {}
        for key, limit in (("rss_mib", "rss_growth_mib"), ("vram_mib", "vram_growth_mib")):
            if any(row[key] is None for row in [*first, *last]):
                growth[limit] = None
            else:
                growth[limit] = round(median(row[key] for row in last) - median(row[key] for row in first), 2)
        switches = live.last["device_switches"] + background_switches
        passed = (all(v is not None and v <= limits[k] for k, v in growth.items())
                  and live.paths == [node.backend] and live.last["device_switches"] == 0 and switches <= limits["switch_limit"]
                  and live.last["steady_lag_ms"] <= 1000)
        summary = {**ready, **live.last, "state": "finishing", "elapsed_s": round(time.monotonic() - start, 2), "busy_periods": busy,
                   "growth": growth, "all_live_switches": switches, "within_limits": passed}
        save(node.output / "status.json", summary)
        if not passed:
            raise RuntimeError("Soak limits exceeded or required memory counter unavailable")
