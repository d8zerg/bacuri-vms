"""Qualify batch enrollment, recorder acknowledgements and independent channel changes."""

from contextlib import ExitStack
import json
import os
from pathlib import Path
import subprocess
import uuid

from commissioning import (
    CAMERAS,
    SITE,
    browser_session,
    button,
    c,
    capture,
    discover,
    field,
    js,
    profiles,
    report,
    wait,
)
from check import streams
from emulator import serve


def request(method, path, user, body=None, *, status=200):
    code, _, result = c.http(method, path, body, user)
    assert code == status, (method, path, code, result.get("code") if isinstance(result, dict) else None)
    return result


def check_legacy_channel(admin, device, channel, original):
    # Recreate the stored ONVIF identity from an installation predating physical source tokens.
    c.query(f"""
        UPDATE bacuri_inventory.configuration_versions v
        SET settings = settings #- '{{onvif,channel_token}}' #- '{{onvif_control,channel}}'
        FROM bacuri_inventory.configuration_progress p
        WHERE (v.site_id,v.device_id,v.revision)=(p.site_id,p.device_id,p.desired_revision)
          AND p.device_id='{original["id"]}'
    """)
    device.tokens.update({"aux-main": (1280, 720), "aux-sub": (640, 360)})
    device.sources.update({"aux-main": "aux", "aux-sub": "aux"})
    device.stream_paths.update({"aux-main": "main", "aux-sub": "sub"})
    found = request("POST", SITE + "/onvif/discovery", admin)
    ticket = next(row["selection"] for row in found if row["endpoint_id"] == device.serial)
    code, info = profiles(admin, ticket, device.password)
    assert code == 200
    base = SITE + "/camera-batches/" + str(uuid.uuid4()) + "/"
    existing, extra = base + str(uuid.uuid4()), base + str(uuid.uuid4())
    body = {
        "username": "fixture",
        "password": device.password,
        "selection": info["selection"],
        "record_quality": "low",
    }
    old = request("PUT", existing, admin, {**body, "channel": channel, "name": "Migrated input"})
    assert (
        old["current"]["id"] == original["id"] and old["after"]["main_url"] == old["before"]["secondary_url"]
    )
    assert (
        request("PUT", extra, admin, {**body, "channel": "aux", "name": "Additional input"})["before"] is None
    )
    added = request("POST", extra + "/apply", admin)
    assert added["state"] == "saved" and added["current"]["point_id"] != original["point_id"]
    migrated = request("POST", existing + "/apply", admin)
    assert migrated["state"] == "saved" and migrated["current"]["id"] == original["id"]
    assert migrated["current"]["point_id"] == original["point_id"]
    return {"migrated": migrated, "added": added}


