"""Recover a disposable site through the production administrative and HTTPS entry points."""
from contextlib import ExitStack
from datetime import datetime, timezone
import errno
import fcntl
import json
import os
from pathlib import Path
import shutil
import secrets
import subprocess
import sys
import time
from urllib.parse import urlencode
import uuid
from xml.etree import ElementTree

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "archive"))
import playback
from recording import local
from playback import control as c

ROOT = Path(os.environ["BACURI_ARCHIVE_DIR"])
REPORT = Path(os.environ["BACURI_RECOVERY_REPORT"])
CAMERAS = f"sites/{c.SITE}/cameras"
POINTS = f"sites/{c.SITE}/points"
RECORDER_ENV = dict(os.environ, BACURI_KEY_FILE=str(c.ROOT / "recorder.key"))
metrics = {}


def fault(action):
    print("FAULT " + action, flush=True)
    assert sys.stdin.readline().strip() == "ok"


def core(action, *args, **opts):
    return subprocess.run([c.CORE, action, *args], capture_output=True, timeout=30, **opts)


def op(action, *args, **opts):
    return core(action, *args, check=True, **opts).stdout


class Measurement:
    def __init__(self):
        self.started = time.monotonic()
        self.utc_us = time.time_ns() // 1000

    def finish(self, duration="elapsed_s"):
        return {"fault_utc_us": self.utc_us, "recovered_utc_us": time.time_ns() // 1000,
                duration: round(time.monotonic() - self.started, 3)}


def stamp(us):
    return datetime.fromtimestamp(us // 1_000_000, timezone.utc).replace(
        microsecond=us % 1_000_000).isoformat(timespec="microseconds").replace("+00:00", "Z")


def bounds(m):
    return {"start": stamp(m["begin_utc_us"]), "end": stamp(m["end_utc_us"])}


def catalog():
    return {m["block_id"]: m for m in map(json.loads, c.query(
        "SELECT manifest FROM bacuri_archive.blocks WHERE unavailable_reason IS NULL ORDER BY begin_utc_us"
    ).splitlines())}


def create(point, m, viewer):
    return c.http("POST", f"{POINTS}/{point}/media-sessions", {"mode": "archive", "interval": bounds(m)},
                  {**viewer, "Idempotency-Key": str(uuid.uuid4())})


def read_block(m, viewer):
    status, _, session = create(m["point_id"], m, viewer)
    assert status == 201, (status, session)
    base = "media-sessions/" + session["id"]
    try:
        status, _, response = c.http("GET", base + "/manifest", headers=viewer)
        assert status == 200, response
        part, = response["parts"]
        payload = bytearray()
        for key in ("init_id", "media_id"):
            status, data = playback.binary(base + "/parts/" + part[key], viewer)
            assert status == 200 and data
            payload.extend(data)
        source = (ROOT / "blocks" / m["block_id"] / "media.mp4").read_bytes()
        assert source.startswith(payload)
        pixels = subprocess.run(["gst-launch-1.0", "-q", "fdsrc", "!", "qtdemux", "!", "h264parse", "!",
                                 "avdec_h264", "!", "videoscale", "!", "videoconvert", "!",
                                 "video/x-raw,format=GRAY8,width=16,height=16", "!", "fdsink", "fd=1", "sync=false"],
                                input=payload, capture_output=True, check=True, timeout=10).stdout
        assert pixels and len(pixels) % 256 == 0
    finally:
        assert c.http("DELETE", base, headers=viewer)[0] == 204


def wait_new(known):
    return c.wait_for(lambda: next((m for k, m in catalog().items() if k not in known), None), "new catalog block")


def gap(reason):
    value = c.query(f"SELECT point_id,begin_utc_us,end_utc_us FROM bacuri_archive.gaps WHERE reason='{reason}' "
                    "ORDER BY end_utc_us-begin_utc_us DESC LIMIT 1")
    if value:
        point, begin, end = value.split("|")
        return {"point_id": point, "begin_utc_us": int(begin), "end_utc_us": int(end),
                "gap_us": int(end) - int(begin), "reason": reason}


