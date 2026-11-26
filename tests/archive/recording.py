"""Verify durable recording using the production recorder, catalog and HTTPS registry."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import sqlite3
import struct
import subprocess
import sys
import time
import uuid

import jsonschema

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "control"))
import registry as control

ROOT = Path(os.environ["BACURI_ARCHIVE_DIR"])
JOURNAL = ROOT / "journal.db"
DATABASE = os.environ["PGDATABASE"]


def local(sql, args=()):
    with sqlite3.connect(JOURNAL) as connection:
        return connection.execute(sql, args).fetchall()


def allow_connections(enabled):
    # Only the private database created for this invocation is affected. An outage also ends the
    # sessions that services keep open between requests.
    assert DATABASE.startswith("bacuri_control_") and DATABASE.replace("_", "").isalnum()
    commands = [f'ALTER DATABASE "{DATABASE}" ALLOW_CONNECTIONS {str(enabled).lower()}']
    if not enabled:
        commands.append(f"SELECT count(pg_terminate_backend(pid)) FROM pg_stat_activity WHERE datname='{DATABASE}'")
    for command in commands:
        subprocess.run(["psql", "-Xq", "-d", "postgres", "-v", "ON_ERROR_STOP=1", "-c", command],
                       check=True, stdout=subprocess.DEVNULL)


def registered():
    return local("SELECT block_id,relative_directory FROM committed_blocks WHERE registered_at_utc_us IS NOT NULL "
                 "AND unavailable_reason IS NULL ORDER BY committed_at_utc_us")


def mp4_boxes(payload):
    offset = 0
    while offset < len(payload):
        size, kind = struct.unpack_from(">I4s", payload, offset)
        assert 8 <= size <= len(payload) - offset
        yield kind, payload[offset + 8:offset + size]
        offset += size


def check_blocks(rows, device):
    schema = json.loads(Path("storage/archive/block-manifest-v1.schema.json").read_text())
    for block_id, relative in rows:
        directory = ROOT / relative
        manifest = json.loads((directory / "manifest.json").read_text())
        jsonschema.validate(manifest, schema)
        assert manifest["block_id"] == block_id and manifest["device_id"] == device
        payload = (directory / "media.mp4").read_bytes()
        assert manifest["media"]["size_bytes"] == len(payload)
        assert manifest["media"]["sha256"] == hashlib.sha256(payload).hexdigest()
        assert manifest["end_utc_us"] > manifest["begin_utc_us"]
        boxes = list(mp4_boxes(payload))
        kinds = [kind for kind, _ in boxes]
        assert kinds[0] == b"ftyp" and kinds[-1] == b"mfra"
        assert kinds.index(b"moov") < kinds.index(b"moof") < kinds.index(b"mdat")
        moov = dict(mp4_boxes(dict(boxes)[b"moov"]))
        track = dict(mp4_boxes(moov[b"trak"]))
        mdhd = dict(mp4_boxes(track[b"mdia"]))[b"mdhd"]
        tkhd = track[b"tkhd"]
        description, = manifest["media"]["tracks"]
        for field, header in (("id", tkhd), ("time_scale", mdhd)):
            offset = 20 if header[0] == 1 else 12
            assert struct.unpack_from(">I", header, offset)[0] == description[field]
        pixels = subprocess.check_output(
            ["gst-launch-1.0", "-q", "filesrc", f"location={directory/'media.mp4'}", "!", "qtdemux",
             "!", "h264parse", "!", "avdec_h264", "!", "video/x-raw,width=1280,height=720",
             "!", "videoconvert", "!", "videoscale", "!", "video/x-raw,format=GRAY8,width=16,height=16",
             "!", "fdsink", "fd=1", "sync=false"], timeout=10)
        assert pixels and len(pixels) % 256 == 0


def point_state(headers, point):
    status, _, points = control.http("GET", f"sites/{control.SITE}/points", headers=headers)
    assert status == 200, (status, points)
    return next(p for p in points if p["id"] == point)


def wait_for_recording(headers, point, recording, seconds=20):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        state = point_state(headers, point)
        if state["recording"] == recording:
            return state
        time.sleep(0.5)
    raise AssertionError(f"Timed out waiting for recording state {recording}: {state}")


def run():
    subprocess.run([control.CORE, "create-user", "archive-admin", "administrator"],
                   input=control.PASSWORD+"\n", text=True, check=True)
    core = subprocess.Popen([control.CORE, "serve"])
    recorder = None
    offline = False
    env = dict(os.environ, BACURI_KEY_FILE=str(control.ROOT / "recorder.key"))
    try:
        control.wait_for(lambda: control.http("GET", "session")[0] == 401, "HTTPS registry")
        headers = {**control.login("archive-admin"), "Idempotency-Key": str(uuid.uuid4())}
        config = {"name": "Archive camera", "main_url": "rtsp://rtsp-camera:8554/main",
                  "secondary_url": "rtsp://rtsp-camera:8554/sub"}
        status, _, camera = control.http("POST", f"sites/{control.SITE}/cameras", config, headers)
        assert status == 201
        recorder = subprocess.Popen([control.RECORDER], env=env)
        control.wait_for(lambda: int(control.query("SELECT count(*) FROM bacuri_archive.blocks")) >= 3, "cataloged fMP4")
        blocks = registered()[:2]
        assert len(blocks) == 2
        check_blocks(blocks, camera["id"])
        # The point state is derived from catalogued blocks and the recorder report.
        live = wait_for_recording(headers, camera["point_id"], "current")
        assert live["connection"] == "online" and "recording_reason" not in live, live
        newest = control.query(f"SELECT max(end_utc_us) FROM bacuri_archive.blocks "
                               f"WHERE point_id='{camera['point_id']}'")
        assert live["last_recorded_at"].endswith("Z") and int(newest) > 0
        # The services export the measures behind the operator and archive thresholds.
        published = control.wait_for(
            lambda: control.metric("recorder", "bacuri_archive_publication_lag_seconds_count"), "archive metrics")
        lag = control.metric("recorder", "bacuri_archive_publication_lag_seconds_sum") / published
        assert 0 < lag < 5, lag
        assert control.wait_for(lambda: control.metric(
            "core", 'bacuri_http_request_duration_seconds_count{workload="interactive"}'), "request metrics")
        summary = json.loads(subprocess.check_output([control.RECORDER, "archive-status"], env=env))
        assert summary["committed"] >= 3 and summary["corrupt"] == summary["rejected"] == 0
        competing = subprocess.run([control.RECORDER], env=env, capture_output=True, timeout=5)
        assert competing.returncode != 0 and b"volume already owned" in competing.stderr

        allow_connections(False)
        offline = True
        start = local("SELECT count(*) FROM committed_blocks")[0][0]
        control.wait_for(lambda: local("SELECT count(*) FROM committed_blocks")[0][0] >= start + 3,
                         "new blocks during PostgreSQL outage")
        pending = {r[0] for r in local("SELECT block_id FROM committed_blocks WHERE registered_at_utc_us IS NULL")}
        assert len(pending) >= 3
        recorder.kill()
        recorder.wait(timeout=5)
        recorder = None

        # Recreate a lost catalog acknowledgement and a published directory without a journal row.
        local("UPDATE committed_blocks SET registered_at_utc_us=NULL WHERE block_id=?", (blocks[0][0],))
        orphan = next(iter(pending))
        local("DELETE FROM committed_blocks WHERE block_id=?", (orphan,))
        recorder = subprocess.Popen([control.RECORDER], env=env)
        control.wait_for(lambda: local("SELECT block_id FROM committed_blocks WHERE block_id=?", (orphan,)),
                         "reconciliation before the database returns")
        allow_connections(True)
        offline = False
        expected = pending | {b[0] for b in blocks}
        control.wait_for(lambda: expected <= {r[0] for r in registered()}, "backlog acknowledgements")
        ids = control.query("SELECT block_id FROM bacuri_archive.blocks").splitlines()
        assert expected <= set(ids) and len(ids) == len(set(ids))

        control.stop(recorder)
        assert recorder.returncode == 0
        recorder = None
        # Without the recorder only the archive testifies, and only until the newest block becomes stale.
        assert point_state(headers, camera["point_id"])["connection"] == "unknown"
        stale = wait_for_recording(headers, camera["point_id"], "unknown", seconds=40)
        assert stale["recording_reason"] == "recorder_unavailable", stale
        first, second = blocks
        conflict = next(r[0] for r in registered() if r[0] not in {first[0], second[0]})
        control.query(f"""UPDATE bacuri_archive.blocks SET manifest=jsonb_set(manifest,'{{end_utc_us}}',
                          to_jsonb(end_utc_us+1)) WHERE block_id='{conflict}'""")
        local("UPDATE committed_blocks SET registered_at_utc_us=NULL WHERE block_id=?", (conflict,))
        with (ROOT / first[1] / "media.mp4").open("r+b") as media:
            media.seek(-1, os.SEEK_END)
            byte = media.read(1)[0]
            media.seek(-1, os.SEEK_END)
            media.write(bytes([byte ^ 1]))
        shutil.move(ROOT / second[1], control.ROOT / "detached-block")
        recorder = subprocess.Popen([control.RECORDER], env=env)
        control.wait_for(lambda: control.query("SELECT count(*) FROM bacuri_archive.blocks WHERE unavailable_reason IS NOT NULL") == "2",
                         "catalog exclusion of corrupt and missing blocks")
        for (block_id, _), reason in ((first, "corrupt"), (second, "missing")):
            assert control.query(f"SELECT unavailable_reason FROM bacuri_archive.blocks WHERE block_id='{block_id}'") == reason
            assert control.query(f"""SELECT count(*) FROM bacuri_archive.timeline_intervals t
                JOIN bacuri_archive.blocks b USING(site_id,point_id,binding_revision,begin_utc_us,end_utc_us)
                WHERE b.block_id='{block_id}' AND t.interval_kind='recorded'""") == "0"
        control.wait_for(lambda: local("SELECT catalog_error FROM committed_blocks WHERE block_id=?", (conflict,))
                         == [("catalog_rejected",)], "immutable manifest conflict")
        control.stop(recorder)
        assert recorder.returncode == 0
        recorder = None
        shutil.move(control.ROOT / "detached-block", ROOT / second[1])
        recorder = subprocess.Popen([control.RECORDER], env=env)
        control.wait_for(lambda: control.query(f"SELECT unavailable_reason IS NULL FROM bacuri_archive.blocks "
                                               f"WHERE block_id='{second[0]}'") == "t", "verified repaired block")
        print(f"Decoded independent fMP4 blocks; {len(pending)} pending blocks recovered after database outage; "
              "single writer, replay, corruption and missing media verified")
    finally:
        if offline:
            allow_connections(True)
        control.stop(recorder)
        control.stop(core)


if __name__ == "__main__":
    run()
