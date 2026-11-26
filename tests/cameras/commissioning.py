"""Exercise the delivered wizard, camera transport and versioned playback on an isolated segment."""
from contextlib import ExitStack, contextmanager
import json
import os
from pathlib import Path
import socket
from socketserver import BaseRequestHandler, ThreadingTCPServer
import ssl
import sqlite3
import subprocess
import sys
from threading import Thread
import uuid

from check import streams
from emulator import serve
from soap import message_id

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "workspace"))
from scenarios import browser_session, button, c, capture, field, js, playback, report, wait

from operations import exercise

SITE = f"sites/{c.SITE}"
CAMERAS = SITE + "/cameras"


def post(path, data, user, key=None):
    headers = user if key is None else {**user, "Idempotency-Key": key}
    status, _, result = c.http("POST", path, data, headers)
    return status, result


def discover(admin):
    code, devices = post(SITE + "/onvif/discovery", None, admin)
    assert code == 200 and len(devices) == 1, (code, devices)
    return devices[0]["selection"]


def profiles(admin, selection, password):
    return post(SITE + "/onvif/profiles", {"selection": selection, "username": "fixture", "password": password}, admin)


def expect(answer, status, reason):
    assert (answer[0], answer[1].get("code")) == (status, reason), (answer[0], answer[1].get("code"))


def cameras(admin):
    code, _, values = c.http("GET", CAMERAS, headers=admin)
    assert code == 200
    return values


def configure(device, replacement=False):
    device.serial = message_id()
    device.device_path = "/soap/device-B" if replacement else "/soap/device-A"
    device.media_path = "/service/video-B" if replacement else "/service/video-A"
    device.model = "Camera B" if replacement else "bacuri.vms ONVIF emulator"
    device.tokens = {"enc-high-47": (1280, 720), "enc-low-83": (640, 360)}
    paths = ["sub", "main"] if replacement else ["main", "sub"]
    device.stream_paths = dict(zip(device.tokens, paths, strict=True))


def choose(call, device, previous):
    button(call, "Поиск ONVIF и замена камеры")
    if previous:
        wait(call, "[...document.querySelectorAll('option')].some(o=>o.value===arguments[0])", "existing point", previous)
        js(call, """const s=[...document.querySelectorAll('select')].find(s=>[...s.options].some(o=>o.value===arguments[0]));
            s.value=arguments[0];s.dispatchEvent(new Event('change',{bubbles:true}));""", previous)
    button(call, "Найти камеры ONVIF")
    field(call, "camera_user", "fixture")
    field(call, "camera_password", device.password)
    button(call, "Получить профили")
    wait(call, "document.body.innerText.includes(arguments[0])", "device identity", device.serial)
    button(call, "Использовать профили")
    button(call, "Подключить камеру")


def decode(call, width, name):
    button(call, "Создать тестовую запись")
    button(call, "Открыть запись")
    button(call, "Подтвердить запись")
    wait(call, "document.body.innerText.includes('Запись подтверждена') && document.querySelector('video')?.videoWidth===arguments[0]",
         "new archive frame", width)
    capture(call, "chrome", name)


@contextmanager
def tls_probe(cert, key):
    # Inspect the wire prefix at every reconnect, including connections rejected during TLS.
    attempts = []
    accepted = []
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.load_cert_chain(cert, key)

    class Peer(BaseRequestHandler):
        def handle(self):
            self.request.settimeout(5)
            prefix = self.request.recv(1, socket.MSG_PEEK)
            attempts.append(prefix)
            if prefix == b"\x16":
                try:
                    with context.wrap_socket(self.request, server_side=True) as peer:
                        if peer.recv(1):
                            accepted.append(True)
                except (ssl.SSLError, OSError):
                    pass

    with ThreadingTCPServer(("127.0.0.1", 0), Peer) as server:
        thread = Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            yield server.server_address[1], attempts, accepted
        finally:
            server.shutdown()
            thread.join()