def run():
    cert, key = Path(os.environ["BACURI_TLS_CERT"]), Path(os.environ["BACURI_TLS_KEY"])
    for name, role in [("installer", "administrator"), ("viewer", "operator")]:
        subprocess.run([c.CORE, "create-user", name, role], input=c.PASSWORD + "\n", text=True, check=True)
    with ExitStack() as stack:

        def start(program, *args, **kwargs):
            proc = subprocess.Popen([program, *args], **kwargs)
            stack.callback(lambda: c.stop(proc) if proc.poll() is None else None)
            return proc

        core = start(c.CORE, "serve")
        c.wait_for(lambda: c.http("GET", "session")[0] == 401, "Core HTTPS")
        admin, other, viewer = c.login("installer"), c.login("installer"), c.login("viewer")
        assert admin["Cookie"] != other["Cookie"]
        device, _ = stack.enter_context(serve(cert, key, multicast=True))
        stack.enter_context(streams(cert, key, c.BIN / "tests/fixtures"))
        start(c.RECORDER, env={**os.environ, "BACURI_KEY_FILE": str(c.ROOT / "recorder.key")})
        device.model, device.firmware = "Four-input encoder", "2.1"
        device.services = ["Media", "Imaging"]
        channels = ["channel" + str(i) for i in range(1, 5)]
        device.sources = {
            channel + "/" + profile: channel for channel in channels for profile in ("main", "sub")
        }
        device.tokens = {
            token: (1280, 720) if token.endswith("main") else (640, 360) for token in device.sources
        }
        device.brightness = dict.fromkeys(channels[:3], 30)
        with browser_session("chrome", admin, "/cameras", workspace_coverage=report / "chrome") as (
            call,
            version,
        ):
            button(call, "Подключение")
            button(call, "Массовое подключение")
            button(call, "Обнаружить устройства")
            field(call, "batch_user", "fixture")
            field(call, "batch_password", device.password)
            button(call, "Получить каналы")
            wait(call, "document.querySelectorAll('input[type=checkbox]').length===4", "four source tokens")
            screen = lambda w, h: call("POST", "/goog/cdp/execute", {
                "cmd": "Emulation.setDeviceMetricsOverride",
                "params": {"width": w, "height": h, "deviceScaleFactor": 1, "mobile": False},
            })
            screen(390, 844)
            wait(call, "document.querySelectorAll('.ws-draft-row').length===4", "four compact channel rows")
            js(call, "document.querySelector('.ws-camera-batch').scrollIntoView({block:'start'})")
            assert js(call, """const rows=[...document.querySelectorAll('.ws-draft-row')];
                return !rows.some(r=>r.innerText.includes('urn:uuid:')) &&
                  rows[3].getBoundingClientRect().bottom - rows[0].getBoundingClientRect().top < innerHeight""")
            capture(call, "chrome", "channel-selection-mobile")
            screen(1280, 844)
            js(call, "[...document.querySelectorAll('summary')].find(s=>s.textContent==='Настройки потоков и яркости').click()")
            js(
                call,
                "[...document.querySelectorAll('summary')].find(s=>s.textContent.includes('Сохранить настройки')).click()",
            )
            field(call, "batch_template_name", "Four entrances")
            button(call, "Сохранить шаблон")
            wait(call, "document.body.innerText.includes('Шаблон сохранён.')", "site template persisted")
            button(call, "Сравнить конфигурации")
            wait(
                call,
                "document.querySelectorAll('.ws-batch-receipts .ws-batch-channel').length===4 && "
                "document.activeElement === document.querySelector('.ws-batch-toolbar h2')",
                "four previews",
            )
            assert js(
                call,
                """const list=document.querySelector('.ws-batch-results');
                return list.querySelectorAll('.ws-batch-device').length===1 &&
                  !list.innerText.includes('rtsps:') && !list.innerText.includes('urn:uuid:') &&
                  [...list.querySelectorAll('summary strong')].map(n=>n.innerText).join()===
                    'Канал 1,Канал 2,Канал 3,Канал 4'""",
            )
            js(call, "window.scrollTo(0,0)")
            capture(call, "chrome", "channel-preview")
            button(call, "Применить партию")
            wait(
                call,
                "document.querySelectorAll('.ws-batch-receipts .ws-batch-channel').length===4 && "
                "[...document.querySelectorAll('.ws-batch-receipts .ws-batch-channel summary')].every(s=>"
                "/Ожидает рекордера|Применено рекордером/.test(s.innerText))",
                "four saved results",
            )
            batch_id = js(
                call, "return sessionStorage.getItem(arguments[0])", "bacuri.camera-batch." + c.SITE
            )
            path = SITE + "/camera-batches/" + batch_id

            def applied():
                rows = request("GET", path, admin)
                return len(rows) == 4 and all(r["current"]["applied_revision"] == "1" for r in rows)

            c.wait_for(applied, "recorder acknowledged all four channels")
            button(call, "Обновить результаты")
            wait(
                call,
                "[...document.querySelectorAll('.ws-batch-receipts .ws-batch-channel summary')].every(s=>s.innerText.includes('Применено рекордером'))",
                "four actual versions",
            )
            js(call, "window.scrollTo(0,0)")
            capture(call, "chrome", "channel-results")
            button(call, "Каталог подключённых устройств")
            wait(call, "document.querySelectorAll('.ws-batch-device').length===1 && document.querySelectorAll('.ws-batch-channel').length===4", "four connected channels in catalog")
            assert not js(call, "return document.querySelector('.ws-batch-results').innerText.includes('urn:uuid:')")
            capture(call, "chrome", "channel-inventory")
            button(call, "Подключение")
            button(call, "Массовое подключение")
            wait(call, "document.querySelectorAll('.ws-batch-receipts .ws-batch-channel').length===4", "reopened batch")
            screen(390, 844)
            assert js(call, "return document.documentElement.scrollWidth <= window.innerWidth && window.innerWidth === 390")
            js(call, "window.scrollTo(0,0)")
            capture(call, "chrome", "channel-results-mobile")
        rows = request("GET", path, admin)
        inventory = {r["camera"]["channel_token"]: r for r in rows}
        templates = request("GET", SITE + "/camera-templates", other)
        (template,) = templates
        template_path = SITE + "/camera-templates/" + template["id"]
        settings = {"name": template["name"], "record_quality": "high", "expected_revision": "0"}
        assert request("PUT", template_path, admin, settings) == template
        request("PUT", template_path, viewer, settings, status=403)
        assert all(r["template"] == template for r in rows)
        assert any(
            r["id"] == batch_id and r["count"] == 4 for r in request("GET", SITE + "/camera-batches", admin)
        )
        assert set(inventory) == set(channels) and len({r["current"]["point_id"] for r in rows}) == 4
        assert {r["camera"]["endpoint_id"] for r in rows} == {device.serial}
        c.wait_for(
            lambda: c.query("SELECT count(DISTINCT point_id) FROM bacuri_archive.blocks") == "4",
            "recording at every point",
        )
        request("GET", path, viewer, status=403)
        info_status, info = profiles(admin, discover(admin), device.password)
        assert info_status == 200
        assert len(info["profiles"]) == 8
        new_path = SITE + "/camera-batches/" + str(uuid.uuid4())
        updated_template = request(
            "PUT",
            template_path,
            other,
            {**settings, "record_quality": "low", "expected_revision": "1", "brightness": 65},
        )
        assert updated_template["revision"] == "2"
        request("PUT", template_path, admin, {**settings, "name": "Outdated edit"}, status=409)
        drafts, changes = {}, {}
        for source, enrolled in inventory.items():
            rid = str(uuid.uuid4())
            resource = new_path + "/" + rid
            before = enrolled["current"]
            body = dict(
                name="Updated " + source,
                username="fixture",
                password=device.password,
                selection=info["selection"],
                main_url=before["secondary_url"],
                secondary_url=before["main_url"],
            )
            if source in device.brightness:
                body.pop("main_url")
                body.pop("secondary_url")
                body.update(channel=source, template_id=template["id"], template_revision="2")
                request("PUT", resource, admin, {**body, "template_revision": "1"}, status=409)
            else:
                request("PUT", resource, admin, {**body, "brightness": 65}, status=422)
            if source == channels[0]:
                request(
                    "PUT",
                    resource,
                    admin,
                    dict(
                        name=body["name"],
                        username="fixture",
                        password=device.password,
                        selection=info["selection"],
                        main_url=before["main_url"],
                        secondary_url=inventory[channels[1]]["current"]["secondary_url"],
                    ),
                    status=422,
                )
            row = request("PUT", resource, admin, body)
            assert row["expected_revision"] == "1" and row["before"]["main_url"] != row["after"]["main_url"]
            assert request("PUT", resource, admin, body) == row
            request("PUT", resource, admin, {**body, "name": "Other"}, status=409)
            drafts[source] = body
            changes[source] = resource
        peer, _ = stack.enter_context(serve(cert, key))
        peer.model, peer.firmware, peer.services = "Fixed camera", "3.0", ["Media"]
        device.discoverable_peers.append(peer)
        found = request("POST", SITE + "/onvif/discovery", admin)
        token = next(d["selection"] for d in found if d["endpoint_id"] == peer.serial)
        code, standalone = profiles(admin, token, peer.password)
        assert code == 200
        peer_item = new_path + "/" + str(uuid.uuid4())
        peer_body = {
            "selection": standalone["selection"],
            "name": "Separate camera",
            "channel": "main",
            "record_quality": "high",
            "username": "fixture",
            "password": peer.password,
        }
        assert request("PUT", peer_item, admin, peer_body)["state"] == "prepared"

        def action(source, operation="apply"):
            return request("POST", changes[source] + "/" + operation, admin)

        first, second, offline, concurrent = channels
        assert action(first)["revision"] == "2"
        peer.online = False
        disconnected_peer = request("POST", peer_item + "/apply", admin)
        assert disconnected_peer["state"] == "failed" and not disconnected_peer["current"]
        c.stop(core)
        start(c.CORE, "serve")
        c.wait_for(lambda: c.http("GET", "session")[0] == 401, "Core restart with persisted batch")
        peer.online = True
        imported_peer = request("POST", peer_item + "/apply", admin)
        assert imported_peer["revision"] == "1"
        peer_point = imported_peer["current"]["point_id"]
        c.wait_for(
            lambda: c.query(f"SELECT count(*)>0 FROM bacuri_archive.blocks WHERE point_id='{peer_point}'")
            == "t",
            "standalone recording",
        )
        device.drop_image_reply = True
        assert action(second)["state"] == "failed" and device.brightness[second] == 65
        assert action(second)["revision"] == "2" and device.image_writes.count((second, 65)) == 1
        device.disconnected.add(offline)
        failed = action(offline)
        assert failed["state"] == "failed" and failed["current"]["desired_revision"] == "1"
        camera_path = CAMERAS + "/" + inventory[concurrent]["device_id"]
        _, headers, camera = c.http("GET", camera_path, headers=other)
        edit = {k: camera[k] for k in ("main_url", "secondary_url")}
        edit["name"] = "Independent edit"
        request("PUT", camera_path, {**other, "If-Match": headers["ETag"]}, edit)
        conflict = action(concurrent)
        assert conflict["state"] == "failed" and conflict["error"] == "revision_conflict"
        assert conflict["current"]["name"] == edit["name"]
        device.disconnected.clear()
        assert action(offline)["revision"] == "2"
        assert action(concurrent)["error"] == "revision_conflict"
        assert action(first)["revision"] == "2" and device.image_writes.count((first, 65)) == 1
        rebased = new_path + "/" + str(uuid.uuid4())
        assert request("PUT", rebased, admin, drafts[concurrent])["expected_revision"] == "2"
        assert request("POST", rebased + "/apply", admin)["revision"] == "3"
        assert action(first, "rollback")["revision"] == "3" and device.brightness[first] == 30
        assert action(first, "rollback")["revision"] == "3" and device.image_writes.count((first, 30)) == 1
        assert action(concurrent, "rollback")["error"] == "revision_conflict"
        c.wait_for(
            lambda: all(
                r["current"]["applied_revision"] == r["current"]["desired_revision"]
                for r in request("GET", new_path, admin)
            ),
            "recorder after retry and rollback",
        )
        actual = request("GET", new_path, admin)
        assert device.password not in json.dumps(actual)
        assert {r["current"]["point_id"] for r in actual} == {r["current"]["point_id"] for r in rows} | {
            peer_point
        }
        assert request("POST", peer_item + "/rollback", admin)["current"] is None
        assert request("POST", peer_item + "/apply", admin)["state"] == "rolled_back"
        assert c.query(f"SELECT count(*)>0 FROM bacuri_archive.blocks WHERE point_id='{peer_point}'") == "t"
        original = next(r for r in rows if r["camera"]["channel_token"] == concurrent)
        assert (
            request("POST", path + "/" + original["item_id"] + "/rollback", admin)["error"]
            == "revision_conflict"
        )
        assert request("POST", rebased + "/rollback", admin)["revision"] == "4"
        legacy = check_legacy_channel(admin, device, first, inventory[first]["current"])
        snapshot = subprocess.check_output([c.CORE, "backup-config"])
        restored = "bacuri_batch_" + uuid.uuid4().hex
        subprocess.run(["createdb", "--template=template0", restored], check=True)
        try:
            env = {**os.environ, "PGDATABASE": restored}
            subprocess.run([c.CORE, "migrate"], env=env, check=True)
            subprocess.run([c.CORE, "restore-config"], input=snapshot, env=env, check=True)
            contents = """
                SELECT
                    (SELECT jsonb_agg(to_jsonb(i) ORDER BY site_id, user_id, batch_id, item_id)
                     FROM bacuri_inventory.camera_batch_items i),
                    (SELECT jsonb_agg(to_jsonb(t) ORDER BY site_id, template_id, revision)
                     FROM bacuri_inventory.camera_templates t)
            """
            restored_rows = subprocess.check_output(["psql", "-XAtqc", contents], env=env, text=True).strip()
            assert restored_rows == c.query(contents), "Restored batch items or template revisions differ"
        finally:
            subprocess.run(["dropdb", restored], check=True)
        (report / "batch.json").write_text(
            json.dumps(
                {
                    "browser": version,
                    "input": "synthetic ONVIF and RTSPS",
                    "restored_batch_and_templates": True,
                    "legacy_channels": legacy,
                    "enrollment": rows,
                    "results": actual,
                    "disconnected_camera": disconnected_peer,
                    "disconnected": failed,
                    "concurrent": conflict,
                    "observed_image_writes": device.image_writes,
                },
                indent=2,
            )
            + "\n"
        )
        print(
            "Four ONVIF channels recorded. Partial failure, lost acknowledgement, concurrent edit, guarded retry and rollback passed.",
            flush=True,
        )


if __name__ == "__main__":
    run()
