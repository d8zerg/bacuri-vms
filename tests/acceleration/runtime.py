"""Exercise node grants over the service socket and process a paced masked stream across a reported driver reset."""
import json
import os
from pathlib import Path
import subprocess
import time
import uuid
from contextlib import ExitStack, contextmanager

import registry as control
from pipelines import product_pipelines


class Task:
    def __init__(self, kind):
        self.id = str(uuid.uuid4())
        self.kind = kind
        self.grant = ""

    def update(self, report="POLL"):
        args = [control.BIN / "bacuri-scheduler-worker", self.id, self.kind, report,
                self.grant if report != "POLL" else ""]
        value = json.loads(subprocess.check_output(args, text=True))
        self.grant = value["grant"]
        return value["device"]


def mask_worker(source, codecs):
    pipeline = " ! ".join([source, "h264parse", codecs["h264_dec"], "videoconvert", "video/x-raw,format=I420",
                           "identity name=mask", "videoconvert", codecs["h264_enc"], "h264parse",
                           "video/x-h264,alignment=au", "appsink name=output max-buffers=2 sync=false"])
    return subprocess.Popen([control.BIN / "bacuri-acceleration-cost", pipeline, "paced-mask"],
                            text=True, stdout=subprocess.PIPE)


def frames(worker):
    output, _ = worker.communicate(timeout=5)
    assert worker.returncode == 0, (worker.returncode, output)
    return int(output.split()[0])


def loaded_node(codecs, read):
    source = ("videotestsrc is-live=true num-buffers=75 pattern=ball ! "
              "video/x-raw,format=I420,width=1920,height=1080,framerate=25/1 ! "
              "x264enc threads=2 tune=zerolatency speed-preset=ultrafast")
    lives = []
    for _ in range(32):
        task = Task("live_mask")
        device = task.update()
        if device == "cpu":
            task.update("FINISHED")
            break
        lives.append((task, device))
    else:
        raise AssertionError("Live load did not reach its measured limit")
    assert lives, read()
    export = Task("export")
    assert export.update() == "cpu"
    started = time.monotonic()
    with ExitStack() as cleanup:
        workers = []
        for device in [d for _, d in lives] + ["cpu"]:
            child = mask_worker(source, codecs[device])
            cleanup.callback(control.stop, child)
            workers.append(child)
        while any(worker.poll() is None for worker in workers):
            assert time.monotonic() - started < 5, "Paced video exceeded its delivery window"
            for task, device in lives:
                assert task.update() == device
            assert export.update() == "cpu"
            time.sleep(0.25)
        assert [frames(worker) for worker in workers] == [75] * len(workers)
    for task, _ in lives:
        task.update("FINISHED")
    export.update("FINISHED")
    print(f"25 fps full-load fixture: {len(lives)} masked GPU streams and one CPU export, 75/75 frames each", flush=True)


def continuity(codecs, read):
    task = Task("live_mask")
    gpu = task.update()
    assert gpu != "cpu"
    directory = Path(os.environ["BACURI_PROBE_DIRECTORY"])
    # A camera produces closed one-second GOPs. Its original stream is recorded continuously.
    seconds = 42
    pattern = directory / "continuity-%03d.mp4"
    command = ["gst-launch-1.0", "-qe", "videotestsrc", "is-live=true", "pattern=ball", f"num-buffers={25 * seconds}",
               "!", "video/x-raw,format=I420,width=1920,height=1080,framerate=25/1", "!", "x264enc", "threads=2",
               "tune=zerolatency", "speed-preset=ultrafast", "key-int-max=25", "!", "h264parse", "!", "splitmuxsink",
               "max-size-time=1000000000", f"location={pattern}"]
    last = gpu
    paths = [gpu]
    produced = 0
    with ExitStack() as cleanup:
        camera = subprocess.Popen(command)
        cleanup.callback(control.stop, camera)
        for index in range(seconds):
            next_segment = Path(str(pattern) % (index + 1))
            until = time.monotonic() + 5
            while not next_segment.exists() and camera.poll() is None:
                assert time.monotonic() < until, "Original recording stopped"
                time.sleep(0.02)
            assert camera.poll() in (0, None)
            # The previous segment's GPU pipeline is closed when the worker reports the error.
            path = task.update("DRIVER_RESET" if index == 2 else "POLL")
            if index == 0 or path != last:
                active = [row for row in read()["assignments"] if row["task"] == "live_mask"]
                assert len(active) == 1 and (active[0]["device_id"], active[0]["count"]) == (path, 1), active
            if index == 2:
                assert path == "cpu"
                assert {"device_id": gpu, "reason": "driver_reset"} in read()["warnings"]
            if path != last:
                paths.append(path)
            last = path
            segment = str(pattern) % index
            tick = time.monotonic()
            worker = mask_worker(f"filesrc location={segment} ! qtdemux", codecs[path])
            try:
                count = frames(worker)
            finally:
                control.stop(worker)
            assert count == 25 and time.monotonic() - tick < 1.0, (index, path, count)
            produced += count
        assert camera.wait(timeout=3) == 0
    task.update("FINISHED")
    assert paths == [gpu, "cpu", gpu] and produced == seconds * 25, (paths, produced)
    state = read()
    assert not state["warnings"] and any(row["reason"] == "check_passed" for row in state["journal"]), state
    metrics = (directory / "media.prom").read_text()
    for name in ("transitions_total", "encoder_sessions", "tasks", "memory_bytes", "time_ppm"):
        assert f"bacuri_acceleration_{name}" in metrics
    assert 'reason="driver_reset"' in metrics
    print(f"Reported reset: {paths}, {seconds} consecutive recorded GOPs and {produced}/{produced} masked frames", flush=True)


