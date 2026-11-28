import concurrent.futures
from http.client import HTTPSConnection
import json
import os
from pathlib import Path
import secrets
import socket
from contextlib import ExitStack
import ssl
import subprocess
import sys
import time
import uuid

sys.path.insert(0, str(Path(__file__).resolve().parent))
from openapi import Contract  # noqa: E402

ROOT = Path(sys.argv[1])
BIN = Path(os.environ["BACURI_TEST_BIN"])
CORE = BIN / "bacuri-vms-core"
RECORDER = BIN / "bacuri-vms-recorder"
SITE = os.environ["BACURI_SITE_ID"]
ORIGIN = os.environ["BACURI_ORIGIN"]
TLS = ssl.create_default_context(cafile=os.environ["BACURI_TLS_CERT"])
PASSWORD = secrets.token_hex(20)
CONTRACT = Contract()


def query(sql):
    return subprocess.check_output(["psql", "-XAtq", "-v", "ON_ERROR_STOP=1", "-c", sql], text=True).strip()


def http(method, path, body=None, headers=None, timeout=8, origin=True):
    connection = HTTPSConnection("localhost", 8443, context=TLS, timeout=timeout)
    try:
        # A desktop client sends no browser origin.
        base = {"Content-Type": "application/json", **({"Origin": ORIGIN} if origin else {})}
        connection.request(method, "/api/v1/" + path, body=json.dumps(body) if body is not None else None,
                           headers={**base, **(headers or {})})
        response = connection.getresponse()
        raw = response.read()
        result = json.loads(raw) if raw else None
        # Every response the scenarios receive must be described by the OpenAPI contract.
        CONTRACT.check(method, "/api/v1/" + path, response.status, response.getheader("Content-Type"), result)
        return response.status, dict(response.getheaders()), result
    finally:
        connection.close()


def check_https_transport():
    # Incomplete TLS handshakes must leave authenticated request workers available.
    with ExitStack() as sockets:
        for _ in range(6):
            sockets.enter_context(socket.create_connection(("localhost", 8443), timeout=2))
        connection = HTTPSConnection("localhost", 8443, context=TLS, timeout=3)
        try:
            connection.connect()
            established = connection.sock
            for _ in range(2):
                connection.request("GET", "/api/v1/session")
                response = connection.getresponse()
                assert response.status == 401 and json.loads(response.read())["code"] == "unauthenticated"
                assert connection.sock is established
            for body, headers, expected in (
                (b" " * 32769, {}, (413, "payload_too_large")),
                (b"", {"Expect": "unsupported"}, (417, "expectation_failed")),
                (b"", {"X-Padding": "x" * 8192}, (431, "headers_too_large")),
            ):
                connection.request("POST", "/api/v1/session", body=body, headers=headers)
                response = connection.getresponse()
                problem = json.loads(response.read())
                CONTRACT.check("POST", "/api/v1/session", response.status, response.getheader("Content-Type"), problem)
                assert (response.status, problem["code"]) == expected, problem
                assert problem["request_id"] == response.getheader("X-Request-Id")
        finally:
            connection.close()


def login(name):
    from factor import complete

    fields = {"username": name, "password": PASSWORD}
    response = http("POST", "session", fields)
    if response[0] == 202 or (response[0] == 403 and response[2]["code"] == "enrollment_required"):
        response = complete(http, CORE, ROOT, ORIGIN, fields, response)
    status, headers, body = response
    assert status == 201, (status, body)
    cookie = headers["Set-Cookie"]
    assert all(attr in cookie for attr in ("Secure", "HttpOnly", "SameSite=Strict", "Path=/"))
    assert body["username"] == name
    return {"Cookie": cookie.split(";")[0], "X-CSRF-Token": body["csrf_token"]}


def metric(process, series):
    """Value of a series in the exposition of a service, or None before the service exported it."""
    path = Path(os.environ["BACURI_METRICS_DIRECTORY"]) / f"{process}.prom"
    if not path.exists():
        return None
    for line in path.read_text().splitlines():
        name, _, value = line.rpartition(" ")
        if name == series:
            return float(value)
    return None


def wait_for(check, label):
    expires = time.monotonic() + 20
    while time.monotonic() < expires:
        try:
            value = check()
            if value:
                return value
        except (ConnectionError, OSError):
            pass
        time.sleep(0.1)
    raise AssertionError(f"Timed out waiting for {label}")


def stop(process):
    if process is not None:
        process.terminate()
        process.wait(timeout=10)