def postgres_outage():
    measurement = Measurement()
    known = set(catalog())
    fault("stop postgres")
    committed = local("SELECT count(*) FROM committed_blocks")[0][0]
    c.wait_for(lambda: local("SELECT count(*) FROM committed_blocks")[0][0] >= committed + 2, "offline writes")
    backlog = {r[0] for r in local("SELECT block_id FROM committed_blocks WHERE registered_at_utc_us IS NULL")}
    assert len(backlog) >= 2
    fault("start postgres")
    c.wait_for(lambda: (known | backlog) <= catalog().keys(), "backlog replay")
    metrics["postgres"] = {"committed_offline": len(backlog), "lost_blocks": 0, **measurement.finish()}


def camera_outage():
    measurement = Measurement()
    fault("stop rtsp-camera")
    time.sleep(3)
    fault("start rtsp-camera")
    outage = c.wait_for(lambda: gap("source_offline"), "camera outage interval")
    wait_new(set(catalog()))
    metrics["camera"] = {**outage, **measurement.finish()}


def media_outage(start, media, first, viewer):
    point = first["point_id"]
    measurement = Measurement()
    c.stop(media)
    known = set(catalog())
    status, _, session = create(point, first, viewer)
    assert status == 201
    base = "media-sessions/" + session["id"]
    part, = c.http("GET", base + "/manifest", headers=viewer)[2]["parts"]
    url = base + "/parts/" + part["media_id"]
    assert playback.binary(url, viewer)[0] == 503
    wait_new(known)
    media = start(playback.MEDIA)
    c.wait_for(lambda: playback.binary(url, viewer)[0] == 200, "media restart")
    metrics["media"] = {"recording_continued": True, **measurement.finish()}
    return media, url


def full_volume():
    measurement = Measurement()
    filler = ROOT.parent / "fill"
    with filler.open("xb") as file:
        def fill():
            stat = os.statvfs(ROOT)
            assert stat.f_blocks * stat.f_frsize == 64 * 1024 * 1024
            if stat.f_bavail:
                try:
                    os.posix_fallocate(file.fileno(), file.seek(0, os.SEEK_END), stat.f_bavail * stat.f_frsize)
                except OSError as error:
                    if error.errno != errno.ENOSPC:
                        raise
            return os.statvfs(ROOT).f_bavail == 0
        c.wait_for(fill, "no free blocks on private archive")
        c.wait_for(lambda: b"archive storage_failure" in (c.ROOT / "recorder.log").read_bytes(), "full volume")
    filler.unlink()
    wait_new(set(catalog()))
    outage = c.wait_for(lambda: gap("storage_failure"), "storage gap")
    metrics["volume_full"] = {**outage, **measurement.finish()}


