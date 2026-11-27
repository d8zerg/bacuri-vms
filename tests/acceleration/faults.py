"""Observe live delivery and original recording across isolated device failures."""
from contextlib import ExitStack
import ctypes
import json
import os
from pathlib import Path
import subprocess
import sys
import time
import uuid

from qualification import FPS, command, control, make_input, save
from runtime import encoders, Task
from soak import Live

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "live"))
from streaming import archive_blocks, coverage


def oversized_allocation(ordinal):
    # Exercise the real out-of-memory return without consuming the workstation's free memory.
    cuda = ctypes.CDLL("libcuda.so.1")
    device = ctypes.c_int()
    context = ctypes.c_void_p()
    def check(code):
        if code:
            raise RuntimeError(f"CUDA driver returned {code}")
    check(cuda.cuInit(0))
    check(cuda.cuDeviceGet(ctypes.byref(device), ordinal))
    check(cuda.cuCtxCreate_v2(ctypes.byref(context), 0, device))
    try:
        total = ctypes.c_size_t()
        available = ctypes.c_size_t()
        check(cuda.cuMemGetInfo_v2(ctypes.byref(available), ctypes.byref(total)))
        pointer = ctypes.c_uint64()
        size = total.value + 65536
        result = cuda.cuMemAlloc_v2(ctypes.byref(pointer), ctypes.c_size_t(size))
        if result != 2:
            raise RuntimeError(f"Expected CUDA_ERROR_OUT_OF_MEMORY, got {result}")
        return {"requested_bytes": size, "available_bytes": available.value, "cuda_result": result}
    finally:
        check(cuda.cuCtxDestroy_v2(context))


def wait_path(live, backend, seconds):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        current = live.progress()
        if current.get("backend") == backend:
            return dict(current)
        time.sleep(0.2)
    raise RuntimeError(f"Expected live path {backend}, got {live.last}")


def report_fault(event, device):
    worker = Task("live_mask")
    try:
        control.wait_for(lambda: worker.update() != "cpu", f"active grant for {event}")
        if worker.update() != device:
            raise RuntimeError(f"The fault worker was assigned to a different GPU from {device}")
        if worker.update(event) != "cpu":
            raise RuntimeError(f"{event} did not release its grant")
    finally:
        worker.update("FINISHED")


