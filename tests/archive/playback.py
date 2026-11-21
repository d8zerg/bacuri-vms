"""Check real HTTPS playback against deterministic frame colors and a known UTC gap."""

from contextlib import ExitStack
from datetime import datetime, timedelta, timezone
from http.client import HTTPSConnection
from pathlib import Path
from urllib.parse import urlencode
import hashlib
import json
import os
import socket
import sqlite3
import struct
import subprocess
import sys
import time
import uuid

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "control"))
import registry as control
from browser import browser_session

BASE = datetime.now(timezone.utc).replace(microsecond=0) - timedelta(minutes=10)
ORIGIN_US = int(BASE.timestamp()) * 1_000_000
ROOT = Path(os.environ["BACURI_ARCHIVE_DIR"])
MEDIA = control.BIN / "bacuri-vms-media"


def utc(seconds):
    return (BASE + timedelta(seconds=seconds)).isoformat(timespec="microseconds").replace("+00:00", "Z")


def interval(start, end):
    return {"start": utc(start), "end": utc(end)}


def fixture(camera, offset, width, colors, padding=0):
    block = str(uuid.uuid4())
    directory = ROOT / "blocks" / block
    directory.mkdir()
    path = directory / "media.mp4"
    frames = bytearray()
    for index in range(25):
        # Five black/white bands encode the frame ordinal independently of browser timestamps.
        stripes = b"".join(bytes([255 if index & (1 << bit) else 0]) * (3 * width // 5) for bit in range(5))
        frames.extend(stripes * 120)
        frames.extend(bytes(colors[int(index >= 12)]) * (width * 120))
    subprocess.run(
        [
            "gst-launch-1.0",
            "-q",
            "fdsrc",
            "fd=0",
            "!",
            "rawvideoparse",
            "format=rgb",
            f"width={width}",
            "height=240",
            "framerate=25/1",
            "!",
            "videoconvert",
            "!",
            "video/x-raw,format=I420",
            "!",
            "x264enc",
            "tune=zerolatency",
            "key-int-max=25",
            "!",
            "video/x-h264,profile=baseline",
            "!",
            "h264parse",
            "!",
            "mp4mux",
            "fragment-duration=500",
            "trak-timescale=90000",
            "!",
            "filesink",
            f"location={path}",
        ],
        input=frames,
        check=True,
        timeout=12,
    )
    payload = path.read_bytes()
    if padding:
        index = payload.rindex(b"mfra") - 4
        payload = (
            payload[:index] + struct.pack(">I4s", padding, b"free") + bytes(padding - 8) + payload[index:]
        )
        path.write_bytes(payload)
    manifest = json.loads(Path("tests/schema/block-manifest-v1.json").read_text())
    channel, profile = control.query(
        "SELECT channel_id,profile_id FROM bacuri_inventory.stream_profiles JOIN bacuri_inventory.channels USING(site_id,channel_id)"
        f" WHERE device_id='{camera['id']}' AND stream_role='main'"
    ).split("|")
    start = ORIGIN_US + offset * 1_000_000
    manifest.update(
        point_id=camera["point_id"],
        device_id=camera["id"],
        channel_id=channel,
        profile_id=profile,
        recording_node_id=os.environ["BACURI_NODE_ID"],
        block_id=block,
        begin_utc_us=start,
        end_utc_us=start + 1_000_000,
    )
    manifest["time"].update(received_at_utc_us=start, source_time_us=start)
    manifest["media"].update(size_bytes=len(payload), sha256=hashlib.sha256(payload).hexdigest())
    (directory / "manifest.json").write_text(json.dumps(manifest))
    control.query(f"INSERT INTO bacuri_archive.blocks(manifest) VALUES ('{json.dumps(manifest)}')")
    return manifest


def webdriver(browser, credential, selections):
    with browser_session(browser, credential, "/archive/") as (call, version):
        for selection in selections:
            metrics = call("POST","/execute/async",{
                "script":Path("tests/archive/browser.js").read_text(),"args":[selection]})
            assert "error" not in metrics, (browser,metrics)
            print(browser,version,json.dumps(metrics["value"]),flush=True)



def binary(path, headers):
    connection = HTTPSConnection("localhost", 8443, context=control.TLS, timeout=8)
    try:
        connection.request("GET", "/api/v1/" + path, headers=headers)
        response = connection.getresponse()
        return response.status, response.read()
    finally:
        connection.close()


def slow_reader(path, headers, revoke, post_body=None):
    with socket.socket() as sock:
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 8192)
        sock.settimeout(10)
        sock.connect(("localhost", 8443))
        with control.TLS.wrap_socket(sock, server_hostname="localhost") as tls:
            request = f"GET /api/v1/{path} HTTP/1.1\r\nHost: localhost\r\nCookie: {headers['Cookie']}\r\nConnection: close\r\n\r\n"
            if post_body is not None:
                payload = json.dumps(post_body)
                request = (f"POST /api/v1/{path} HTTP/1.1\r\nHost: localhost\r\nCookie: {headers['Cookie']}\r\n"
                           f"Origin: {control.ORIGIN}\r\nX-CSRF-Token: {headers['X-CSRF-Token']}\r\n"
                           f"Content-Type: application/json\r\nContent-Length: {len(payload)}\r\nConnection: close\r\n\r\n{payload}")
            tls.sendall(request.encode())
            received = b""
            while b"\r\n\r\n" not in received:
                chunk = tls.recv(1024)
                assert chunk, "Closed before response header"
                received += chunk
            head, body = received.split(b"\r\n\r\n", 1)
            assert b"200 OK" in head
            size = int(
                next(
                    line for line in head.lower().splitlines() if line.startswith(b"content-length:")
                ).split(b":")[1]
            )
            # Keep draining through a renewal before revoking the point grant.
            started = time.monotonic()
            revoked = None
            length = len(body)
            tls.settimeout(3)
            while chunk := tls.recv(16384):
                length += len(chunk)
                if revoked is None and time.monotonic() - started >= 2.2:
                    revoke()
                    revoked = time.monotonic()
                if revoked is not None:
                    assert time.monotonic() - revoked < 5, "Delivery continued after revocation"
                time.sleep(0.015)
            assert revoked is not None and 0 < length < size, (length, size)
            elapsed = time.monotonic() - revoked
            assert elapsed < 5
            print(f"Active HTTPS transfer stopped {elapsed:.3f} s after grant revocation", flush=True)
            return elapsed


def camera_recording(start_process, path, admin):
    # Hold catalog writes while permitting the search API to read the same point.
    with subprocess.Popen(
        ["psql", "-XAtq", "-v", "ON_ERROR_STOP=1"], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True
    ) as lock:
        lock.stdin.write("BEGIN; LOCK bacuri_archive.blocks IN SHARE MODE; SELECT 'locked';\n")
        lock.stdin.flush()
        assert lock.stdout.readline().strip() == "locked"
        try:
            recorder = start_process(
                [control.RECORDER],
                env={**os.environ, "BACURI_KEY_FILE": str(control.ROOT / "recorder.key")},
            )

            def current():
                if not (ROOT / "journal.db").exists():
                    return None
                with sqlite3.connect(ROOT / "journal.db") as db:
                    if not db.execute(
                        "SELECT name FROM sqlite_master WHERE name='committed_blocks'"
                    ).fetchone():
                        return None
                    return db.execute(
                        """SELECT block_id,manifest_json->>'begin_utc_us',manifest_json->>'end_utc_us'
                        FROM committed_blocks WHERE manifest_json->>'begin_utc_us'>? AND registered_at_utc_us IS NULL
                        ORDER BY manifest_json->>'begin_utc_us' LIMIT 1""",
                        (ORIGIN_US + 60_000_000,),
                    ).fetchone()

            block, begin, end = control.wait_for(
                current, "local camera recording while catalog writes are paused"
            )
            bounds = {
                name: (BASE + timedelta(microseconds=us - ORIGIN_US))
                .isoformat(timespec="microseconds")
                .replace("+00:00", "Z")
                for name, us in (("start", begin), ("end", end))
            }
            page = control.http("GET", path + "/archive?" + urlencode(bounds), headers=admin)[2]
            assert {entry["state"] for entry in page["items"]} == {"pending_catalog"}, page
            reply = control.http(
                "POST",
                path + "/media-sessions",
                {"mode": "archive", "interval": bounds},
                {**admin, "Idempotency-Key": str(uuid.uuid4())},
            )
            assert reply[0] == 409 and reply[2]["code"] == "pending_catalog", reply
        finally:
            lock.stdin.write("COMMIT;\n\\q\n")
            lock.stdin.flush()
    control.wait_for(
        lambda: control.query(f"SELECT count(*) FROM bacuri_archive.blocks WHERE block_id='{block}'")
        == "1",
        "camera catalog",
    )
    control.stop(recorder)
    return {**bounds, "recorded": True}


def run():
    for name, role in [("archive-admin", "administrator"), ("viewer", "operator")]:
        subprocess.run(
            [control.CORE, "create-user", name, role], input=control.PASSWORD + "\n", text=True, check=True
        )
    with ExitStack() as stack:

        def start(args, **kwargs):
            process = subprocess.Popen(args, **kwargs)
            stack.callback(control.stop, process)
            return process

        core = start([control.CORE, "serve"])
        control.wait_for(lambda: control.http("GET", "session")[0] == 401, "HTTPS")
        media = start([MEDIA])
        admin = control.login("archive-admin")
        viewer = control.login("viewer")
        headers = {**admin, "Idempotency-Key": str(uuid.uuid4())}
        status, _, camera = control.http(
            "POST",
            f"sites/{control.SITE}/cameras",
            {
                "name": "Reference camera",
                "main_url": "rtsp://rtsp-camera:8554/main",
                "secondary_url": "rtsp://rtsp-camera:8554/sub",
            },
            headers,
        )
        assert status == 201, camera
        point = camera["point_id"]
        path = f"sites/{control.SITE}/points/{point}"
        red, green, blue, yellow = (255, 0, 0), (0, 255, 0), (0, 0, 255), (255, 255, 0)
        first = fixture(camera, 0, 320, [red, green])
        fixture(camera, 1, 640, [blue, yellow])
        fixture(camera, 3, 320, [red, green])
        fixture(camera, 5, 320, [red, green], 16 * 1024 * 1024)
        control.query(
            f"INSERT INTO bacuri_archive.gaps VALUES('{control.SITE}','{point}',1,{ORIGIN_US + 2000000},{ORIGIN_US + 3000000},'source_offline')"
        )
        search = path + "/archive?" + urlencode(interval(-1, 4))
        status, _, timeline = control.http("GET", search, headers=admin)
        assert status == 200, timeline
        assert [i["state"] for i in timeline["items"]] == [
            "unavailable",
            "recorded",
            "recorded",
            "gap",
            "recorded",
        ], timeline
        assert timeline["items"][3]["reason"] == "source_offline"
        assert len(timeline["items"]) == 5 and timeline["items"][3]["interval"] == {
            k: v.replace(".000000", "") for k, v in interval(2, 3).items()
        }
        assert control.http("GET", search, headers=viewer)[0] == 404
        assert control.http("GET", search)[0] == 401
        assert (
            control.http("GET", path + "/archive?" + urlencode(interval(0, 3601)), headers=admin)[0] == 400
        )

        def create(bounds, h=headers):
            return control.http(
                "POST", path + "/media-sessions", {"mode": "archive", "interval": bounds}, h
            )

        assert create(interval(0, 1), {**headers, "X-CSRF-Token": ""})[0] == 403
        assert create(interval(0.1, 0.9))[2]["code"] == "media_unavailable"
        assert create(interval(2, 3))[2]["code"] == "recording_gap"
        status, _, session = create(interval(0, 1))
        assert status == 201, session
        assert create(interval(0, 1))[2] == session and create(interval(0, 2))[0] == 409
        base = "media-sessions/" + session["id"]
        activity_query = ("SELECT last_active FROM bacuri_inventory.browser_sessions b "
                          "JOIN bacuri_inventory.media_sessions m ON b.token_hash=m.browser "
                          f"WHERE m.id='{session['id']}'")
        last_active = control.query(activity_query)
        manifest = control.http("GET", base + "/manifest", headers=admin)[2]
        (one,) = manifest["parts"]
        assert binary(base + "/parts/" + one["init_id"], admin)[1][4:8] == b"ftyp"
        assert binary(base + "/parts/" + one["media_id"], admin)[1][4:8] == b"moof"
        assert control.query(activity_query) == last_active
        other_browser = control.login("archive-admin")
        assert control.http("GET", base + "/manifest", headers=other_browser)[0] == 404
        assert control.http("GET", base + "/manifest", headers=viewer)[0] == 404
        assert control.http("DELETE", base, headers=viewer)[0] == 204
        second = create(interval(1, 2), {**headers, "Idempotency-Key": str(uuid.uuid4())})[2]
        assert binary("media-sessions/" + second["id"] + "/parts/" + one["media_id"], admin)[0] == 404
        control.query(f"UPDATE bacuri_inventory.media_sessions SET expires_at=now() WHERE id='{second['id']}'")
        assert control.http("GET", "media-sessions/" + second["id"] + "/manifest", headers=admin)[0] == 410
        control.stop(core)
        control.stop(media)
        core = start([control.CORE, "serve"])
        media = start([MEDIA])
        control.wait_for(
            lambda: control.http("GET", base + "/manifest", headers=admin)[0] == 200,
            "archive after restart",
        )
        assert binary(base + "/parts/" + one["media_id"], admin)[0] == 200
        recorded = camera_recording(start, path, admin)
        for browser in ["chrome", "firefox"]:
            webdriver(browser, admin, [{**interval(0, 4), "target": utc(0.64), "gap": utc(2.5)}, recorded])
        grant_headers = {**viewer, "Idempotency-Key": str(uuid.uuid4())}
        subprocess.run([control.CORE, "grant-point", "viewer", point], check=True)
        throttled_session = create(interval(5, 6), grant_headers)[2]
        stream = "media-sessions/" + throttled_session["id"]
        part = control.http("GET", stream + "/manifest", headers=viewer)[2]["parts"][0]["media_id"]
        slow_reader(
            stream + "/parts/" + part,
            viewer,
            lambda: control.query(f"DELETE FROM bacuri_inventory.point_grants WHERE point_id='{point}'"),
        )
        assert binary(stream + "/parts/" + part, viewer)[0] == 404
        assert control.http("DELETE", base, headers=admin)[0] == 204
        assert control.http("GET", base + "/manifest", headers=admin)[0] == 410
        assert create(interval(0, 1))[0] == 409
        # Pagination with authenticated continuation under the original query and browser.
        control.query(f"""INSERT INTO bacuri_archive.gaps SELECT '{control.SITE}','{point}',1,
            {ORIGIN_US}+10000000+2*i,{ORIGIN_US}+10000001+2*i,'overload' FROM generate_series(1,510) i""")
        query = path + "/archive?" + urlencode(interval(10, 11))
        page = control.http("GET", query, headers=admin)[2]
        assert len(page["items"]) == 1000 and page["next_cursor"]
        continuation = "&cursor=" + page["next_cursor"]
        last = control.http("GET", query + continuation, headers=admin)[2]
        assert (
            "next_cursor" not in last
            and page["items"][-1]["interval"]["end"] == last["items"][0]["interval"]["start"]
        )
        assert control.http("GET", search + continuation, headers=admin)[0] == 400
        assert control.http("GET", query + continuation, headers=other_browser)[0] == 400
        assert control.http("DELETE", "session", headers=other_browser)[0] == 204
        assert control.http("GET", search, headers=other_browser)[0] == 401
        file = ROOT / "blocks" / first["block_id"] / "media.mp4"
        file.write_bytes(file.read_bytes()[:-1])
        fresh = create(interval(0, 1), {**headers, "Idempotency-Key": str(uuid.uuid4())})[2]
        resource = "media-sessions/" + fresh["id"]
        part = control.http("GET", resource + "/manifest", headers=admin)[2]["parts"][0]["media_id"]
        assert binary(resource + "/parts/" + part, admin)[0] == 503
        print(
            "Archive scope, UTC coverage, pagination, restart, revocation and corruption verified",
            flush=True,
        )


if __name__ == "__main__":
    run()
