"""Measure the server video workloads on the same four reference inputs in a Compose container."""

import argparse
import csv
import os
from pathlib import Path
import shlex
import statistics
import subprocess
import tempfile
import time

from desktop_reference_check import inspect


FPS = 25
FRAMES = 150
WORKER = Path("build/gcc-release/bacuri-acceleration-cost")
ENCODED = ("mask", "transcode", "scale720", "export-mask", "export-transcode")
WORKLOADS = (*ENCODED, "keyframe", "thumbnail", "motion")


def make_input(directory, codec, width, height):
    path = directory / f"{codec}-{width}x{height}.mp4"
    encoder = ("x264enc threads=2 speed-preset=veryfast bframes=0 option-string=scenecut=0"
               if codec == "h264" else "x265enc speed-preset=ultrafast option-string=pools=2:bframes=0:scenecut=0")
    pipeline = (f"videotestsrc num-buffers={FRAMES} pattern=smpte ! "
                f"video/x-raw,format=I420,width={width},height={height},framerate={FPS}/1 ! "
                f"{encoder} key-int-max={FPS} bitrate=6000 ! video/x-{codec},profile=main ! "
                f"{codec}parse ! mp4mux fragment-duration=2000 ! filesink location={path}")
    subprocess.run(["gst-launch-1.0", "-q", "-e", *shlex.split(pipeline)], check=True, timeout=90)
    expected = dict(codec=codec, profile="main", size=f"{width}x{height}", fps=FPS,
                    frames=FRAMES, keyframe_interval=FPS)
    if (actual := inspect(path)) != expected:
        raise ValueError(f"{path.name}: expected {expected}, found {actual}")
    return path


def pipeline(path, codec, workload, gpu):
    operation = workload.removeprefix("export-")
    parts = [f"filesrc location={path}", "qtdemux", f"{codec}parse config-interval=-1",
             f"video/x-{codec},alignment=au", "identity sync=true"]
    if workload in ("keyframe", "thumbnail"):
        parts.append("identity drop-buffer-flags=delta-unit")
    parts.append(f"nv{codec}dec" if gpu else f"avdec_{codec} max-threads=2")
    if operation == "transcode" and gpu:
        parts.append("video/x-raw(memory:CUDAMemory),format=NV12")
    elif workload in ("motion", "thumbnail"):
        parts.extend(["videoconvertscale", "video/x-raw,format=GRAY8,width=64,height=48,pixel-aspect-ratio=1/1"])
    elif workload == "keyframe":
        parts.append("video/x-raw")
    else:
        size = ",width=1280,height=720,pixel-aspect-ratio=1/1" if workload == "scale720" else ""
        parts.extend(["videoconvertscale", "video/x-raw,format=I420" + size])
    if operation == "mask":
        parts.append("identity name=mask")
    if workload in ENCODED:
        if gpu:
            if operation != "transcode":
                parts.extend(["videoconvert", "video/x-raw,format=NV12"])
            rate = "rc-mode=constqp qp-const-i=16 qp-const-p=16" if workload.startswith("export-") else "bitrate=2000"
            parts.append(f"nvh264enc {rate} gop-size=25 zerolatency=true bframes=0")
        else:
            rate = "pass=quant quantizer=16" if workload.startswith("export-") else "bitrate=2000"
            parts.append(f"x264enc {rate} key-int-max=25 threads=2 tune=zerolatency speed-preset=ultrafast")
        parts.extend(["video/x-h264,profile=constrained-baseline", "h264parse", "video/x-h264,alignment=au"])
    parts.append("appsink name=output max-buffers=2 sync=false async=false")
    return " ! ".join(parts)