def main():
    for name, role in (("admin", "administrator"), ("viewer", "operator")):
        subprocess.run([CORE, "create-user", name, role], input=PASSWORD + "\n", text=True, check=True)
    assert query("SELECT name FROM bacuri_inventory.sites") == "Existing site"
    core = subprocess.Popen([CORE, "serve"])
    recorder = None
    recorder_env = dict(os.environ, BACURI_KEY_FILE=str(ROOT / "recorder.key"),
                        BACURI_CAMERA_NETWORKS=os.environ["BACURI_TEST_CAMERA_NETWORK"])
    cameras = f"sites/{SITE}/cameras"
    points = f"sites/{SITE}/points"
    try:
        wait_for(lambda: http("GET", "session")[0] == 401, "HTTPS")
        check_https_transport()
        assert http("POST", "session", {"username": "admin", "password": "wrong"})[0] == 401
        admin = login("admin")
        viewer = login("viewer")
        assert http("GET", "session", headers=viewer)[2]["site_roles"][0]["role"] == "operator"
        membership = http("GET", "session", headers=admin)[2]["site_roles"][0]
        assert membership["time_zone"] == "UTC"
        profile_path = f"sites/{SITE}/keyboard-profile"
        profile = {"revision": membership["keymap_revision"], "bindings": {"palette": "Ctrl+Alt+P"}}
        assert http("PUT", profile_path, profile, viewer)[0] == 403
        assert http("PUT", profile_path, {**profile, "bindings": {"archive": "Ctrl+Alt+T"}}, admin)[0] == 400
        assert http("PUT", profile_path, profile, admin)[0] == 200
        stale_profile = http("PUT", profile_path, profile, admin)
        assert stale_profile[0] == 409 and stale_profile[2]["code"] == "revision_conflict"
        assert http("GET", "session", headers=viewer)[2]["site_roles"][0]["keymap"] == profile["bindings"]
        assert query("SELECT count(*) FROM bacuri_access.access_audit WHERE action='keyboard_profile.publish'") == "1"
        import desktop_session
        desktop_session.check(http, query, login, "viewer")
        config = {"name": "Gate", "main_url": "rtsp://rtsp-camera:8554/main", "secondary_url": "rtsp://rtsp-camera:8554/sub",
                  "credentials": {"username": "camera", "password": secrets.token_hex(16)}}
        key = str(uuid.uuid4())
        write = {**admin, "Idempotency-Key": key}
        assert http("POST", cameras, config, {**write, "Origin": "https://other.invalid"})[0] == 403
        assert http("POST", cameras, config, {**write, "X-CSRF-Token": ""})[0] == 403
        assert http("POST", cameras, config, viewer)[0] == 403
        assert http("GET", cameras, headers=viewer)[0] == 403
        assert http("GET", f"sites/{uuid.uuid4()}/cameras", headers=admin)[0] == 404
        assert http("POST", cameras, {**config, "main_url": "rtsp://127.0.0.1/main"}, write)[0] == 422
        with concurrent.futures.ThreadPoolExecutor(2) as pool:
            responses = list(pool.map(lambda _: http("POST", cameras, config, write), range(2)))
        status, headers, first = responses[0]
        assert status == 201, (status, first)
        assert responses[1][0] == 201 and responses[1][2] == first
        audit_id = query("SELECT request_id FROM bacuri_inventory.configuration_audit")
        assert audit_id in {reply[1]["X-Request-Id"] for reply in responses}
        assert first["state"] == "applying" and first["applied_revision"] is None
        assert first["has_credentials"] and "credentials" not in first
        path = cameras + "/" + first["id"]
        assert headers["Location"] == "/api/v1/" + path
        assert http("POST", cameras, {**config, "name": "Other"}, write)[0] == 409
        assert len(http("GET", cameras, headers=admin)[2]) == 1
        assert http("GET", points, headers=viewer)[2] == []
        point = first["point_id"]
        receipt_path = points + "/" + point + "/snapshot-receipts"
        capture_headers = {**viewer, "Idempotency-Key": str(uuid.uuid4())}
        capture = {"access_revision": "0", "pts_ns": "10000000", "width": 320, "height": 180, "sha256_rgba8": "a" * 64}
        assert http("POST", receipt_path, capture, capture_headers)[0] == 404
        user_id = http("GET", "session", headers=viewer)[2]["user_id"]
        grant = {"user_id": user_id, "point_id": point, "action": "export"}
        grant_path = f"sites/{SITE}/grants/{uuid.uuid4()}"
        def grant_headers():
            tag = http("GET", f"sites/{SITE}/grants", headers=admin)[1]["ETag"]
            return {**admin, "If-Match": tag}
        assert http("PUT", grant_path, grant, grant_headers())[0] in (200, 201)
        assert http("GET", points, headers=viewer)[2] == []
        assert http("POST", receipt_path, capture, capture_headers)[0] == 404
        subprocess.run([CORE, "grant-point", "viewer", point], check=True)
        details = http("GET", points, headers=viewer)[2][0]
        assert details["can_snapshot"]
        capture["access_revision"] = details["access_revision"]
        with concurrent.futures.ThreadPoolExecutor(2) as pool:
            captures = list(pool.map(lambda _: http("POST", receipt_path, capture, capture_headers), range(2)))
        receipt = captures[0][2]["receipt"]
        assert uuid.UUID(receipt) and receipt != capture_headers["Idempotency-Key"]
        assert all(status == 200 and result["receipt"] == receipt for status, _, result in captures)
        assert query("SELECT id::text FROM bacuri_access.access_audit WHERE action='snapshot.capture'") == receipt
        assert http("POST", receipt_path, {**capture, "width": 640}, capture_headers)[0] == 409
        assert query("SELECT count(*) FROM bacuri_access.access_audit WHERE action='snapshot.capture'") == "1"
        assert http("DELETE", grant_path, headers=grant_headers())[0] in (200, 204)
        assert http("POST", receipt_path, capture, capture_headers)[0] == 403
        assert http("PUT", path, config, admin)[0] == 428
        stale = http("PUT", path, config, {**admin, "If-Match": '"stale"'})
        assert stale[0] == 412 and stale[2]["current"] == first
        assert query("SELECT count(*) FROM bacuri_inventory.configuration_audit") == "1"
        wait_for(lambda: query("SELECT count(*) FROM bacuri_inventory.recorder_attempt") == "1", "durable pending snapshot")
        recorder = subprocess.Popen([RECORDER], env=recorder_env)

        def current(state, revision):
            status, headers, value = http("GET", path, headers=admin)
            if status == 200 and value["state"] == state and value["desired_revision"] == revision:
                return headers["ETag"], value
            return None

        tag, _ = wait_for(lambda: current("applied", "1"), "first applied snapshot")
        retained = {k: v for k, v in config.items() if k != "credentials"}
        rejected = http("PUT", path, {**retained, "main_url": "rtsp://192.168.1.3/main"}, {**admin, "If-Match": tag})
        assert rejected[0] == 200 and rejected[2]["has_credentials"], rejected
        tag, value = wait_for(lambda: current("rejected", "2"), "rejected snapshot")
        assert value["apply_failure"] == "invalid_configuration" and value["applied_revision"] == "1"
        stop(recorder)
        assert recorder.returncode == 0
        recorder = None
        stop(core)
        core = None
        with (ROOT / "restart.log").open("w") as log:
            recorder = subprocess.Popen([RECORDER], env=recorder_env, stderr=log)
        def restored_profiles():
            lines = (ROOT / "restart.log").read_text().splitlines()
            samples = [json.loads(line) for line in lines if line.startswith("{") and line.endswith("}")]
            return {entry["profile"] for entry in samples
                    if entry.get("state") == "receiving" and entry.get("device") == first["id"]}

        wait_for(lambda: restored_profiles() == {"main", "secondary"}, "both RTSP profiles without the core")
        log = (ROOT / "restart.log").read_text()
        assert "Recorder active revision 1, cameras 1" in log
        assert config["credentials"]["password"] not in log
        core = subprocess.Popen([CORE, "serve"])
        wait_for(lambda: http("GET", "session", headers=admin)[0] == 200, "persisted browser session")
        response = http("PUT", path, {**retained, "credentials": None}, {**admin, "If-Match": tag})
        assert response[0] == 200, response
        _, final = wait_for(lambda: current("applied", "3"), "latest applied snapshot")
        assert final["has_credentials"] is False and final["applied_revision"] == "3"
        assert query("SELECT count(*) FROM bacuri_inventory.configuration_audit") == "3"
        assert query("SELECT sealed_credentials IS NULL FROM bacuri_inventory.camera_registry") == "t"
        for file in ROOT.glob("recorder.db*"):
            assert config["credentials"]["password"].encode() not in file.read_bytes()
        assert http("DELETE", "session", headers=admin)[0] == 204
        assert http("GET", cameras, headers=admin)[0] == 401
        with socket.create_connection(("localhost", 8443), timeout=3):
            stop(core)
            assert core.returncode == 0
            core = None
        print("HTTPS registry, scope, replay, revision conflict, audit, gRPC rejection and offline recovery passed")
    finally:
        stop(core)
        stop(recorder)


if __name__ == "__main__":
    main()
