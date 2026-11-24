"""Replay a numbered H.264 recording over interleaved RTP with ONVIF clock headers."""

from contextlib import ExitStack
from datetime import datetime, timezone
from pathlib import Path
from socketserver import StreamRequestHandler, ThreadingTCPServer
from threading import Thread
import json
import os
import re
import ssl
import struct
import subprocess
import sys
import time
import uuid

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "control"))
import registry as c
from emulator import serve, utc


def iso(us):
    return (
        datetime.fromtimestamp(us / 1e6, timezone.utc)
        .isoformat(timespec="microseconds")
        .replace("+00:00", "Z")
    )


def make_frames():
    width, height = 128, 64
    frames = []
    for i in range(360):
        row = b"".join(bytes([235 if i & (1 << bit) else 16]) * 16 for bit in range(8))
        frames.append(row * height)
    args = "gst-launch-1.0 -q fdsrc fd=0 ! rawvideoparse format=gray8 width=128 height=64 framerate=2/1 ! videoconvert ! video/x-raw,format=I420 ! x264enc tune=zerolatency speed-preset=ultrafast key-int-max=2 byte-stream=true aud=true ! h264parse config-interval=-1 ! video/x-h264,stream-format=byte-stream,alignment=au ! fdsink fd=1".split()
    data = subprocess.run(args, input=b"".join(frames), capture_output=True, check=True).stdout
    units = []
    for nal in re.split(b"\x00\x00(?:\x00)?\x01", data):
        if not nal:
            continue
        if nal[0] & 31 == 9:
            units.append([])
        units[-1].append(nal)
    assert len(units) == 360
    return units


class Replay(StreamRequestHandler):
    def handle(self):
        session = "12345678"
        address = f"rtsp://localhost:{self.server.server_address[1]}/recording"
        while True:
            first = self.rfile.read(1)
            if not first:
                return
            if first == b"$":
                header = self.rfile.read(3)
                if len(header) != 3:
                    return
                self.rfile.read(int.from_bytes(header[1:], "big"))
                continue
            request = first + self.rfile.readline(4096)
            if not request.endswith(b"\n"):
                return
            if not request.strip():
                continue
            method = request.split()[0].decode()
            headers = {}
            while True:
                line = self.rfile.readline(4096)
                if not line.endswith(b"\n"):
                    return
                if not line.strip():
                    break
                k, v = line.decode().split(":", 1)
                headers[k.lower()] = v.strip()
            if "cseq" not in headers:
                self.wfile.write(b"RTSP/1.0 400 Bad Request\r\nContent-Length: 0\r\n\r\n")
                return
            fields = {"CSeq": headers["cseq"]}
            body = b""
            if method == "OPTIONS":
                fields["Public"] = "OPTIONS, DESCRIBE, SETUP, PLAY, TEARDOWN, GET_PARAMETER"
            if method == "DESCRIBE":
                body = (
                    "v=0\r\no=- 1 1 IN IP4 127.0.0.1\r\ns=Edge\r\nt=0 0\r\na=control:*\r\n"
                    "m=video 0 RTP/AVP 96\r\na=rtpmap:96 H264/90000\r\na=fmtp:96 packetization-mode=1\r\na=control:track1\r\n"
                ).encode()
                fields.update({"Content-Type": "application/sdp", "Content-Base": address + "/"})
            if method == "SETUP":
                fields.update(
                    {"Session": session, "Transport": "RTP/AVP/TCP;unicast;interleaved=0-1;ssrc=01020304"}
                )
            if method == "PLAY":
                value = headers["range"]
                assert value.startswith("clock="), value
                bounds = [
                    int(
                        datetime.strptime(t, "%Y%m%dT%H%M%S.%fZ").replace(tzinfo=timezone.utc).timestamp()
                        * 1e6
                    )
                    for t in value[6:].split("-")
                ]
                a, b = bounds
                fields.update(
                    {
                        "Session": session,
                        "Range": value,
                        "RTP-Info": "url=" + address + "/track1;seq=1;rtptime=90000",
                    }
                )
            fields["Content-Length"] = str(len(body))
            self.wfile.write(
                (
                    "RTSP/1.0 200 OK\r\n" + "".join(f"{k}: {v}\r\n" for k, v in fields.items()) + "\r\n"
                ).encode()
                + body
            )
            if method == "PLAY":
                seq = 1
                for index, unit in enumerate(self.server.units):
                    utc = self.server.base + index * 500000
                    if not a <= utc < b or index in self.server.erased:
                        continue
                    sec, micro = divmod(utc, 1000000)
                    ext = struct.pack(
                        "!HHIIBBH",
                        0xABAC,
                        3,
                        (sec + 2208988800) & 0xFFFFFFFF,
                        micro * (1 << 32) // 1000000,
                        0,
                        0,
                        int(headers["cseq"]),
                    )
                    for j, nal in enumerate(unit):
                        packet = (
                            struct.pack(
                                "!BBHII",
                                0x90,
                                96 | (128 if j + 1 == len(unit) else 0),
                                seq,
                                90000 + (utc - a) * 9 // 100,
                                0x01020304,
                            )
                            + ext
                            + nal
                        )
                        self.wfile.write(b"$\0" + struct.pack("!H", len(packet)) + packet)
                        seq += 1
                    time.sleep(0.008)
                    if self.server.interrupt_at == index:
                        self.server.interrupt_at = None
                        return
                # RTCP sender BYE ends the recording without closing the RTSP control channel.
                self.wfile.write(struct.pack("!BBHBBHI", 36, 1, 8, 0x81, 203, 1, 0x01020304))
                continue
            if method == "TEARDOWN":
                return


