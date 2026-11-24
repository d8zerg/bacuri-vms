"""Verify five minutes of protected video per camera on a bounded, disposable volume."""

from contextlib import ExitStack
from pathlib import Path
import hashlib
import json
import os
import shutil
import sqlite3
import subprocess
import time
import uuid

import playback
from playback import control as c

MINUTES = 5
START = -MINUTES * 60
END = START + MINUTES * 60


def fixture(camera, pattern):
    template = c.ROOT / f"{pattern}.mp4"
    subprocess.run(
        [
            "gst-launch-1.0",
            "-q",
            "videotestsrc",
            "num-buffers=1500",
            f"pattern={pattern}",
            "!",
            "video/x-raw,format=I420,width=64,height=48,framerate=25/1",
            "!",
            "x264enc",
            "tune=zerolatency",
            "key-int-max=25",
            "!",
            "h264parse",
            "!",
            "mp4mux",
            "fragment-duration=1000",
            "!",
            "filesink",
            "location=" + str(template),
        ],
        check=True,
    )
    decoded = subprocess.check_output(
        [
            "gst-launch-1.0",
            "-q",
            "filesrc",
            "location=" + str(template),
            "!",
            "qtdemux",
            "!",
            "decodebin",
            "!",
            "videoconvert",
            "!",
            "video/x-raw,format=GRAY8",
            "!",
            "fdsink",
            "fd=1",
        ]
    )
    assert len(decoded) == 1500 * 64 * 48
    digest = hashlib.sha256(template.read_bytes()).digest()
    blocks = []
    for minute in range(MINUTES):
        directory = playback.ROOT / "blocks" / str(uuid.uuid4())
        directory.mkdir()
        shutil.copyfile(template, directory / "media.mp4")
        m = playback.register_fixture(camera, directory, START + 60 * minute, 60)
        blocks.append((m["block_id"], digest))
    return blocks


def run():
    assert subprocess.check_output(["stat", "-f", "-c", "%T", playback.ROOT], text=True).strip() == "tmpfs"
    replica = c.ROOT / "replica"
    replica.mkdir()
    os.environ["BACURI_ARCHIVE_REPLICA_DIR"] = str(replica)
    with ExitStack() as stack:

        def start(args, **kwargs):
            process = subprocess.Popen(args, **kwargs)
            stack.callback(c.stop, process)
            return process

        subprocess.run(
            [c.CORE, "create-user", "admin", "administrator"], input=c.PASSWORD + "\n", text=True, check=True
        )
        start([c.CORE, "serve"])
        c.wait_for(lambda: c.http("GET", "session")[0] == 401, "core ready")
        admin = c.login("admin")

        def post(path, body):
            status, _, result = c.http(
                "POST", f"sites/{c.SITE}/" + path, body, {**admin, "Idempotency-Key": str(uuid.uuid4())}
            )
            assert status == 201, (status, result)
            return result

        blocks = []
        for pattern in ("ball", "blink", "smpte75", "checkers-4"):
            camera = post(
                "cameras",
                {
                    "name": pattern,
                    "main_url": "rtsp://rtsp-camera:8554/main",
                    "secondary_url": "rtsp://rtsp-camera:8554/sub",
                },
            )
            blocks.extend(fixture(camera, pattern))
            post(
                f"points/{camera['point_id']}/holds",
                {
                    "interval": playback.interval(START, END),
                    "reason": "Preserve loading interval",
                    "expires_at": playback.utc(86400),
                },
            )
        assert c.query("SELECT count(*) FROM bacuri_archive.held_blocks") == str(len(blocks))
        env = {**os.environ, "BACURI_KEY_FILE": str(c.ROOT / "recorder.key")}
        recorder = start([c.RECORDER], env=env)
        backup = replica / f"bacuri-{c.SITE}-{os.environ['BACURI_NODE_ID']}"
        deadline = time.monotonic() + 60
        while not all((backup / ident / "media.mp4").is_file() for ident, _ in blocks):
            if time.monotonic() > deadline:
                raise AssertionError("Archive copy backlog did not drain")
            time.sleep(0.5)
        # Advance retirement through the held intervals, then exhaust only the private test mount.
        cutoff = playback.ORIGIN_US + END * 1000000
        c.query(
            f"BEGIN; SELECT pg_advisory_xact_lock(hashtextextended('{c.SITE}',1)); INSERT INTO bacuri_archive.retired_through VALUES('{c.SITE}',{cutoff}) ON CONFLICT(site_id) DO UPDATE SET end_us=greatest(excluded.end_us,bacuri_archive.retired_through.end_us); SELECT bacuri_archive.reap_expired('{c.SITE}',{cutoff}); COMMIT"
        )
        filler = playback.ROOT.parent / "capacity-test.bin"
        with filler.open("wb") as out:
            usage = shutil.disk_usage(playback.ROOT)
            os.posix_fallocate(out.fileno(), 0, max(1, usage.free - usage.total // 6))

            def stopped():
                status = json.loads(subprocess.check_output([c.RECORDER, "archive-status"], env=env))
                return status if status["state"] == "recording_stopped" else None

            c.wait_for(stopped, "writer reserve")
            assert c.query(
                "SELECT count(*) FROM bacuri_archive.retained_blocks b JOIN bacuri_archive.held_blocks h USING(site_id,block_id)"
            ) == str(len(blocks))
        filler.unlink()
        c.stop(recorder)
        # Rebuild the local journal from manifests and restore the protected set from the durable catalog.
        (playback.ROOT / "journal.db").rename(playback.ROOT.parent / "previous-journal.db")
        for suffix in ("-wal", "-shm"):
            aux = playback.ROOT / ("journal.db" + suffix)
            if aux.exists():
                aux.rename(playback.ROOT.parent / ("previous-journal.db" + suffix))
        began = time.monotonic()
        rebuilt = json.loads(subprocess.check_output([c.CORE, "rebuild-archive"]))
        assert rebuilt["verified"] >= len(blocks), rebuilt
        total = 0
        for ident, digest in blocks:
            source = playback.ROOT / "blocks" / ident / "media.mp4"
            assert hashlib.sha256(source.read_bytes()).digest() == digest
            assert hashlib.sha256((backup / ident / "media.mp4").read_bytes()).digest() == digest
            total += source.stat().st_size
        with sqlite3.connect(playback.ROOT / "journal.db") as db:
            assert db.execute("SELECT count(*) FROM protected_blocks").fetchone()[0] == len(blocks)
        report = {
            "cameras": 4,
            "minutes_per_camera": MINUTES,
            "frames": 4 * MINUTES * 1500,
            "verified_blocks": len(blocks),
            "media_bytes_per_copy": total,
            "lost_held_frames": 0,
            "rebuild_and_verify_seconds": round(time.monotonic() - began, 3),
        }
        if target := os.environ.get("BACURI_WORKSPACE_RESULTS"):
            (Path(target) / "protected-recording.json").write_text(json.dumps(report, indent=2))
        print("Verified protected recording", json.dumps(report), flush=True)


if __name__ == "__main__":
    run()