def gpu_samples(log, pid):
    # pmon -s um columns start with device, PID, type, SM, memory, encoder and decoder.
    # Use the driver's header for framebuffer memory as optional engine columns vary by driver.
    log.seek(0)
    samples = []
    columns = []
    for line in log:
        fields = line.split()
        if fields[:2] == ["#", "gpu"]:
            columns = fields[1:]
        if len(fields) < 3 or fields[1] != str(pid):
            continue
        row = dict(zip(columns, fields))
        if row.get("fb", "-").isdigit() and int(row["fb"]) > 0:
            samples.append({key: int(value) for key, value in row.items() if value.isdigit()})
    if len(samples) < 2:
        raise RuntimeError(f"Insufficient per-process GPU samples for PID {pid}: {samples}")
    return samples


def measure(path, codec, workload, gpu):
    with tempfile.TemporaryFile(mode="w+") as log:
        sampler = subprocess.Popen(["nvidia-smi", "pmon", "-i", "0", "-s", "um"], stdout=log) if gpu else None
        try:
            start = time.monotonic()
            with subprocess.Popen([WORKER, pipeline(path, codec, workload, gpu), workload.removeprefix("export-")],
                                  stdout=subprocess.PIPE, text=True) as worker:
                try:
                    output, _ = worker.communicate(timeout=45)
                except subprocess.TimeoutExpired:
                    worker.kill()
                    worker.wait()
                    raise
                if worker.returncode:
                    raise RuntimeError(f"{path.name} {workload}: worker exited with {worker.returncode}")
            wall = time.monotonic() - start
        finally:
            if sampler:
                sampler.terminate()
                sampler.wait()
        frames, observations, cpu_us, rss_kib = map(int, output.split())
        expected = FRAMES // FPS if workload in ("keyframe", "thumbnail") else FRAMES
        expected_observations = (FRAMES + 6) // 7 if workload == "motion" else 0
        if (frames, observations) != (expected, expected_observations):
            raise ValueError(f"{workload}: expected {expected}/{expected_observations}, got {frames}/{observations}")
        samples = gpu_samples(log, worker.pid) if gpu else []
    seconds = FRAMES / FPS
    row = dict(input=path.stem, workload=workload, path="nvidia" if gpu else "cpu",
               input_frames=FRAMES, output_frames=frames, motion_samples=observations,
               wall_s=round(wall, 3), cpu_cores_at_25fps=round(cpu_us / 1e6 / seconds, 3),
               peak_rss_mib=round(rss_kib / 1024, 1),
               dec_sessions=int(gpu), enc_sessions=int(gpu and workload in ENCODED),
               sampled_vram_mib=max((s["fb"] for s in samples), default=0), gpu_samples=len(samples))
    for engine in ("sm", "enc", "dec"):
        readings = [s[engine] for s in samples if engine in s]
        if gpu and engine in ("enc", "dec") and row[f"{engine}_sessions"] and len(readings) < 2:
            raise RuntimeError(f"Insufficient {engine} readings for {path.name} {workload}")
        if readings:
            cost = round(statistics.mean(readings) * 10 / FPS, 2)
        else:
            cost = "" if gpu and engine == "sm" else 0
        row[f"{engine}_ms_per_input_frame"] = cost
    return row


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--backend", choices=("cpu", "nvidia"), default="cpu")
    args = parser.parse_args()
    out = Path("build/acceleration")
    out.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=out) as temporary:
        scratch = Path(temporary)
        results = scratch / "costs.csv"
        with results.open("w", newline="") as stream:
            writer = None
            for width, height in ((1920, 1080), (2560, 1440)):
                for codec in ("h264", "h265"):
                    path = make_input(scratch, codec, width, height)
                    for workload in WORKLOADS:
                        print(path.stem, workload, args.backend, flush=True)
                        row = measure(path, codec, workload, args.backend == "nvidia")
                        if writer is None:
                            writer = csv.DictWriter(stream, fieldnames=row, lineterminator="\n")
                            writer.writeheader()
                        writer.writerow(row)
        os.replace(results, out / f"{args.backend}.csv")


if __name__ == "__main__":
    main()