def media_negative(admin, host, cert, key):
    with tls_probe(cert, key) as (port, attempts, accepted):
        base = f"rtsps://{host}:{port}"
        # Only the main profile points at this listener, so two hellos establish a retry.
        status, camera = post(CAMERAS, {"name": "TLS check", "main_url": base + "/main",
                              "secondary_url": "rtsps://localhost:8554/sub"}, admin, str(uuid.uuid4()))
        assert status == 201, camera
        c.wait_for(lambda: len(attempts) >= 2, "recorder TLS reconnect")
        def rejected_certificate():
            code, _, issues = c.http("GET", CAMERAS + "/" + camera["id"] + "/stream-issues", headers=admin)
            return code == 200 and issues["main"] == "certificate_error"
        c.wait_for(rejected_certificate, "certificate error visible to administrator")
        assert all(prefix == b"\x16" for prefix in attempts) and not accepted
        assert c.query(f"SELECT count(*) FROM bacuri_archive.blocks WHERE point_id='{camera['point_id']}'") == "0"


def run():
    cert, key = Path(os.environ["BACURI_TLS_CERT"]), Path(os.environ["BACURI_TLS_KEY"])
    other_cert, other_key = c.ROOT / "untrusted.pem", c.ROOT / "untrusted.key"
    subprocess.run(["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "1", "-subj", "/CN=localhost",
                    "-addext", "subjectAltName=DNS:localhost", "-keyout", other_key, "-out", other_cert],
                   check=True, capture_output=True)
    for name, role in [("installer", "administrator"), ("viewer", "operator")]:
        subprocess.run([c.CORE, "create-user", name, role], input=c.PASSWORD + "\n", text=True, check=True)

    with ExitStack() as stack:
        def start(command, **kwargs):
            proc = subprocess.Popen(command, **kwargs)
            stack.callback(lambda: c.stop(proc) if proc.poll() is None else None)
            return proc

        start([c.CORE, "serve"])
        c.wait_for(lambda: c.http("GET", "session")[0] == 401, "HTTPS")
        admin = c.login("installer")
        expect(post(SITE + "/onvif/discovery", None, c.login("viewer")), 403, "forbidden")
        with serve(other_cert, other_key, multicast=True) as (untrusted, _):
            expect(profiles(admin, discover(admin), untrusted.password), 502, "camera_tls_error")
        with serve(cert, key, multicast=True) as (device, _):
            configure(device)
            address = device.base
            plain = "http://localhost:80" + device.device_path
            device.discovery_urls = [plain]
            code, devices = post(SITE + "/onvif/discovery", None, admin)
            assert code == 200 and [row["device_service"] for row in devices] == ["https://localhost" + device.device_path]
            device.discovery_urls = [plain, address + device.device_path]
            code, devices = post(SITE + "/onvif/discovery", None, admin)
            assert code == 200 and [row["device_service"] for row in devices] == [address + device.device_path]
            device.discovery_urls.clear()
            device.services.extend(["Imaging","DeviceIO"])
            device.zoom_range = (0, 1)
            device.base = address.replace("localhost", "127.0.0.1")
            expect(profiles(admin, discover(admin), device.password), 502, "camera_tls_error")
            device.base = address
            selection = discover(admin)
            expect(profiles(admin, selection, "wrong"), 422, "camera_authentication_failed")
            device.codec = "JPEG"
            expect(profiles(admin, selection, device.password), 422, "camera_incompatible_profiles")
            device.media2=True
            status, inspected=profiles(admin,selection,device.password)
            assert status==200 and [p["codec"] for p in inspected["profiles"]]==["h265","h264"], inspected.get("code")
            device.media2=False
            device.codec = "H264"
            device.stream_uris["enc-low-83"] = "rtsps://localhost:8554/main"
            expect(profiles(admin, selection, device.password), 422, "camera_incompatible_profiles")
            device.stream_uris["enc-low-83"] = "rtsp://localhost:8554/sub"
            expect(profiles(admin, selection, device.password), 422, "camera_transport_denied")
            device.stream_uris.clear()
            device.tokens["other-encoder"] = (320, 180)
            device.stream_uris["other-encoder"] = "rtsp://localhost:8554/other"
            code, info = profiles(admin, selection, device.password)
            assert code == 200 and [p["token"] for p in info["profiles"]] == ["enc-high-47", "enc-low-83"], code
            assert info["ptz"] and info["events"]
            print("Discovery, authenticated service traversal and HTTPS rejection checks passed", flush=True)
            stack.enter_context(streams(cert, key, c.BIN / "tests/fixtures", audio=True))
            recorder_env = {**os.environ, "BACURI_KEY_FILE": str(c.ROOT / "recorder.key")}
            recorder = start([c.RECORDER], env=recorder_env)
            start([playback.MEDIA])
            with browser_session("chrome", admin, "/cameras?setup=manual", workspace_coverage=report / "chrome") as (call, browser_version):
                choose(call, device, None)
                decode(call, 1280, "first-binding")
                first, = cameras(admin)
                assert first["binding_revision"] == "1" and first["serial"] == device.serial
                code, _, points = c.http("GET", SITE + "/points", headers=admin)
                assert code == 200
                point = next(row for row in points if row["id"] == first["point_id"])
                assert "zoom_in" in point["ptz"]["commands"] and "zoom_out" not in point["ptz"]["commands"]
                expect(post(SITE + "/points/" + point["id"] + "/ptz", {"command": "zoom_out", "device_id": first["id"]},
                            admin, str(uuid.uuid4())), 422, "camera_ptz_unsupported")
                duplicate = {k: first[k] for k in ("name", "main_url", "secondary_url")}
                duplicate["onvif_selection"] = info["selection"]
                expect(post(CAMERAS, dict(duplicate,onvif_selection="invalid"), admin, str(uuid.uuid4())), 400, "camera_selection_expired")
                expect(post(CAMERAS, duplicate, admin, str(uuid.uuid4())), 409, "camera_already_registered")
                device.tilt_range = (0, 0)
                device.zoom_range = (-1, 1)
                button(call, "Поиск ONVIF и замена камеры")
                button(call, "Найти камеры ONVIF")
                field(call, "camera_user", "fixture")
                field(call, "camera_password", device.password)
                button(call, "Получить профили")
                button(call, "Обновить подключённую камеру")
                wait(call,"document.body.innerText.includes('Профили и возможности камеры обновлены.')","capability refresh")
                refreshed,=cameras(admin)
                assert refreshed["id"]==first["id"] and refreshed["binding_revision"]==first["binding_revision"]
                exercise(call, device, first, admin)
                print("PTZ, PullPoint redelivery and decoded motion frame verified", flush=True)
                c.stop(recorder)
                configure(device, replacement=True)
                device.services = ["Media"]
                choose(call, device, first["id"])
                def pending():
                    rows = cameras(admin)
                    return len(rows) == 1 and rows[0]["binding_revision"] == "2" and rows[0]["state"] == "applying"
                c.wait_for(pending, "replacement pending while recorder is offline")
                generation = int(c.query("SELECT desired_revision FROM bacuri_inventory.recorder_delivery"))
                def persisted():
                    with sqlite3.connect(os.environ["BACURI_RECORDER_DB"]) as db:
                        return db.execute("SELECT max(revision) FROM decisions WHERE sealed IS NOT NULL").fetchone()[0] == generation

                # Hold the core acknowledgement until the recorder has durably applied the replacement.
                with subprocess.Popen(["psql", "-XAtq", "-v", "ON_ERROR_STOP=1"], stdin=subprocess.PIPE,
                                      stdout=subprocess.PIPE, text=True) as gate:
                    gate.stdin.write("BEGIN; LOCK bacuri_inventory.configuration_progress IN SHARE MODE; SELECT 'locked';\n")
                    gate.stdin.flush()
                    assert gate.stdout.readline().strip() == "locked"
                    try:
                        applying = start([c.RECORDER], env=recorder_env)
                        c.wait_for(persisted, "durable recorder decision before core acknowledgement")
                        c.stop(applying)
                        assert pending(), "the interrupted generation was acknowledged early"
                        start([c.RECORDER], env=recorder_env)
                    finally:
                        gate.stdin.write("COMMIT;\n\\q\n")
                        gate.stdin.flush()
                decode(call, 640, "second-binding")
                second, = cameras(admin)
                assert second["id"] != first["id"] and second["point_id"] == first["point_id"]
                assert c.http("POST", SITE + "/points/" + second["point_id"] + "/ptz", {"device_id":first["id"],"command":"right"}, {**admin,"Idempotency-Key":str(uuid.uuid4())})[0] == 409
                assert c.http("POST", SITE + "/points/" + second["point_id"] + "/ptz", {"device_id":second["id"],"command":"right"}, {**admin,"Idempotency-Key":str(uuid.uuid4())})[0] == 422
                assert not second["ptz"] and not second["events"]
                scene = c.http("GET", SITE+"/maps",headers=admin)[2]
                placed = next(p for p in scene["placements"] if p["point_id"]==second["point_id"])
                assert placed["view"]["state"]=="unknown" and "calibration" not in placed
                path = CAMERAS + "/" + second["id"] + "/previous-recording"
                status, _, previous = c.http("GET", path, headers=admin)
                assert status == 200 and previous["binding_revision"] == "1"
                media = SITE + "/points/" + second["point_id"] + "/media-sessions"
                selected = {"mode": "archive", **previous}
                media_key = str(uuid.uuid4())
                status, session = post(media, selected, admin, media_key)
                assert status == 201 and session["selection"] == selected
                wrong_binding = dict(selected, binding_revision="2")
                expect(post(media, wrong_binding, admin, media_key), 409, "idempotency_conflict")
                denied, _ = post(media, wrong_binding, admin, str(uuid.uuid4()))
                assert denied == 409
                assert c.http("DELETE", "media-sessions/" + session["id"], headers=admin)[0] == 204
                stale = dict(duplicate, point_id=first["point_id"], replace_device_id=first["id"])
                expect(post(CAMERAS, stale, admin, str(uuid.uuid4())), 409, "camera_binding_conflict")
                button(call, "Открыть предыдущую запись")
                wait(call, "document.querySelector('video')?.videoWidth===1280 && document.querySelector('video').readyState>=2",
                     "previous binding decoded")
                capture(call, "chrome", "previous-binding")
                assert not js(call, "return [...document.querySelectorAll('button')].some(b=>b.innerText==='Подтвердить запись')")
                print("Browser decoded both camera bindings after interrupted application", flush=True)
            media_negative(admin, "127.0.0.1", cert, key)
            media_negative(admin, "localhost", other_cert, other_key)
            # The fixture IP is the only cleartext exception inside the allowed camera networks.
            addresses = {"name": "Legacy", "main_url": "rtsp://127.0.0.1:8554/main", "secondary_url": "rtsp://127.0.0.1:8554/sub"}
            expect(post(CAMERAS, addresses, admin, str(uuid.uuid4())), 422, "camera_transport_denied")
            base = "rtsp://" + os.environ["BACURI_TEST_CAMERA_HOST"] + ":8554"
            code, legacy = post(CAMERAS, dict(addresses, main_url=base+"/main", secondary_url=base+"/sub"), admin, str(uuid.uuid4()))
            assert code == 201
            c.wait_for(lambda: c.query(f"SELECT count(*)>0 FROM bacuri_archive.blocks WHERE point_id='{legacy['point_id']}'") == "t",
                       "permitted legacy camera recording")
            assert c.query("SELECT count(*) FROM bacuri_inventory.camera_transport_audit") == "1"
            print("RTSPS certificate diagnostics, scoped legacy exception and archived bindings verified", flush=True)
            (report / "commissioning.json").write_text(json.dumps({
                "browser": browser_version, "video_source": "videotestsrc", "point_id": first["point_id"],
                "recordings": [{"binding_revision": "1", "width": 1280}, {"binding_revision": "2", "width": 640}],
                "previous_binding_reopened": True, "interrupted_application_recovered": True,
                "negative_controls": ["operator", "HTTPS issuer and name", "credentials", "codec", "duplicate",
                                      "RTSPS issuer and name on reconnect", "unlisted legacy address"]
            }, indent=2) + "\n")


if __name__ == "__main__":
    run()
