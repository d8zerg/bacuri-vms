"""Check numbered pictures through the production codecs and change the node policy during a group of pictures."""
import base64
import json
import os
from pathlib import Path
import subprocess
import signal
import time

import registry as control

W, H, N = 320, 240, 96


def gst(*chain, output=False):
    return subprocess.run(["gst-launch-1.0", "-qe", *chain], check=True, stdout=subprocess.PIPE if output else None,
                          timeout=20).stdout


def reference(root, codec, count=N):
    raw = root / "frames.i420"
    with raw.open("wb") as data:
        for i in range(count):
            row = b"".join(bytes([225 if i & 1 << b else 30]) * 40 for b in range(8))
            data.write(row * 32 + bytes([195]) * (W * (H - 32)) + bytes([128]) * (W * H // 2))
    file = root / (codec + ".mp4" if count == N else "long-gop.mp4")
    enc = ["x264enc", "threads=2", "bframes=0", "option-string=scenecut=0"] if codec == "h264" else [
        "x265enc", "option-string=pools=none:frame-threads=2:bframes=0:scenecut=0"]
    gst("filesrc", f"location={raw}", "!", "rawvideoparse", "format=i420", "width=320", "height=240",
        "framerate=25/1", "!", *enc, f"key-int-max={count if count == N else 930}", "speed-preset=ultrafast", "tune=zerolatency",
        "!", codec + "parse", "!", "mp4mux", "!", "filesink", f"location={file}")
    return file


def verify_images(raw, numbers, masked=False, size=(W, H)):
    w, h = size
    stride = w * h
    assert len(raw) == len(numbers) * stride, (len(raw), len(numbers), size)
    for i, number in enumerate(numbers):
        frame = memoryview(raw)[i * stride:(i + 1) * stride]
        ordinal = sum((frame[(h // 16) * w + (2 * b + 1) * w // 16] > 128) << b for b in range(8))
        assert ordinal == number, (i, ordinal, number)
        center = frame[(h // 2) * w + w // 2]
        assert center < 32 if masked else center > 150, (i, center, masked)


def interrupt_block(args, task, apply):
    # Freeze only the fixture process after its first decoded/encoded picture. Let the node revoke
    # its grant, then resume the same process at the next poll with the unfinished output on disk.
    with subprocess.Popen(args, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True) as worker:
        try:
            for line in worker.stderr:
                if line.startswith('{"backend"') and json.loads(line)["kind"] == task:
                    break
            else:
                raise AssertionError(f"{task} did not start")
            os.kill(worker.pid, signal.SIGSTOP)
            apply("cpu_only")
            time.sleep(0.8)
        finally:
            if worker.poll() is None:
                os.kill(worker.pid, signal.SIGCONT)
        stderr = worker.stderr.read()
        assert worker.wait(timeout=15) == 0, stderr
    assert any(line.startswith('{"backend"') and json.loads(line)["kind"] == task and json.loads(line)["backend"] == "cpu"
               for line in stderr.splitlines()), (task, stderr)


def product_pipelines(expected, apply):
    root = Path(os.environ["BACURI_PROBE_DIRECTORY"])
    kinds = ["live_mask", "live_profile", "live_analysis", "export", "archive"]
    for kind in kinds:
        codec = "h265" if kind in ("live_profile", "archive") else "h264"
        file = root / (codec + ".mp4")
        if not file.exists():
            reference(root, codec)
        apply("cpu_only" if expected == "cpu" else "auto")
        destination = root / (kind + ".out")
        args = [control.BIN / "bacuri-pipeline-check", kind, codec, file, destination]
        with subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True) as worker:
            if kind.startswith("live_"):
                started = worker.stdout.readline().strip()
                assert started == "started " + expected, (kind, started, worker.stderr.read() if not started else "")
                if expected != "cpu":
                    apply("cpu_only")
            _, stderr = worker.communicate(timeout=15)
            assert worker.returncode == 0, (kind, stderr)
        reports = [json.loads(line) for line in stderr.splitlines() if line.startswith('{"backend"')]
        paths = [(row["kind"], row["backend"]) for row in reports]
        for task in (["keyframe", "thumbnail", "archive_analysis"] if kind == "archive" else [kind]):
            assert (task, expected) in paths, paths
        if kind.startswith("live_") and expected != "cpu":
            assert paths == [(kind, expected), (kind, "cpu")], paths
        def verify_media():
            if kind == "archive":
                index = json.loads(destination.read_text())
                assert index["frames"] == [i * 40000 for i in range(N)]
                for sample in index["samples"]:
                    verify_images(base64.b64decode(sample["image"]), [sample["at"] // 40000], size=(64, 48))
                return index
            if kind == "live_analysis":
                verify_images(destination.read_bytes(), range(N), size=(64, 48))
            else:
                demux = ["qtdemux", "!"] if kind == "export" else []
                raw = gst("filesrc", f"location={destination}", "!", *demux, "h264parse", "!", "avdec_h264",
                          "!", "videoconvert", "!", "video/x-raw,format=GRAY8", "!", "fdsink", "fd=1", "sync=false",
                          output=True)
                verify_images(raw, range(5, 80) if kind == "export" else range(N), kind != "live_profile")

        first = verify_media()
        if kind == "archive":
            assert all(sample["backend"] == expected for sample in first["samples"])
            subprocess.run(args, check=True, capture_output=True, timeout=15)
            assert first == json.loads(destination.read_text())
        if expected != "cpu" and kind in ("archive", "export"):
            interrupt_block(args, "archive_analysis" if kind == "archive" else kind, apply)
            resumed = verify_media()
            if kind == "archive":
                assert all(sample["backend"] == "cpu" for sample in resumed["samples"])
                assert [s["at"] for s in first["samples"]] == [s["at"] for s in resumed["samples"]]
        print(f"{kind}: {expected}, frame numbers, content and processing paths verified", flush=True)
    apply("cpu_only" if expected == "cpu" else "auto")
    file = reference(root, "h264", 936)
    output = root / "long-gop.h264"
    run = subprocess.run([control.BIN / "bacuri-pipeline-check", "long_gop", "h264", file, output],
                         capture_output=True, text=True, timeout=25)
    assert run.returncode == 0 and run.stdout.strip() == "started " + expected, (run.stdout, run.stderr)
    paths = [json.loads(line)["backend"] for line in run.stderr.splitlines() if line.startswith('{"backend"')]
    assert paths and paths[0] == expected and "cpu" in paths, paths
    if expected == "cpu":
        assert set(paths) == {"cpu"}, paths
    raw = gst("filesrc", f"location={output}", "!", "h264parse", "!", "avdec_h264", "!", "videoconvert",
              "!", "video/x-raw,format=GRAY8", "!", "fdsink", "fd=1", "sync=false", output=True)
    verify_images(raw, [i % 256 for i in range(936)], masked=True)
    print(f"Long keyframe interval: 936 consecutive masked pictures, paths {paths}", flush=True)
