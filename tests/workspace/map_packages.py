"""Qualify archive selection and resumable HTTPS map delivery through the installed API and browser."""

import base64
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import io
import json
import os
import shutil
import sqlite3
import ssl
import subprocess
import threading
from collections import defaultdict
import zipfile

from feed import browser_session, c, js

BASE = f"sites/{c.SITE}/maps"
INTERRUPT_AT = 131071
PAUSE_BYTES = 32768


def package(filename, key, stamp, large=False):
    file = c.ROOT / filename
    shutil.copyfile(c.ROOT / "current.mbtiles", file)
    with sqlite3.connect(file) as db:
        db.execute("INSERT INTO metadata VALUES('region_id',?)", (key,))
        db.execute("UPDATE metadata SET value=? WHERE name='name'", (key,))
        db.execute("UPDATE metadata SET value=? WHERE name='osm_base_timestamp'", (stamp,))
        if large:
            db.executescript(
                "CREATE TABLE padding(data blob); INSERT INTO padding VALUES(zeroblob(4600000));"
            )
    return file


class Mirror(BaseHTTPRequestHandler):
    def do_GET(self):
        file = self.server.folder / (self.path.lstrip("/") or "catalog.json")
        if not file.is_file():
            self.send_error(404)
            return
        data = file.read_bytes()
        interrupt = False
        hold_open = False
        if file.suffix == ".json":
            self.send_response(200)
        else:
            start, end = map(int, self.headers["Range"].removeprefix("bytes=").split("-"))
            self.server.ranges[file.name].append(start)
            offline = file.name in self.server.interrupt and not self.server.connected.is_set()
            interrupt = offline and start == 0
            hold_open = (
                file.name == self.server.pause_file
                and start == INTERRUPT_AT
                and self.server.test_active_pause.is_set()
            )
            if offline and start and not hold_open:
                self.send_error(503)
                return
            self.send_response(206)
            self.send_header("Content-Range", f"bytes {start}-{end}/{len(data)}")
            data = data[start : end + 1]
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        if hold_open:
            self.wfile.write(data[:PAUSE_BYTES])
            self.wfile.flush()
            self.server.release_response.wait(30)
        else:
            self.wfile.write(data[:INTERRUPT_AT] if interrupt else data)
            self.wfile.flush()
        self.close_connection = True

    def log_message(self, *args):
        pass