def measure(node):
    if node.backend == "cpu" or not node.apply("auto"):
        raise RuntimeError("Fault qualification requires an active hardware backend")
    source = make_input(control.ROOT, "h264", 1920, 1080)
    route = f"sites/{control.SITE}/cameras"
    code, _, camera = control.http("POST", route, {"name": "Recording reference", "main_url": "rtsp://rtsp-camera:8554/main",
                                                     "secondary_url": "rtsp://rtsp-camera:8554/sub"},
                                   {**node.headers, "Idempotency-Key": str(uuid.uuid4())})
    if code != 201:
        raise RuntimeError((code, camera))
    point = camera["point_id"]
    records = []
    with ExitStack() as stack:
        recorder = subprocess.Popen([control.RECORDER], env={**os.environ, "BACURI_KEY_FILE": str(control.ROOT / "recorder.key")})
        stack.callback(control.stop, recorder)
        control.wait_for(lambda: len(archive_blocks(point)) >= 3, "reference recorder")
        start_block = archive_blocks(point)[-1][0]
        live = Live(stack, "live_mask", source, 220 * FPS, node.output)
        wait_path(live, node.backend, 10)
        def mask_device():
            assignments = node.request("GET", "/probe")[2]["scheduler"]["assignments"]
            return next((a["running_device_id"] for a in assignments if a["task"] == "live_mask"
                         and a.get("running_device_id") not in (None, "cpu")), None)
        active_device = control.wait_for(mask_device, "physical device of the masked stream")
        device = next(d for d in node.probe["devices"] if d["id"] == active_device)
        for event in ("reported_driver_reset", "worker_device_hidden", "cuda_allocation_refused", "encoder_sessions"):
            before = dict(live.progress())
            evidence = {"case": event}
            if event == "reported_driver_reset":
                report_fault("DRIVER_RESET", active_device)
            elif event == "worker_device_hidden":
                if node.backend != "nvidia":
                    records.append({**evidence, "state": "requires separate device removal"})
                    continue
                denied = subprocess.run(command("live_mask", source), env={**os.environ, "CUDA_VISIBLE_DEVICES": ""},
                                        capture_output=True, text=True, timeout=20, check=True)
                evidence["second_stream"] = json.loads(denied.stdout.splitlines()[-1])
                if evidence["second_stream"]["backend"] != "cpu":
                    raise RuntimeError("Hidden device remained usable")
            elif event == "cuda_allocation_refused":
                if node.backend != "nvidia":
                    records.append({**evidence, "state": "requires a hardware-specific memory test"})
                    continue
                evidence.update(oversized_allocation(device["ordinal"]))
                report_fault("ENCODER_ERROR", active_device)
            else:
                limits = [m for m in node.probe["measurements"] if m["device_id"] == device["id"]
                          and m["operation"] == "sessions" and m["codec"] == "h264" and not m.get("reached_ceiling")]
                if not limits:
                    records.append({**evidence, "state": "probe stopped at its ceiling"})
                    continue
                count = limits[0]["verified_sessions"]
                with encoders(device["codecs"]["h264_enc"], count - 1):
                    with encoders(device["codecs"]["h264_enc"], 1, available=False):
                        evidence["refused_session"] = count + 1
                    fallback = subprocess.run(command("live_mask", source), capture_output=True, text=True,
                                              check=True, timeout=20)
                    evidence["second_stream"] = json.loads(fallback.stdout.splitlines()[-1])
                    warnings = node.request("GET", "/probe")[2]["scheduler"]["warnings"]
                    if {"device_id": device["id"], "reason": "session_limit"} not in warnings:
                        raise RuntimeError(f"Encoder exhaustion classified incorrectly: {warnings}")
                    if evidence["second_stream"]["backend"] != "cpu":
                        raise RuntimeError("New real encoder did not fall back to CPU")
            if event != "encoder_sessions":
                evidence["fallback"] = wait_path(live, "cpu", 8)
                reason = {"reported_driver_reset": "driver_reset", "worker_device_hidden": "device_lost",
                          "cuda_allocation_refused": "encoder_error"}[event]
                warnings = node.request("GET", "/probe")[2]["scheduler"]["warnings"]
                if not any(w["reason"] == reason for w in warnings):
                    raise RuntimeError((reason, warnings))
            evidence["recovered"] = wait_path(live, node.backend, 60)
            expected = before["switches"] + (0 if event == "encoder_sessions" else 2)
            if evidence["recovered"]["switches"] != expected:
                raise RuntimeError(f"Unexpected number of device transitions during {event}")
            if evidence["recovered"]["frames"] <= before["frames"]:
                raise RuntimeError("Masked stream made no progress during fault")
            records.append(evidence)
            save(node.output / "faults.json", records)
            save(node.output / "status.json", {"state": "running", "phase": "faults", "backend": node.backend,
                                               "last_case": event, "frames": evidence["recovered"]["frames"]})
        live.process.wait(timeout=230)
        finished = live.finish()
        if finished["steady_lag_ms"] > 1000:
            raise RuntimeError(f"Masked live delivery exceeded one second: {finished}")
        # Wait for the GOP containing the final live frame to be published.
        final_block = archive_blocks(point)[-1][0]
        control.wait_for(lambda: archive_blocks(point)[-1][0] > final_block, "final recorded GOP")
        blocks = [b for b in archive_blocks(point) if b[0] >= start_block]
        if len(blocks) < 219 or any(coverage(point, blocks)) or recorder.poll() is not None:
            raise RuntimeError("Original recording is incomplete")
        save(node.output / "faults.json", {"cases": records, "live": finished, "recorded_blocks": len(blocks),
                                         "recorded_frames": 25 * len(blocks)})