def run():
    boundaries = ElementTree.parse(REPORT.with_suffix(".boundaries.xml")).getroot()
    assert boundaries.get("tests") == "6" and boundaries.get("skipped") == "0"
    database = os.environ["PGDATABASE"]
    assert database.startswith("bacuri_control_") and database.replace("_", "").isalnum()
    with ExitStack() as stack:
        log = stack.enter_context((c.ROOT / "recorder.log").open("wb"))
        def start(*cmd, **opts):
            process = subprocess.Popen(cmd, **opts)
            stack.callback(c.stop, process)
            return process

        def start_core():
            process = start(c.CORE, "serve")
            c.wait_for(lambda: c.http("GET", "session")[0] == 401, "HTTPS ready")
            return process

        def start_recorder():
            return start(c.RECORDER, env=RECORDER_ENV, stderr=log)

        for name, role in (("admin", "administrator"), ("viewer", "operator")):
            op("create-user", name, role, input=(c.PASSWORD + "\n").encode())
        server = start_core()
        media = start(playback.MEDIA)
        admin = c.login("admin")
        config = {"name": "Recovery camera", "main_url": "rtsp://rtsp-camera:8554/main",
                  "secondary_url": "rtsp://rtsp-camera:8554/sub",
                  "credentials": {"username": "camera", "password": secrets.token_hex(16)}}
        status, _, camera = c.http("POST", CAMERAS, config, {**admin, "Idempotency-Key": str(uuid.uuid4())})
        assert status == 201, camera
        foreign = str(uuid.uuid4())
        c.query(f"INSERT INTO bacuri_inventory.observation_points VALUES('{c.SITE}','{foreign}','Restricted')")
        point = camera["point_id"]
        op("grant-point", "viewer", point)
        recorder = start_recorder()
        c.wait_for(lambda: len(catalog()) >= 2, "initial recording")
        first = next(iter(catalog().values()))
        viewer = c.login("viewer")
        read_block(first, viewer)

        postgres_outage()
        camera_outage()
        media, url = media_outage(start, media, first, viewer)
        full_volume()

        c.stop(recorder)
        measurement = Measurement()
        fault("detached " + database)
        assert json.loads(op("recovery-status"))["archive"] == "volume_unavailable"
        assert create(point, first, viewer)[0] == 503
        assert playback.binary(url, viewer)[0] in (404, 410)
        json.loads(op("rebuild-archive"))
        read_block(first, viewer)
        metrics["volume_detached"] = {"lost_blocks": 0, **measurement.finish()}
        recorder = start_recorder()
        wait_new(set(catalog()))
        c.stop(recorder)

        # Snapshot, then make one independently observable post-snapshot configuration change.
        snapshot_time = time.monotonic()
        op("set-retention", "30", "20")
        backup = op("backup-config")
        assert b"Recovery camera" not in backup and config["credentials"]["password"].encode() not in backup
        op("create-user", "after-backup", "operator", input=(c.PASSWORD + "\n").encode())
        assert core("restore-config", input=backup).returncode != 0
        c.stop(server)
        before = {m["block_id"]: m for path in (ROOT / "blocks").glob("*/manifest.json")
                  if not path.parent.name.startswith(".")
                  for m in [json.loads(path.read_text())]}
        before = dict(sorted(before.items(), key=lambda item: item[1]["begin_utc_us"]))
        assert catalog().keys() <= before.keys()
        last_end = max(m["end_utc_us"] for m in before.values())
        bad = next(m for m in before.values() if m["block_id"] != first["block_id"])
        damaged = ROOT / "blocks" / bad["block_id"] / "media.mp4"
        with damaged.open("r+b") as file:
            file.seek(-1, os.SEEK_END)
            value = file.read(1)
            file.seek(-1, os.SEEK_END)
            file.write(bytes([value[0] ^ 1]))
        local("UPDATE committed_blocks SET catalog_error='catalog_rejected',registered_at_utc_us=NULL WHERE block_id=?",
              (first["block_id"],))
        loss = Measurement()
        subprocess.run(["dropdb", database], check=True)
        subprocess.run(["createdb", "--template=template0", database], check=True)
        op("migrate")
        assert core("restore-config", input=backup[:-12]).returncode != 0
        tampered = backup[:-3] + (b"a" if backup[-3:-2] != b"a" else b"b") + backup[-2:]
        assert core("restore-config", input=tampered).returncode != 0
        assert core("restore-config", env=RECORDER_ENV, input=backup).returncode != 0
        assert c.query("SELECT count(*) FROM bacuri_inventory.sites") == "0"
        op("restore-config", input=backup)
        assert c.query("SELECT days,reserve_percent FROM bacuri_inventory.archive_retention") == "30|20"
        op("restore-config", input=backup)
        server = start_core()
        assert c.http("GET", "session", headers=viewer)[0] == 401
        viewer = c.login("viewer")
        admin = c.login("admin")
        restored, = c.http("GET", CAMERAS, headers=admin)[2]
        assert restored["id"] == camera["id"] and restored["has_credentials"]
        assert [p["id"] for p in c.http("GET", POINTS, headers=viewer)[2]] == [point]
        assert foreign in {p["id"] for p in c.http("GET", POINTS, headers=admin)[2]}
        assert c.query("SELECT count(*) FROM bacuri_inventory.local_users WHERE username='after-backup'") == "0"
        assert create(foreign, first, viewer)[0] == 404
        assert create(point, first, viewer)[0] == 503
        metrics["configuration"] = {**loss.finish("rto_s"), "rpo_snapshot_age_s": round(loss.started - snapshot_time, 3),
                                    "lost_changes": 1}
        # Stall the actual volume scan and interrupt the recovery process after its durable fence.
        directory = os.open(ROOT / "blocks", os.O_DIRECTORY)
        try:
            fcntl.flock(directory, fcntl.LOCK_EX)
            interrupted = start(c.CORE, "rebuild-archive")
            c.wait_for(lambda: json.loads(op("recovery-status"))["archive"] == "verifying", "recovery fence")
            assert create(point, first, viewer)[0] == 503
            interrupted.kill()
            interrupted.wait(timeout=5)
        finally:
            os.close(directory)
        assert c.query("SELECT count(*) FROM bacuri_archive.blocks") == "0"
        assert create(point, first, viewer)[0] == 503
        repaired = json.loads(op("rebuild-archive"))
        assert repaired["corrupt"] == 1
        status = json.loads(subprocess.check_output([c.RECORDER, "archive-status"], env=RECORDER_ENV))
        assert status["pending"] == status["rejected"] == 0
        for kind, reason in (("camera", "source_offline"), ("volume_full", "storage_failure")):
            assert gap(reason)["gap_us"] >= metrics[kind]["gap_us"]
        # Rebuild from surviving directories even after losing SQLite acknowledgements and identity.
        (ROOT / "journal.db").unlink()
        for suffix in ("-wal", "-shm"):
            (ROOT / ("journal.db" + suffix)).unlink(missing_ok=True)
        second = json.loads(op("rebuild-archive"))
        assert second == repaired, (repaired, second)
        survivors = before.keys() - {bad["block_id"]}
        assert catalog().keys() == survivors
        assert len(survivors) == repaired["verified"]
        assert c.query("SELECT count(*)=count(DISTINCT block_id) FROM bacuri_archive.blocks") == "t"
        read_block(first, viewer)
        metrics["historical_archive"] = {**loss.finish("rto_s"), "verified_blocks": len(survivors),
                                         "lost_healthy_blocks": 0, "corrupt_blocks": 1, "damaged_block": bad["block_id"],
                                         "begin_utc_us": bad["begin_utc_us"], "end_utc_us": bad["end_utc_us"],
                                         "lost_video_us": bad["end_utc_us"] - bad["begin_utc_us"]}
        search = f"{POINTS}/{point}/archive?" + urlencode(bounds(bad))
        assert any(i.get("reason") == "storage_failure" for i in c.http("GET", search, headers=viewer)[2]["items"])
        assert create(point, bad, viewer)[0] == 409
        # A clean recorder must obtain the restored configuration through normal delivery.
        Path(os.environ["BACURI_RECORDER_DB"]).unlink()
        recorder = start_recorder()
        fresh = wait_new(survivors)
        read_block(fresh, viewer)
        metrics["new_recording"] = {**loss.finish("rto_s"), "rpo_gap_us": max(0, fresh["begin_utc_us"] - last_end)}
        for manifest in before.values():
            if manifest["block_id"] in survivors:
                read_block(manifest, viewer)
        assert create(foreign, fresh, viewer)[0] == 404
        c.stop(recorder)
        c.stop(media)

        # Destroy only the private 64 MiB test volume contents. PostgreSQL remains available.
        previously_readable = catalog()
        destruction = Measurement()
        shutil.rmtree(ROOT)
        assert core("rebuild-archive").returncode != 0
        assert create(point, first, viewer)[0] == 503
        ROOT.mkdir()
        replacement = start_recorder()
        wait_new(previously_readable)
        c.stop(replacement)
        result = json.loads(op("rebuild-archive"))
        assert result["catalog_missing"] == len(previously_readable)
        assert create(point, first, viewer)[0] == 409
        metrics["destroyed_volume"] = {"lost_blocks": len(previously_readable), "recoverable_old_blocks": 0,
                                       "lost_media_bytes": sum(m["media"]["size_bytes"] for m in previously_readable.values())}
        assert not (catalog().keys() & previously_readable.keys())
        media = start(playback.MEDIA)
        read_block(next(iter(catalog().values())), viewer)
        metrics["destroyed_volume"].update(destruction.finish("new_media_rto_s"))
        metrics["passed"] = True
        print(json.dumps(metrics), flush=True)


if __name__ == "__main__":
    try:
        run()
    finally:
        REPORT.write_text(json.dumps(metrics, indent=2) + "\n")