@contextmanager
def encoders(factory, count, available=True):
    with subprocess.Popen([control.BIN / "bacuri-scheduler-worker", "--hold-encoders", factory, str(count)],
                          text=True, stdin=subprocess.PIPE, stdout=subprocess.PIPE) as worker:
        try:
            assert worker.stdout.readline().strip() == ("ready" if available else "unavailable")
            yield
        finally:
            worker.stdin.close()
            assert worker.wait(timeout=4) == (0 if available else 3)


def exhausted_sessions(result, codecs, read):
    limits = [row for row in result["measurements"] if row["operation"] == "sessions" and row["codec"] == "h264"
              and row["state"] == "measured" and not row.get("reached_ceiling")]
    if not limits:
        return
    live = Task("live_mask")
    background = Task("export")
    device = background.update()
    if device == "cpu":
        background.update("FINISHED")
        return
    capacity = next((row["verified_sessions"] for row in limits if row["device_id"] == device), 0)
    if live.update() != device or capacity < 2:
        live.update("FINISHED")
        background.update("FINISHED")
        return
    factory = codecs[device]["h264_enc"]
    with encoders(factory, capacity - 1):
        assert background.update() == device
        with encoders(factory, 1):
            assert live.update() == device
            with encoders(factory, 1, available=False):
                assert live.update("SESSION_LIMIT") == "cpu"
                assert background.update() == "cpu"
        assert background.update("RELEASED") == "cpu"
        assert live.update() == device
        with encoders(factory, 1):
            assert {"device_id": device, "reason": "session_limit"} in read()["warnings"]
    live.update("FINISHED")
    background.update("FINISHED")
    print(f"NVENC refused session {capacity + 1}; export released its encoder and the new live encoder opened", flush=True)


def check_scheduler(route, admin, revision, expected, result, read_probe):
    read = lambda: read_probe()["scheduler"]
    cpu_task = Task("live_mask")
    if expected == "cpu":
        assert cpu_task.update() == "cpu" and read()["tasks"]["cpu"] == 1
        cpu_task.update("FINISHED")
        assert read()["tasks"]["cpu"] == 0
        product_pipelines(expected, lambda _: None)
        return revision
    original = control.http("GET", route, headers=admin)[2]
    manual = {"mode": original["mode"]}
    if manual["mode"] == "manual":
        manual["gpu_tasks"] = original["gpu_tasks"]
    def apply(settings, rev):
        code, _, value = control.http("PUT", route, settings, {**admin, "If-Match": f'"{rev}"'})
        assert code == 200, value
        control.wait_for(lambda: read()["mode"] == settings["mode"], "applied scheduler policy")
        return value["revision"]
    revision = apply({"mode": "auto"}, revision)
    codecs = {dev["id"]: dev["codecs"] for dev in result["devices"]}
    codecs["cpu"] = {"h264_dec": "avdec_h264 max-threads=2", "h264_enc": "x264enc threads=2 tune=zerolatency speed-preset=ultrafast"}
    def set_mode(mode):
        nonlocal revision
        revision = apply({"mode": mode}, revision)
    product_pipelines(expected, set_mode)
    set_mode("auto")
    loaded_node(codecs, read)
    continuity(codecs, read)
    if expected == "nvidia":
        metrics = (Path(os.environ["BACURI_PROBE_DIRECTORY"]) / "media.prom").read_text()
        for metric in ("observed_memory_bytes", "observed_encoder_sessions", "engine_busy_ratio"):
            assert f"bacuri_acceleration_{metric}{{" in metrics, metric
        exhausted_sessions(result, codecs, read)
    return apply(manual, revision)