def qualify(wait, button, raw):
    from maps import click

    env = dict(os.environ, BACURI_MAP_CA_FILE=os.environ["BACURI_TLS_CERT"])

    def start():
        proc = subprocess.Popen([c.CORE, "serve"], env=env)
        c.wait_for(lambda: c.http("GET", "session")[0] == 401, "map management server")
        return proc

    server = ThreadingHTTPServer(("localhost", 0), Mirror)
    server.folder = c.ROOT / "catalog"
    server.connected = threading.Event()
    server.ranges = defaultdict(list)
    server.interrupt = set()
    server.pause_file = None
    server.test_active_pause = threading.Event()
    server.release_response = threading.Event()
    tls = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    tls.load_cert_chain(env["BACURI_TLS_CERT"], env["BACURI_TLS_KEY"])
    server.socket = tls.wrap_socket(server.socket, server_side=True)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    core = start()
    try:
        admin = c.login("installer")
        viewer = c.login("viewer")

        def api(method, path, body=None, status=200):
            reply = c.http(method, BASE + path, body, admin)
            assert reply[0] == status, reply
            return reply[2]

        def installed():
            return {r["id"]: r for r in api("GET", "")["regions"]}

        def state(region, expected):
            return any(
                j["region_id"] == region and j["state"] == expected for j in api("GET", "/management")["jobs"]
            )

        def queue(source, mode, ids):
            api("POST", "/jobs", {"source_id": source, "mode": mode, "regions": ids}, 204)

        def upload(call, filename):
            container = io.BytesIO()
            with zipfile.ZipFile(container, "w") as wrapper:
                wrapper.write(filename, filename.name)
            remote = call("POST", "/se/file", {"file": base64.b64encode(container.getvalue()).decode()})
            node = wait(
                call, "document.querySelector('.ws-map-packages input[type=file]')", "package chooser"
            )
            call(
                "POST", "/element/" + node["element-6066-11e4-a52e-4f735466cecf"] + "/value", {"text": remote}
            )

        first = "2020-01-01T00:00:00Z"
        second = "2020-01-02T00:00:00Z"
        third = "2020-01-03T00:00:00Z"
        a = package("north.mbtiles", "North", first)
        b = package("south.mbtiles", "South", first)
        archive = c.ROOT / "regions.zip"
        with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as z:
            z.write(a, "nested/north.mbtiles")
            z.write(b, "elsewhere/south.mbtiles")
        assert c.http("GET", BASE + "/management", headers=viewer)[0] == 403
        assert (
            c.http("POST", BASE + "/sources", {"kind": "file", "bytes": 10}, {**admin, "X-CSRF-Token": ""})[0]
            == 403
        )
        with browser_session("chrome", admin, "/maps", software_map=True, workspace_coverage=raw) as (
            call,
            _,
        ):
            button(call, "Установить регионы")
            invalid = c.ROOT / "invalid.zip"
            invalid.write_bytes(b"not a map package")
            upload(call, invalid)
            wait(
                call, "document.querySelector('.ws-map-packages').innerText.includes('Выберите корректный пакет карт')",
                "localized invalid package",
            )
            upload(call, archive)
            node = wait(
                call,
                "[...document.querySelectorAll('.ws-map-packages li label')].find(l=>l.textContent.includes('North'))?.querySelector('input')",
                "North option",
            )
            click(call, node)
            button(call, "Установить выбранные")
            c.wait_for(lambda: "North" in installed(), "selective install")
            assert "South" not in installed()
            previous = installed()["North"]["revision"]
            a = package("north2.mbtiles", "North", second)
            with zipfile.ZipFile(archive, "w") as z:
                z.write(a, "renamed.mbtiles")
                z.write(b, "south.mbtiles")
            button(call, "Обновить установленные")
            upload(call, archive)
            node = wait(
                call,
                "[...document.querySelectorAll('label')].find(l=>l.textContent==='Выбрать все')?.querySelector('input')",
                "select all updates",
            )
            assert not js(
                call,
                "return [...document.querySelectorAll('.ws-map-packages li label')].some(l=>l.textContent.includes('South'))",
            )
            click(call, node)
            button(call, "Обновить выбранные")
            c.wait_for(lambda: installed()["North"]["revision"] != previous, "update installed subset")
            assert "South" not in installed()
        north = package("north3.mbtiles", "North", third, large=True)
        south = package("south-large.mbtiles", "South", first, large=True)
        east = package("east.mbtiles", "East", first)
        west = package("west.mbtiles", "West", first)
        subprocess.run(
            ["python3", "tools/map-catalog.py", server.folder, north, south, east, west, "--zip", archive],
            check=True,
        )
        entries = json.loads((server.folder / "catalog.json").read_text())["regions"]
        files = {entry["id"]: entry["sha256"] + ".mbtiles" for entry in entries}
        server.interrupt.update([files["North"], files["South"]])
        url = f"https://localhost:{server.server_port}/catalog.json"
        with browser_session("chrome", admin, "/maps", software_map=True, workspace_coverage=raw) as (
            call,
            _,
        ):
            button(call, "Установить регионы")
            address = wait(
                call, "document.querySelector('.ws-map-packages input[type=url]')", "catalog address"
            )
            call(
                "POST", "/element/" + address["element-6066-11e4-a52e-4f735466cecf"] + "/value", {"text": url}
            )
            button(call, "Показать каталог")
            for name in ("South", "East"):
                node = wait(
                    call,
                    "[...document.querySelectorAll('.ws-map-packages li label')].find(l=>l.textContent.includes(arguments[0]))?.querySelector('input')",
                    "online region " + name,
                    name,
                )
                click(call, node)
            button(call, "Установить выбранные")
            wait(
                call, "document.body.innerText.includes('Регионы добавлены в очередь')", "persisted selection"
            )
        c.wait_for(
            lambda: state("South", "retrying") and "East" in installed(),
            "other selected region completed after disconnect",
        )
        pending = next(j for j in api("GET", "/management")["jobs"] if j["region_id"] == "South")
        assert pending["received"] == INTERRUPT_AT and files["West"] not in server.ranges, pending
        server.pause_file = files["South"]
        server.test_active_pause.set()
        paused_offset = INTERRUPT_AT + PAUSE_BYTES
        c.wait_for(
            lambda: any(
                j["region_id"] == "South" and j["state"] == "downloading" and j["received"] == paused_offset
                for j in api("GET", "/management")["jobs"]
            ),
            "live download received more bytes in an open response",
        )
        control = BASE + "/jobs/" + pending["id"] + "/download"
        assert c.http("PUT", control, {"paused": True}, viewer)[0] == 403
        assert c.http("PUT", control, {"paused": True}, {**admin, "X-CSRF-Token": ""})[0] == 403
        with browser_session("chrome", admin, "/maps", software_map=True, workspace_coverage=raw) as (
            call,
            _,
        ):
            button(call, "Приостановить загрузку")
            c.wait_for(lambda: state("South", "paused"), "manual pause")
        prior = installed()["North"]["revision"]
        queue(pending["source_id"], "update", ["North", "West"])
        c.wait_for(lambda: state("North", "retrying"), "interrupted update inside first HTTP response")
        server.release_response.set()
        assert installed()["North"]["revision"] == prior and "West" not in installed()
        c.stop(core)
        core = None
        server.connected.set()
        core = start()
        c.wait_for(
            lambda: installed()["North"]["source_time"] == third,
            "automatic download resume after Core restart",
        )
        assert state("South", "paused") and "South" not in installed()
        with browser_session("chrome", admin, "/maps", software_map=True, workspace_coverage=raw) as (
            call,
            _,
        ):
            button(call, "Продолжить загрузку")
        c.wait_for(lambda: "South" in installed(), "manual resume after Core restart")
        for name in ("North", "South"):
            offsets = server.ranges[files[name]]
            assert offsets.count(0) == 1 and INTERRUPT_AT in offsets, offsets
        assert paused_offset in server.ranges[files["South"]], server.ranges
        assert files["West"] not in server.ranges
        current = installed()["North"]["revision"]
        damaged = package("north-damaged.mbtiles", "North", "2020-01-04T00:00:00Z", large=True)
        subprocess.run(["python3", "tools/map-catalog.py", server.folder, damaged], check=True)
        index = json.loads((server.folder / "catalog.json").read_text())
        remote_file = server.folder / (index["regions"][0]["sha256"] + ".mbtiles")
        with remote_file.open("r+b") as output:
            output.seek(-1, os.SEEK_END)
            output.write(b"x")
        bad_source = api(
            "POST", "/sources", {"kind": "online", "url": url.removesuffix("/catalog.json")}, 201
        )["id"]
        c.wait_for(lambda: api("GET", "/sources/" + bad_source)["state"] == "ready", "new release catalog")
        queue(bad_source, "update", ["North"])
        c.wait_for(lambda: state("North", "failed"), "package digest mismatch")
        failed = next(j for j in api("GET", "/management")["jobs"] if j["region_id"] == "North")
        assert failed["error"] == "checksum_mismatch", failed
        assert installed()["North"]["revision"] == current
        (server.folder / "catalog.json").write_text("{}")
        invalid_source = api("POST", "/sources", {"kind": "online", "url": url}, 201)["id"]
        c.wait_for(lambda: api("GET", "/sources/" + invalid_source)["error"] == "invalid_catalog",
                   "invalid catalog metadata")
        print(
            f"HTTPS resumed at byte {INTERRUPT_AT} after disconnect and at byte {paused_offset} after manual pause; "
            "selection, restart and SHA-256 verified",
            flush=True,
        )
    finally:
        server.release_response.set()
        c.stop(core)
        server.shutdown()
        server.server_close()
        thread.join()
