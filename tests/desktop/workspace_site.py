"""Serve authorized site resources and media commands for the workstation integration tests."""

from datetime import UTC, datetime, timedelta
import json
import ssl
import subprocess
import os
import uuid
import sys
from base64 import b64encode
from hashlib import sha1
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from time import monotonic, sleep
from urllib.parse import parse_qs, urlsplit


site_id = "00000000-0000-4000-8000-000000000001"
point = "00000000-0000-4000-8000-000000000003"
point_path = f"/api/v1/sites/{site_id}/points/{point}"
second_site = "00000000-0000-4000-8000-000000000004"


files = Path(os.environ["BACURI_COMMAND_FILES"])


class Site(BaseHTTPRequestHandler):
    def feed(self):
        accept = b64encode(sha1((self.headers["Sec-WebSocket-Key"] + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").encode()).digest()).decode()
        self.protocol_version = "HTTP/1.1"
        self.send_response(101)
        self.send_header("Upgrade", "websocket")
        self.send_header("Connection", "Upgrade")
        self.send_header("Sec-WebSocket-Accept", accept)
        self.send_header("Sec-WebSocket-Protocol", "bacuri.vms.alerts.v1")
        self.end_headers()
        self.close_connection = True
        self.connection.settimeout(5)
        header = self.rfile.read(2)
        if len(header) != 2 or not header[1] & 128:
            return
        length = header[1] & 127
        if length > 125:
            return
        mask, data = self.rfile.read(4), self.rfile.read(length)
        subscription = json.loads(bytes(byte ^ mask[i % 4] for i, byte in enumerate(data)))
        if subscription != {"type": "subscribe", "after": "1"}:
            return
        for event in ({"type": "upsert", "cursor": "2", "alert": {"id": "streamed", "point_id": point, "observed_at": "2000-01-01T12:15:00Z"}},
                      {"type": "remove", "cursor": "3", "alert_id": "streamed"}):
            deadline = monotonic() + 30
            while not (files / event["type"]).exists():
                if monotonic() > deadline:
                    return
                sleep(.025)
            payload = json.dumps(event, separators=(",", ":")).encode()
            size = len(payload)
            self.wfile.write(bytes([129, size]) + payload if size < 126 else bytes([129, 126]) + size.to_bytes(2, "big") + payload)
        try:
            self.connection.settimeout(30)
            self.rfile.read(2)
        except TimeoutError:
            pass

    def log_message(self, *_):
        pass

    def respond(self, status, body):
        data = json.dumps(body).encode() if body is not None else b""
        self.send_body(status, "application/json", data)

    def send_body(self, status, content_type, data):
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def do_POST(self):
        raw = self.rfile.read(int(self.headers["Content-Length"]))
        sid = self.path.split("/")[-2]
        if self.path == f"/api/v1/media-sessions/{sid}/offer" and sid in self.server.media:
            if not self.authorized():
                return self.respond(401, {})
            peer = subprocess.Popen([os.environ["BACURI_TEST_PEER"], str(len(raw))],
                                    stdin=subprocess.PIPE, stdout=subprocess.PIPE)
            self.server.media[sid] = peer
            peer.stdin.write(raw)
            peer.stdin.flush()
            size = int(peer.stdout.readline())
            return self.send_body(200, "application/sdp", peer.stdout.read(size))
        body = json.loads(raw)
        if self.path.startswith(point_path):
            if not self.authorized():
                return self.respond(401, {})
            if self.path == point_path + "/media-sessions":
                if not self.headers.get("Idempotency-Key") or body.get("accept_masked") is not True:
                    return self.respond(400, {})
                sid = str(uuid.uuid4())
                self.server.media[sid] = None
                return self.respond(201, {"id": sid, "video": {"codec": "h264"}, "expires_at":
                                    (datetime.now(UTC) + timedelta(minutes=1)).isoformat()})
            if self.path == point_path + "/snapshot-receipts" and body.get("access_revision") == "1":
                if body.get("width") == 320 and body.get("height") == 180 and len(body.get("sha256_rgba8","")) == 64:
                    return self.respond(200, {"receipt": "e8af2570-ef06-42f4-a70a-5831317a9e7b"})
            if self.path == point_path + "/ptz":
                if body.get("device_id") == "device" and body.get("command") in ("left", "stop", "preset_goto") and self.headers.get("Idempotency-Key"):
                    with (files / "ptz.log").open("a") as log:
                        print(body["command"], body.get("target", ""), file=log)
                    return self.respond(204, None)
            if self.path == point_path + "/exports":
                interval = body.get("interval", {})
                if body.get("mode") == "selection" and interval.get("start", "") < interval.get("end", ""):
                    if interval.get("start", "").startswith("2001-"): sleep(.35)
                    return self.send_body(200, "application/x-tar", b"test export payload")
            return self.respond(400, {})
        if self.headers.get("Authorization") is not None:
            return self.respond(400, {})
        if self.path != "/api/v1/session/desktop" or body.get("code") != "test-code":
            return self.respond(400, {})
        self.server.active = True
        self.respond(201, {"token": self.server.token, "site_roles": [{"site_id": site_id, "role": "operator", "time_zone": "Europe/Moscow"}, {"site_id": second_site, "role": "viewer"}],
                           "user_id": "00000000-0000-4000-8000-000000000002",
                           "expires_at": (datetime.now(UTC) + timedelta(minutes=5)).isoformat()})

    def authorized(self):
        return self.server.active and self.headers.get("Authorization") == "Bearer " + self.server.token

    def do_GET(self):
        if not self.authorized():
            return self.respond(401, {})
        if self.path == point_path + "/ptz":
            actions = [{"command": "preset_goto", "target": f"position-{i}", "label": f"Position {i}"} for i in range(1, 10)]
            return self.respond(200, {"device_id": "device", "actions": actions, "inputs": [], "history": []})
        if self.path == f"/api/v1/sites/{site_id}/monitor-layouts":
            return self.respond(200, [{"id": "wall", "name": "Entrance wall", "points": [point,None]}])
        if self.path == f"/api/v1/sites/{site_id}/alerts/stream":
            return self.feed()
        if self.path == f"/api/v1/sites/{site_id}/alerts":
            return self.respond(200, {"cursor": "1", "items": [{"id": "alarm", "point_id":point, "observed_at": "2026-01-02T12:00:00Z"},
                                              {"id": "forbidden-alarm", "point_id": "hidden", "observed_at": "2026-01-02T12:00:00Z"}]})
        if self.path == f"/api/v1/sites/{site_id}/investigations":
            return self.respond(200, [{"id": "case", "number": "CASE-72", "notes": "Gate inspection", "materials": []}])
        if self.path == f"/api/v1/sites/{second_site}/investigations":
            return self.respond(403, {"code": "forbidden"})
        if urlsplit(self.path).path == point_path + "/archive":
            query = parse_qs(urlsplit(self.path).query)
            if "end" not in query or "start" not in query:
                return self.respond(400, {})
            return self.respond(200, {"items": [{"state": "recorded", "interval": {"start":query["start"][0], "end":query["end"][0]}}]})
        if self.path == f"/api/v1/sites/{second_site}/points":
            return self.respond(200, [{"id": "second", "name": "South", "connection": "offline", "recording": "stopped"}])
        if self.path != f"/api/v1/sites/{site_id}/points":
            return self.respond(404, {})
        self.server.reads += 1
        if self.server.reads == 2:
            return self.respond(503, {"code": "dependency_unavailable"})
        self.respond(200, [{
            "id": point, "name": "Gate A", "can_preview": True, "can_snapshot": True, "can_export": True, "access_revision": "1",
            "ptz": {"device_id": "device", "commands": ["left", "stop"]},
            "connection": "online", "recording": "current",
        }])

    def do_DELETE(self):
        sid = self.path.split("/")[-1]
        if self.authorized() and self.path == f"/api/v1/media-sessions/{sid}" and sid in self.server.media:
            peer = self.server.media.pop(sid)
            if peer:
                peer.stdin.close()
                peer.wait(timeout=3)
            return self.respond(204, None)
        if self.path != "/api/v1/session" or self.headers.get("Authorization") != "Bearer " + self.server.token:
            return self.respond(403, {})
        self.server.active = False
        self.respond(204, None)


with ThreadingHTTPServer(("127.0.0.1", 0), Site) as site:
    tls = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    tls.load_cert_chain(sys.argv[1], sys.argv[2])
    site.socket = tls.wrap_socket(site.socket, server_side=True)
    site.token = "d1." + "a" * 64
    site.media = {}
    site.active = False
    site.reads = 0
    print(f"https://127.0.0.1:{site.server_port}", flush=True)
    site.serve_forever()
