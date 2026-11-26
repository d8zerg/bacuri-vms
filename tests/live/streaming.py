"""Exercise the real recorder, WebRTC transport and HTTPS client on a private database."""

from contextlib import ExitStack
import signal
import os
from pathlib import Path
import struct
import subprocess
import sys
import time
import uuid

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "control"))
import registry as control
from browser import browser_session
from relay import VideoLink


def archive_blocks(point):
    rows = control.query(f"""SELECT begin_utc_us,end_utc_us,unavailable_reason IS NULL,block_id
        FROM bacuri_archive.blocks WHERE point_id='{point}' AND time_basis='source_clock'
        ORDER BY begin_utc_us""")
    return [
        (int(a), int(b), ok == "t", block)
        for a, b, ok, block in (row.split("|") for row in rows.splitlines())
    ]


def samples(data):
    total, offset = 0, 0
    while offset < len(data):
        size, kind = struct.unpack_from(">I4s", data, offset)
        assert 8 <= size <= len(data) - offset, (kind, size)
        content = data[offset + 8 : offset + size]
        if kind in (b"moof", b"traf"):
            total += samples(content)
        elif kind == b"trun":
            total += struct.unpack_from(">I", content, 4)[0]
        offset += size
    return total


def coverage(point, blocks):
    begin, end = blocks[0][0], blocks[-1][1]
    root = Path(os.environ["BACURI_ARCHIVE_DIR"]) / "blocks"
    for _, _, ok, block in blocks:
        assert ok, block
        # Both reference encoders produce one 25-frame GOP per full main-profile block.
        count = samples(memoryview((root / block / "media.mp4").read_bytes()))
        assert count == 25, (block, count)
    # Allow subframe clock correction but detect one missing source frame at 25 fps.
    holes = [b[0] - a[1] for a, b in zip(blocks, blocks[1:]) if b[0] - a[1] > 20000]
    gaps = control.query(f"""SELECT count(*),coalesce(sum(least(end_utc_us,{end})-greatest(begin_utc_us,{begin})),0)
        FROM bacuri_archive.gaps WHERE point_id='{point}' AND end_utc_us>{begin} AND begin_utc_us<{end}""")
    metrics = (len(holes), sum(holes), *map(int, gaps.split("|")))
    print(
        f"archive blocks={len(blocks)} duration_us={end - begin} holes={metrics[:2]} catalog_gaps={metrics[2:]}",
        flush=True,
    )
    return metrics


def frame(call):
    result = call("POST", "/execute/async", {"script": Path("tests/live/frame.js").read_text(), "args": []})
    assert "error" not in result, result
    return result


def linked(call, link):
    call(
        "POST",
        "/execute/sync",
        {"script": Path("tests/live/link.js").read_text(), "args": [link.host, link.port]},
    )
    print("relay first frame", frame(call), flush=True)
    assert time.monotonic() - link.last_packet < 1
    return uuid.UUID(call("POST", "/execute/sync", {"script": "return window.liveSession", "args": []}))


def verify_cutoff(link, change, label):
    initial = link.last_packet
    control.wait_for(lambda: link.last_packet > initial, "active video before " + label)
    begin = time.monotonic()
    change()
    time.sleep(5.3)
    delay = max(0, link.last_packet - begin)
    assert delay <= 5, (label, delay)
    print(f"{label} last SRTP packet after {delay:.3f} s", flush=True)


