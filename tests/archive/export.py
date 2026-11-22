"""Export a synthetic camera episode through the delivered Chrome interface."""

import base64
from io import BytesIO
from zipfile import ZipFile
from contextlib import ExitStack
from datetime import datetime
import json
import os
from pathlib import Path
import subprocess
import sys
import uuid

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "alerts"))
from feed import browser_session, c, js, motion, playback, send, snapshot


def run():
    report = Path(os.environ["BACURI_WORKSPACE_RESULTS"])
    with ExitStack() as stack:
        for name, role in [("admin", "administrator"), ("viewer", "operator")]:
            subprocess.run([c.CORE, "create-user", name, role], input=c.PASSWORD + "\n", text=True, check=True)
        for cmd in [[c.CORE, "serve"], [playback.MEDIA]]:
            stack.callback(c.stop, subprocess.Popen(cmd))
        c.wait_for(lambda: c.http("GET", "session")[0] == 401, "HTTPS")
        admin, viewer = c.login("admin"), c.login("viewer")

        def camera(name):
            code, _, cam = c.http("POST", f"sites/{c.SITE}/cameras", {
                "name": name, "main_url": "rtsp://rtsp-camera:8554/main",
                "secondary_url": "rtsp://rtsp-camera:8554/sub",
            }, {**admin, "Idempotency-Key": str(uuid.uuid4())})
            assert code == 201, cam
            return cam

        cam = camera("Export fixture")
        point, other = cam["point_id"], camera("Other point")["point_id"]
        subprocess.run([c.CORE, "grant-point", "viewer", point], check=True)
        blocks = [playback.fixture(cam, i, 320, [(255, 0, 0), (0, 255, 0)]) for i in (0, 1, 3)]
        playback.fixture(cam, 5, 320, [(0, 0, 255), (0, 255, 0)], 16 * 1024 * 1024)
        start = playback.ORIGIN_US
        c.query(f"INSERT INTO bacuri_archive.gaps VALUES('{c.SITE}','{point}',1,{start+2000000},{start+3000000},'source_offline')")
        send(motion(point))
        alert = snapshot(viewer)["items"][0]["id"]
        path = f"sites/{c.SITE}/points/{point}/exports"
        interval = playback.interval(.221, 3.779)
        payload = {"interval": interval, "alert_id": alert}
        refusals = {}

        def reject(label, expected, selection=payload, user=viewer, route=path):
            code, _, problem = c.http("POST", route, selection, user)
            assert (code, problem["code"]) == expected, (label, code, problem)
            refusals[label] = expected

        def grant():
            subprocess.run([c.CORE, "grant-export", "viewer", point, playback.utc(-20), playback.utc(20)], check=True)

        def revoke():
            subprocess.run([c.CORE, "revoke-export", "viewer", point], check=True)

        reject("no_export_grant", (403, "forbidden"))
        grant()
        reject("other_point", (403, "forbidden"), route=path.replace(point, other))
        reject("outside_grant", (403, "forbidden"), {"interval": playback.interval(-22, 1)})
        reject("outside_alert", (403, "forbidden"), {**payload, "interval": playback.interval(-19, 1)})
        reject("gap_only", (409, "media_unavailable"), {"interval": playback.interval(2, 3)})
        reject("missing_csrf", (403, "forbidden"), user={"Cookie": viewer["Cookie"]})
        with browser_session("chrome", viewer, "/monitor", downloads=True) as (call, version):
            def wait(script, label, *args):
                return c.wait_for(lambda: js(call, "return " + script, *args), label)

            wait("document.getElementById('alert-'+arguments[0])", "alert card", alert)
            js(call, "document.getElementById('alert-'+arguments[0]).click()", alert)
            wait("document.querySelector('.ws-export summary')", "export from alert")
            js(call, "document.querySelector('.ws-export summary').click()")
            def capture(name, width):
                call("POST", "/goog/cdp/execute", {"cmd": "Emulation.setDeviceMetricsOverride", "params": {
                    "width": width, "height": 900, "deviceScaleFactor": 1, "mobile": False}})
                shot = call("POST", "/goog/cdp/execute", {"cmd": "Page.captureScreenshot", "params": {
                    "format": "png", "captureBeyondViewport": True}})
                (report / f"{name}-{width}.png").write_bytes(base64.b64decode(shot["data"]))

            def fill(field, value):
                node = wait("document.querySelector('input[name='+arguments[0]+']')", field, field)
                element = "/element/" + node["element-6066-11e4-a52e-4f735466cecf"]
                call("POST", element + "/clear", {})
                call("POST", element + "/value", {"text": value})
            for width in (1440, 768, 390, 320):
                capture("selection", width)
            fill("exportStart", playback.utc(-19))
            js(call, "document.querySelector('.ws-export button').click()")
            wait("document.querySelector('.ws-export [role=alert]')?.textContent.includes('Нет права')", "denied interval")
            capture("denied", 390)
            fill("exportStart", interval["start"])
            fill("exportEnd", interval["end"])
            wait("!document.querySelector('.ws-export [role=alert]')", "previous error cleared on edit")
            js(call, "const button=document.querySelector('.ws-export button');button.focus();button.click()")
            result = wait("document.querySelector('.ws-export a')?.href || document.querySelector('.ws-export [role=alert]')?.textContent", "export result")
            assert result.startswith("blob:"), result
            wait("document.activeElement===document.querySelector('.ws-export a')", "download link focused")
            for width in (1440, 768, 390, 320):
                capture("ready", width)
            js(call, "document.querySelector('.ws-export a').click()")
            files = c.wait_for(lambda: [p for p in call("GET", "/se/files")["names"] if p.endswith(".tar")], "Chrome download")
            assert len(files) == 1, files
            download = call("POST", "/se/files", {"name": files[0]})
            with ZipFile(BytesIO(base64.b64decode(download["contents"]))) as archive:
                (report / "episode.tar").write_bytes(archive.read(files[0]))
            trace = js(call, """return performance.getEntriesByType('resource')
                .filter(r=>r.name.endsWith('/exports')).map(r=>({path:new URL(r.name).pathname,
                status:r.responseStatus, duration_ms:r.duration, bytes:r.decodedBodySize}))""")
            assert any(row["status"] == 200 for row in trace), trace
        with (report / "trusted-key.json").open("wb") as key:
            subprocess.run([c.CORE, "export-public-key"], stdout=key, check=True)
        def replace_grant():
            revoke()
            grant()
        manifest_file = playback.ROOT / "blocks" / blocks[0]["block_id"] / "manifest.json"
        original = manifest_file.read_bytes()
        try:
            altered = {**blocks[0], "device_id": str(uuid.uuid4())}
            manifest_file.write_text(json.dumps(altered))
            reject("changed_source_identity", (503, "media_unavailable"))
        finally:
            manifest_file.write_bytes(original)
        stopped_after = playback.slow_reader(path, viewer, replace_grant, {"interval": playback.interval(5, 6)})
        revoke()
        reject("revoked_grant", (403, "forbidden"))
        grant()
        c.query(f"INSERT INTO bacuri_archive.retired_through VALUES('{c.SITE}',{start+6000000})")
        reject("expired_blocks", (409, "media_unavailable"))
        code, _, page = c.http("POST", f"sites/{c.SITE}/alert-audit/search", {"alert_id": alert}, viewer)
        assert code == 200, page
        audit = [row for row in page["items"] if row["action"] == "export"]
        issued = [row for row in audit if row["outcome"] == "applied"]
        assert len(issued) == 1 and issued[0]["reason"] == "release"
        assert issued[0]["point_id"] == point
        audited_interval = issued[0]["export"]["interval"]
        assert all(datetime.fromisoformat(audited_interval[k]) == datetime.fromisoformat(interval[k]) for k in interval)
        assert any(row["reason"] == "forbidden" for row in audit)
        (report / "browser.json").write_text(json.dumps({
            "chrome": version, "site_id": c.SITE, "point_id": point, "alert_id": alert,
            "origin_us": start, "source_manifests": blocks, "refusals": refusals,
            "grant_replacement_stop_seconds": round(stopped_after, 3), "browser_requests": trace, "audit": audit,
        }, indent=2))

if __name__ == "__main__":
    run()

