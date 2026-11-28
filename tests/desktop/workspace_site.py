"""Serve the desktop session and point list used by the workstation integration test."""

from datetime import UTC, datetime, timedelta
import json
import ssl
import sys
from http.server import BaseHTTPRequestHandler, HTTPServer


site_id = "00000000-0000-4000-8000-000000000001"


second_site = "00000000-0000-4000-8000-000000000004"


class Site(BaseHTTPRequestHandler):
    def log_message(self, *_):
        pass

    def respond(self, status, body):
        data = json.dumps(body).encode() if body is not None else b""
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def do_POST(self):
        if self.headers.get("Authorization") is not None: return self.respond(400, {})
        body = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
        if self.path != "/api/v1/session/desktop" or body.get("code") != "test-code":
            return self.respond(400, {})
        self.server.active = True
        self.respond(201, {"token": self.server.token, "site_roles": [{"site_id": site_id, "role": "operator"},{"site_id": second_site, "role": "viewer"}],
                           "user_id": "00000000-0000-4000-8000-000000000002",
                           "expires_at": (datetime.now(UTC) + timedelta(minutes=5)).isoformat()})

    def do_GET(self):
        if not self.server.active or self.headers.get("Authorization") != "Bearer " + self.server.token:
            return self.respond(401, {})
        if self.path == f"/api/v1/sites/{second_site}/points":
            return self.respond(200,[{"id":"second","name":"South","connection":"offline","recording":"stopped"}])
        if self.path != f"/api/v1/sites/{site_id}/points":
            return self.respond(404, {})
        self.server.reads += 1
        if self.server.reads == 2:
            return self.respond(503, {"code":"dependency_unavailable"})
        self.respond(200, [{"id": "00000000-0000-4000-8000-000000000003", "name": "Gate A", "connection": "online", "recording": "current"}])

    def do_DELETE(self):
        if self.path != "/api/v1/session" or self.headers.get("Authorization") != "Bearer " + self.server.token:
            return self.respond(403, {})
        self.server.active = False
        self.respond(204, None)


with HTTPServer(("127.0.0.1", 0), Site) as site:
    tls = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    tls.load_cert_chain(sys.argv[1], sys.argv[2])
    site.socket = tls.wrap_socket(site.socket, server_side=True)
    site.token = "d1." + "a" * 64
    site.active = False
    site.reads = 0
    print(f"https://127.0.0.1:{site.server_port}", flush=True)
    site.serve_forever()