def run():
    with ExitStack() as services:

        def stop(proc):
            if proc.returncode == -signal.SIGKILL:
                return
            control.stop(proc)
            assert proc.returncode == 0, (proc.args, proc.returncode)

        def start(command, **options):
            proc = subprocess.Popen(command, **options)
            services.callback(stop, proc)
            return proc

        for name, role in [("operator", "operator"), ("admin", "administrator")]:
            subprocess.run(
                [control.CORE, "create-user", name, role],
                input=control.PASSWORD + "\n",
                text=True,
                check=True,
            )
        core = start([control.CORE, "serve"])
        control.wait_for(lambda: control.http("GET", "session")[0] == 401, "HTTPS")
        admin, viewer = control.login("admin"), control.login("operator")
        start([control.RECORDER], env={**os.environ, "BACURI_KEY_FILE": str(control.ROOT / "recorder.key")})
        media = start([control.BIN / "bacuri-vms-media"])
        camera_url = "rtsp://" + os.environ["BACURI_TEST_CAMERA_HOST"] + ":8554"
        status, _, camera = control.http(
            "POST",
            f"sites/{control.SITE}/cameras",
            {
                "name": "Live reference",
                "main_url": camera_url + "/main",
                "secondary_url": camera_url + "/sub",
            },
            {**admin, "Idempotency-Key": str(uuid.uuid4())},
        )
        assert status == 201, camera
        point = camera["point_id"]
        route = f"sites/{control.SITE}/points/{point}/media-sessions"
        headers = {**viewer, "Idempotency-Key": str(uuid.uuid4())}
        assert control.http("POST", route, {"mode": "live"}, headers)[0] == 404
        subprocess.run([control.CORE, "grant-point", "operator", point], check=True)
        status, _, session = control.http("POST", route, {"mode": "live"}, headers)
        assert status == 201, session
        assert control.http("POST", route, {"mode": "live"}, headers)[2] == session
        resource = "media-sessions/" + session["id"]
        assert control.http("GET", resource + "/manifest", headers=viewer)[0] == 404
        assert control.http("POST", resource + "/offer", headers=admin)[0] == 404
        assert control.http("POST", resource + "/offer", headers={**viewer, "X-CSRF-Token": ""})[0] == 403
        assert control.http("POST", resource + "/offer", headers=viewer)[0] == 400
        assert control.http("DELETE", resource, headers=viewer)[0] == 204
        assert control.http("POST", resource + "/offer", headers=viewer)[0] == 410
        quota = []
        for _ in range(16):
            status, _, item = control.http(
                "POST", route, {"mode": "live"}, {**viewer, "Idempotency-Key": str(uuid.uuid4())}
            )
            assert status == 201, item
            quota.append(item["id"])
        status, limit_headers, _ = control.http(
            "POST", route, {"mode": "live"}, {**viewer, "Idempotency-Key": str(uuid.uuid4())}
        )
        assert status == 429 and int(limit_headers["Retry-After"]) >= 1
        for session_id in quota:
            assert control.http("DELETE", "media-sessions/" + session_id, headers=viewer)[0] == 204
        control.wait_for(lambda: len(archive_blocks(point)) >= 5, "reference recording")
        baseline = archive_blocks(point)
        reference = coverage(point, baseline)
        assert reference == (0, 0, 0, 0), reference
        for browser in ["chrome", "firefox"]:
            with browser_session(browser, viewer, "/live/") as (call, version):
                print(browser, version, frame(call), flush=True)
        # The media service measures the same wait on the server and exports it for the site.
        served = control.wait_for(lambda: control.metric("media", "bacuri_live_first_frame_seconds_count"),
                                  "first frame metrics")
        first = control.metric("media", "bacuri_live_first_frame_seconds_sum") / served
        print(f"server first frame mean {first:.3f} s over {served:.0f} sessions", flush=True)
        assert 0 < first < 2, first
        with browser_session("chrome", viewer, "/live/") as (call, _):
            pending_id = uuid.UUID(
                call(
                    "POST",
                    "/execute/async",
                    {
                        "script": Path("tests/live/no-answer.js").read_text(),
                        "args": [],
                    },
                )
            )
            begin = time.monotonic()
            control.wait_for(
                lambda: control.query(
                    f"SELECT closed FROM bacuri_media_sessions.media_sessions WHERE id='{pending_id}'"
                )
                == "t",
                "unconnected peer release",
            )
            elapsed = time.monotonic() - begin
            # This includes transport teardown, the close RPC and polling the committed row.
            assert elapsed < 5.6, elapsed
            print(f"unconnected live session released after {elapsed:.3f} s", flush=True)
        with browser_session("chrome", viewer, "/live/") as (call, _), VideoLink("chrome") as link:
            linked(call, link)
            link.limited.set()
            count = len(archive_blocks(point))
            control.wait_for(
                lambda: link.dropped >= 40 and len(archive_blocks(point)) >= count + 3,
                "shaped preview and continuous recording",
            )
            print("8 KiB/s video link dropped", link.dropped, "SRTP datagrams", flush=True)
            media.kill()
            media.wait(timeout=5)
            start([control.BIN / "bacuri-vms-media"])
            control.wait_for(
                lambda: len(archive_blocks(point)) >= count + 5, "recording across delivery restart"
            )
            link.limited.clear()
            print("reconnect in the same browser", frame(call), flush=True)
        with browser_session("chrome", viewer, "/live/") as (call, _), VideoLink("chrome") as link:
            revoked = linked(call, link)
            verify_cutoff(
                link,
                lambda: control.query(
                    f"DELETE FROM bacuri_access.point_grants WHERE point_id='{point}'"
                ),
                "revocation",
            )
            assert (
                control.query(f"SELECT closed FROM bacuri_media_sessions.media_sessions WHERE id='{revoked}'")
                == "t"
            )
        subprocess.run([control.CORE, "grant-point", "operator", point], check=True)
        with browser_session("chrome", viewer, "/live/") as (call, _), VideoLink("chrome") as link:
            live_id = linked(call, link)
            verify_cutoff(
                link,
                lambda: control.query(
                    f"UPDATE bacuri_media_sessions.media_sessions SET expires_at=clock_timestamp() WHERE id='{live_id}'"
                ),
                "expiry",
            )
            assert (
                control.query(f"SELECT closed FROM bacuri_media_sessions.media_sessions WHERE id='{live_id}'")
                == "t"
            )
        with browser_session("chrome", viewer, "/live/") as (call, _), VideoLink("chrome") as link:
            linked(call, link)
            try:
                verify_cutoff(link, lambda: core.send_signal(signal.SIGSTOP), "authorization outage")
            finally:
                core.send_signal(signal.SIGCONT)
        mixed = archive_blocks(point)[len(baseline) - 1 :]
        assert len(mixed) > 8 and coverage(point, mixed) == reference, mixed


if __name__ == "__main__":
    run()
