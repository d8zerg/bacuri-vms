"""Exercise retention boundaries, existing media links and recording pressure on a private tmpfs."""

from contextlib import ExitStack
import hashlib
import json
import os
from pathlib import Path
import sqlite3
import subprocess
import uuid
from urllib.parse import urlencode

from playback import ROOT, MEDIA, binary, control as c, fixture, interval


def main():
    total = os.statvfs(ROOT)
    assert total.f_blocks * total.f_frsize == 64 * 1024**2
    subprocess.run(
        [c.CORE, "create-user", "admin", "administrator"], input=c.PASSWORD + "\n", text=True, check=True
    )

    def configure(days, reserve):
        subprocess.run([c.CORE, "set-retention", str(days), str(reserve)], check=True)

    def status():
        return json.loads(subprocess.check_output([c.RECORDER, "archive-status"]))

    with ExitStack() as processes:

        def start(args, **kw):
            process = subprocess.Popen(args, **kw)
            processes.callback(c.stop, process)
            return process

        start([c.CORE, "serve"])
        start([MEDIA])
        c.wait_for(lambda: c.http("GET", "session")[0] == 401, "HTTPS ready")
        user = c.login("admin")

        def post(path, body):
            code, _, result = c.http("POST", path, body, {**user, "Idempotency-Key": str(uuid.uuid4())})
            assert code == 201, result
            return result

        camera = post(
            f"sites/{c.SITE}/cameras",
            {
                "name": "Retention camera",
                "main_url": "rtsp://rtsp-camera:8554/main",
                "secondary_url": "rtsp://rtsp-camera:8554/sub",
            },
        )
        point = f"sites/{c.SITE}/points/{camera['point_id']}"
        old, live = [fixture(camera, offset, 320, [(255, 0, 0), (0, 255, 0)]) for offset in (0, 1)]
        cutoff = old["end_utc_us"]
        day_us = 86400000000
        # Substitute the catalog clock only inside this throwaway test database.
        c.query(
            f"""CREATE TABLE retention_test_clock(us bigint);
                    INSERT INTO retention_test_clock VALUES({cutoff + day_us - 5000000});
                    CREATE OR REPLACE FUNCTION bacuri_archive.retention_now_us() RETURNS bigint LANGUAGE sql STABLE
                    AS 'SELECT us FROM public.retention_test_clock'"""
        )
        configure(1, 20)
        env = {**os.environ, "BACURI_KEY_FILE": str(c.ROOT / "recorder.key")}
        recorder = start([c.RECORDER], env=env)
        c.wait_for(
            lambda: Path(os.environ["BACURI_SOCKET"]).is_socket() and status()["retention_days"] == 1,
            "local policy received",
        )

        def part(offset):
            session = post(
                point + "/media-sessions", {"mode": "archive", "interval": interval(offset, offset + 1)}
            )
            base = "media-sessions/" + session["id"]
            (item,) = c.http("GET", base + "/manifest", headers=user)[2]["parts"]
            return base + "/parts/" + item["media_id"]

        old_url, live_url = part(0), part(1)
        assert binary(old_url, user)[1][4:8] == b"moof"
        reference = binary(live_url, user)[1]
        assert reference[4:8] == b"moof"
        c.stop(recorder)
        c.query("UPDATE retention_test_clock SET us=us+4999999")
        assert (
            c.query(f"SELECT count(*) FROM bacuri_archive.retained_blocks WHERE block_id='{old['block_id']}'")
            == "1"
        )
        c.query("UPDATE retention_test_clock SET us=us+1")
        assert binary(old_url, user)[0] == 404 and binary(live_url, user)[1] == reference
        assert (ROOT / "blocks" / old["block_id"] / "media.mp4").exists()
        recorder = start([c.RECORDER], env=env)
        c.wait_for(lambda: status()["removed_blocks"] >= 1, "reclaimed expired block after restart")
        assert not (ROOT / "blocks" / old["block_id"]).exists()
        assert (
            c.query(f"SELECT count(*) FROM bacuri_archive.blocks WHERE block_id='{old['block_id']}'") == "0"
        )
        with sqlite3.connect(ROOT / "journal.db") as db:
            assert not db.execute(
                "SELECT 1 FROM committed_blocks WHERE block_id=?", (old["block_id"],)
            ).fetchall()
        timeline = c.http("GET", point + "/archive?" + urlencode(interval(0, 2)), headers=user)[2]
        assert [entry["state"] for entry in timeline["items"]] == ["unavailable", "recorded"], timeline
        volume_path = f"sites/{c.SITE}/storage/volumes"
        revision = c.http("GET", volume_path, headers=user)[2]["revision"]
        code, _, configured = c.http("PUT", volume_path, {"days": 30, "reserve_percent": 20, "revision": revision}, user)
        assert code == 200 and configured["days"] == 30 and configured["reserve_percent"] == 20
        c.wait_for(lambda: status()["retention_days"] == 30, "longer retention applied")
        assert binary(old_url, user)[0] == 404
        c.wait_for(
            lambda: status()["estimated_depth_seconds"] and status()["remaining_seconds"] > 0,
            "measured forecast",
        )

        # Add a disposable external file to this private mount only.
        filler = ROOT.parent / "retention-fill"
        measurements = {}
        with filler.open("xb") as f:

            def leave(fraction):
                stats = os.statvfs(ROOT)
                amount = stats.f_bavail * stats.f_frsize - int(stats.f_blocks * stats.f_frsize * fraction)
                if amount > 0:
                    os.posix_fallocate(f.fileno(), os.fstat(f.fileno()).st_size, amount)

            leave(0.24)
            measurements["low_space"] = status()
            assert measurements["low_space"]["state"] == "low_space"
            leave(0.19)
            c.wait_for(
                lambda: c.query("SELECT count(*) FROM bacuri_archive.gaps WHERE reason='storage_failure'")
                != "0",
                "confirmed recording interruption",
            )
            measurements["recording_stopped"] = status()
            assert measurements["recording_stopped"]["state"] == "recording_stopped"
            code, _, capacity = c.http("GET", volume_path, headers=user)
            assert code == 200 and capacity["volume"]["state"] == "recording_stopped" and capacity["points"]
            assert binary(live_url, user)[1] == reference
            protected = ROOT / "blocks" / live["block_id"] / "media.mp4"
            assert hashlib.sha256(protected.read_bytes()).hexdigest() == live["media"]["sha256"]
        latest = status()["last_commit_utc_us"]
        filler.unlink()
        c.wait_for(
            lambda: status()["state"] == "normal" and status()["last_commit_utc_us"] > latest,
            "recording resumed",
        )
        assert c.http("GET", volume_path, headers=user)[2]["volume"]["state"] == "normal"
        measurements["resumed"] = status()
        resumed = measurements["resumed"]
        assert 0 < resumed["estimated_depth_seconds"] <= 30 * 86400
        assert resumed["capacity_depth_seconds"] >= resumed["estimated_depth_seconds"]
        c.stop(recorder)
        # Rebuild over restored catalog metadata. A stale row must not make a retired block readable again.
        c.query(f"INSERT INTO bacuri_archive.blocks(manifest) VALUES ('{json.dumps(old)}')")
        rebuilt = json.loads(subprocess.check_output([c.CORE, "rebuild-archive"]))
        assert rebuilt["verified"] >= 1 and rebuilt["catalog_missing"] == 0, rebuilt
        assert (
            c.query(f"SELECT count(*) FROM bacuri_archive.blocks WHERE block_id='{old['block_id']}'") == "0"
        )
        assert protected.is_file()
        report = Path(os.environ["BACURI_RETENTION_REPORT"])
        report.write_text(
            json.dumps(
                {
                    "volume_bytes": total.f_blocks * total.f_frsize,
                    "cutoff_us": cutoff,
                    "expired_block": old["block_id"],
                    "protected_block": live["block_id"],
                    "protected_media_sha256": live["media"]["sha256"],
                    "states": measurements,
                    "rebuild": rebuilt,
                },
                indent=2,
            )
            + "\n"
        )
        print(report, flush=True)
        print(
            "Retention boundary, catalog replay, protected media, low space, stop and resume verified",
            flush=True,
        )


if __name__ == "__main__":
    main()