def run():
    cert, key = map(Path, (os.environ["BACURI_TLS_CERT"], os.environ["BACURI_TLS_KEY"]))
    base = (int(time.time()) - 240) * 1000000
    with ExitStack() as stack:
        replay = stack.enter_context(ThreadingTCPServer(("127.0.0.1", 0), Replay))
        replay.daemon_threads = True
        tls = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        tls.load_cert_chain(cert, key)
        replay.socket = tls.wrap_socket(replay.socket, server_side=True)
        replay.units, replay.base, replay.erased = make_frames(), base, set(range(140, 150))
        replay.interrupt_at = 65
        Thread(target=replay.serve_forever, daemon=True).start()
        stack.callback(replay.shutdown)
        device, _ = stack.enter_context(serve(cert, key, multicast=True))
        device.edge = (
            iso(base),
            iso(base + 180000000),
            f"rtsps://localhost:{replay.server_address[1]}/recording",
        )
        for name, role in [("installer", "administrator"), ("viewer", "operator")]:
            subprocess.run(
                [c.CORE, "create-user", name, role], input=c.PASSWORD + "\n", text=True, check=True
            )
        for cmd, env in [
            ([c.CORE, "serve"], os.environ),
            ([c.RECORDER], {**os.environ, "BACURI_KEY_FILE": str(c.ROOT / "recorder.key")}),
        ]:
            stack.callback(c.stop, subprocess.Popen(cmd, env=env))
        c.wait_for(lambda: c.http("GET", "session")[0] == 401, "HTTPS")
        admin = c.login("installer")
        site = "sites/" + c.SITE

        def call(method, path, body=None, code=200):
            status, _, reply = c.http(
                method, site + path, body, {**admin, "Idempotency-Key": str(uuid.uuid4())}, timeout=45
            )
            assert status == code, (status, reply)
            return reply

        candidates = call("POST", "/onvif/discovery")
        info = call(
            "POST",
            "/onvif/profiles",
            {"selection": candidates[0]["selection"], "username": "fixture", "password": device.password},
        )
        cam = call(
            "POST",
            "/cameras",
            {
                "name": "Counter",
                "main_url": info["profiles"][0]["url"],
                "secondary_url": info["profiles"][1]["url"],
                "credentials": {"username": "fixture", "password": device.password},
                "onvif_selection": info["selection"],
            },
            201,
        )
        point = cam["point_id"]
        c.query(
            f"UPDATE bacuri_inventory.point_bindings SET valid_from_utc_us={base} WHERE point_id='{point}'"
        )
        c.query(
            f"INSERT INTO bacuri_archive.gaps VALUES('{c.SITE}','{point}',1,{base+30000000},{base+90000000},'source_offline')"
        )
        path = "/cameras/" + cam["id"] + "/edge-archive"
        inventory = call("GET", path)
        assert inventory["supported"] and inventory["gaps"] and len(inventory["recordings"]) == 1, inventory
        assert c.http("GET", site + path, headers=c.login("viewer"))[0] == 403
        selection = {"binding_revision": "1", "recording": inventory["recordings"][0]["id"]}

        def recover(a, b):
            return call(
                "POST",
                path,
                {**selection, "interval": {"start": iso(base + a * 1000000), "end": iso(base + b * 1000000)}},
            )

        c.wait_for(lambda: call("GET", "/cameras/" + cam["id"]).get("applied_revision"), "binding applied")
        copied = recover(30, 35)
        assert replay.interrupt_at is None
        assert copied["duration_us"] < 5000000, copied
        recover(30, 35)
        c.wait_for(
            lambda: c.query(
                f"SELECT count(*) FROM bacuri_archive.blocks WHERE point_id='{point}' AND manifest ? 'edge_recording'"
            )
            != "0",
            "registered replay",
        )
        for start in range(30, 90, 5):
            recover(start, start + 5)

        def intervals():
            return json.loads(
                c.query(
                    f"SELECT coalesce(json_agg(manifest ORDER BY begin_utc_us),'[]') FROM bacuri_archive.blocks WHERE point_id='{point}' AND manifest ? 'edge_recording'"
                )
            )

        blocks = c.wait_for(
            lambda: (items := intervals()) and items[-1]["end_utc_us"] >= base + 89500000 and items,
            "last edge block",
        )

        def verify_frames(media):
            for block in media:
                data = subprocess.check_output(
                    [
                        "gst-launch-1.0",
                        "-q",
                        "filesrc",
                        "location="
                        + str(
                            Path(os.environ["BACURI_ARCHIVE_DIR"])
                            / "blocks"
                            / block["block_id"]
                            / "media.mp4"
                        ),
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
                for pos in range(0, len(data), 128 * 64):
                    ordinal = sum(1 << bit for bit in range(8) if data[pos + bit * 16 + 8] > 128)
                    assert ordinal == ((block["begin_utc_us"] - base) // 500000 + pos // (128 * 64)) % 256

        verify_frames(blocks)
        assert all(a["end_utc_us"] <= b["begin_utc_us"] for a, b in zip(blocks, blocks[1:]))
        remaining = call("GET", path)["gaps"]
        span = lambda key: int(datetime.fromisoformat(key).timestamp() * 1000000)
        assert [(span(g["start"]) - base, span(g["end"]) - base) for g in remaining] == [
            (70000000, 75000000)
        ], remaining
        for start in range(30, 90, 5):
            recover(start, start + 5)
        assert intervals() == blocks
        cfg = f"/points/{point}/video-rule"
        # A second, fully available minute starts with an empty server-side archive.
        c.query(
            f"INSERT INTO bacuri_archive.gaps VALUES('{c.SITE}','{point}',1,{base+90000000},{base+150000000},'source_offline')"
        )
        began = time.monotonic()
        for start in range(90, 150, 10):
            recover(start, start + 10)
        c.wait_for(lambda: len(call("GET", path)["gaps"]) == 1, "complete second edge minute")
        complete = [b for b in intervals() if b["begin_utc_us"] >= base + 90000000]
        elapsed = time.monotonic() - began
        assert sum(b["end_utc_us"] - b["begin_utc_us"] for b in complete) == 60000000 and elapsed < 120
        verify_frames(complete)
        print("Complete recovered interval: 60 s, elapsed", round(elapsed, 3), "s", flush=True)

        rule = {
            "enabled": True,
            "source": "onvif",
            "include": [],
            "exclude": [],
            "line": None,
            "notify": True,
            "record": True,
            "preset": "door",
            "min_cells": 1,
            "threshold": 5,
            "cooldown": 1,
            "schedule": [],
            "zone": "UTC",
        }
        call("PUT", cfg, {"revision": "0", "rule": rule})
        c.wait_for(lambda: device.active, "event subscription")
        instant = utc()
        device.motion(instant)
        device.motion(instant)
        c.wait_for(lambda: device.preset == "door", "rule preset acknowledgement")
        call("PUT", cfg, {"revision": "1", "rule": {**rule, "enabled": False}})
        device.motion(instant)
        c.wait_for(
            lambda: c.query(
                "SELECT preset_state FROM bacuri_events.video_matches WHERE preset_state='acknowledged'"
            ),
            "committed reaction",
        )
        assert (
            c.query("SELECT count(*) FROM bacuri_inventory.ptz_commands WHERE command='preset_goto'") == "1"
        )
        alert = c.query("SELECT alert_id FROM bacuri_events.alerts LIMIT 1")
        card = call("GET", f"/alerts/{alert}/work")
        assert card["rule"]["revision"] == "1" and card["rule"]["binding"] == "1", card
        print("Verified edge frames, interrupted recovery, erased interval and duplicate replay", flush=True)


if __name__ == "__main__":
    run()
