"""Exercise local map packages and point-level access through the delivered workspace."""

import base64
import hashlib
import io
import json
import os
import sqlite3
import subprocess
import zipfile
import struct
import zlib
from pathlib import Path

from feed import browser_session, c, js

BASE = f"sites/{c.SITE}/maps"
FILE = Path("web/client/stories/floor.png")


def install(version, broken=False):
    root = Path("tests/maps")
    meta = json.loads((root / f"{version}.json").read_text())
    path = c.ROOT / (version + ".mbtiles")
    with sqlite3.connect(path) as db:
        db.executescript(
            "DROP TABLE IF EXISTS metadata; DROP TABLE IF EXISTS tiles;"
            "CREATE TABLE metadata(name text,value text);"
            "CREATE TABLE tiles(zoom_level integer,tile_column integer,tile_row integer,tile_data blob)"
        )
        db.executemany(
            "INSERT INTO metadata VALUES (?,?)",
            [(key, meta[key]) for key in ("format", "bounds", "name", "osm_base_timestamp")],
        )
        db.execute(
            "INSERT INTO tiles VALUES (?,?,?,?)",
            (
                meta["z"],
                meta["x"],
                2 ** meta["z"] - 1 - meta["y"],
                b"invalid" if broken else (root / f"{version}.png").read_bytes(),
            ),
        )
    result = subprocess.run([c.CORE, "import-map", path], check=False)
    assert (result.returncode != 0) == broken
    return meta["osm_base_timestamp"]


def click(call, node):
    call("POST", "/element/" + node["element-6066-11e4-a52e-4f735466cecf"] + "/click", {})


def link(call, wait, selector):
    click(call, wait(call, "document.querySelector(arguments[0])", selector, selector))


def admin_placements(browser, admin, viewer, cam, wait, button, field, raw):
    point = cam["point_id"]
    previous = install("previous")
    placement = {"point_id": point, "plan_id": None, "x": 7.41, "y": 43.73}
    assert c.http("PUT", BASE + "/placement", placement, viewer)[0] == 403
    assert c.http("PUT", BASE + "/placement", placement, {**admin, "X-CSRF-Token": ""})[0] == 403

    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))

    width = height = 3000
    large_png = (
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 0, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(bytes((width + 1) * height)))
        + chunk(b"IEND", b"")
    )
    for content in (b"not a PNG", FILE.read_bytes()[:48], bytes(4 * 1024 * 1024 + 1), large_png):
        status = c.http(
            "POST", BASE + "/plans", {"name": "Invalid", "png": base64.b64encode(content).decode()}, admin
        )[0]
        assert status == 400, status
    with browser_session(
        browser, admin, "/maps?point=" + point, workspace_coverage=raw, software_map=True
    ) as (call, _):
        wait(
            call,
            "document.querySelector('main time')?.dateTime===arguments[0]",
            "regional source timestamp",
            previous,
        )
        wait(
            call,
            "performance.getEntriesByType('resource').some(e=>e.name.includes('/tiles/') && e.responseEnd>0)",
            "local raster tile",
            timeout=15,
        )
        wait(
            call,
            "document.querySelector('.maplibregl-canvas') && !document.body.innerText.includes('Не удалось отобразить')",
            "local MapLibre canvas",
        )
        js(call, "document.querySelector('.ws-placement-form').closest('details').querySelector('summary').click()")
        for label, value in (("Долгота", "7.41"), ("Широта", "43.73")):
            node = wait(
                call,
                "[...document.querySelectorAll('label')].find(e=>e.textContent===arguments[0])?.querySelector('input')",
                label,
                label,
            )
            call(
                "POST", "/element/" + node["element-6066-11e4-a52e-4f735466cecf"] + "/value", {"text": value}
            )
        button(call, "Сохранить положение")
        wait(call, "document.body.innerText.includes('Положение сохранено')", "saved geographic position")
        if "id" in cam:
            calibration = {"point_id": point, "plan_id": None, "device_id": cam["id"], "bearing": 75, "hfov": 68}
            assert c.http("PUT", BASE + "/calibration", {**calibration, "hfov": 0}, admin)[0] == 400
            assert c.http("PUT", BASE + "/calibration", calibration, admin)[0] == 204
            wait(call, "document.querySelector('.maplibregl-marker .ws-sector')?.dataset.state==='fixed'", "geographic camera sector")
        js(call, "[...document.querySelectorAll('summary')].find(s=>s.textContent==='Добавить план').click()")
        field(call, "name", "План " + browser)
        payload = io.BytesIO()
        with zipfile.ZipFile(payload, "w") as archive:
            archive.writestr("floor.png", FILE.read_bytes())
        remote = call("POST", "/se/file", {"file": base64.b64encode(payload.getvalue()).decode()})
        node = wait(call, "document.querySelector('input[type=file]')", "PNG input")
        call("POST", "/element/" + node["element-6066-11e4-a52e-4f735466cecf"] + "/value", {"text": remote})
        button(call, "Загрузить план")
        image = wait(
            call,
            "document.querySelector('.ws-plan img')?.naturalWidth===640 && document.querySelector('.ws-plan img')",
            "decoded customer plan",
        )
        js(call, "document.querySelector('.ws-placement-form').closest('details').querySelector('summary').click()")
        click(call, image)
        button(call, "Сохранить положение")
        wait(call, "document.body.innerText.includes('Положение сохранено')", "saved floor position")
        if "id" in cam:
            plan_id = next(p["id"] for p in c.http("GET", BASE, headers=admin)[2]["plans"] if p["name"] == "План " + browser)
            assert c.http("PUT", BASE + "/calibration", {**calibration,"plan_id":plan_id,"bearing":20},admin)[0] == 204
            wait(call,"document.querySelector('.ws-plan .ws-sector')?.dataset.state==='fixed'","fixed camera sector on plan")
        external = js(
            call,
            "return performance.getEntriesByType('resource').filter(e=>/^https?:/.test(e.name)&&new URL(e.name).origin!==location.origin).map(e=>e.name)",
        )
        assert not external, external
    before = c.http("GET", BASE, headers=admin)[2]
    plan = next(p for p in before["plans"] if p["name"] == "План " + browser)
    assert any(p["plan_id"] == plan["id"] and p["point_id"] == point for p in before["placements"])
    assert any(all(row.get(k) == v for k, v in placement.items()) for row in before["placements"])
    denied = c.http("GET", BASE, headers=viewer)[2]
    assert all(p["point_id"] != point for p in denied["placements"]) and plan not in denied["plans"]
    assert c.http("GET", BASE + "/plans/" + plan["id"], headers=viewer)[0] == 404
    install("current", broken=True)
    assert c.http("GET", BASE, headers=admin)[2] == before
    stamp = install("current")
    after = c.http("GET", BASE, headers=admin)[2]
    assert after["region"]["source_time"] == stamp and stamp > previous
    assert (
        after["region"]["revision"] != before["region"]["revision"]
        and before["placements"] == after["placements"]
    )
    assert c.http("GET", before["region"]["style_url"].removeprefix("/api/v1/"), headers=viewer)[0] == 404
    return plan["id"]



def georeference(browser, admin, viewer, plan, point, wait, button, raw):
    import math

    endpoint = BASE + "/georeference"
    a = {"x": 0, "y": 1, "lng": 7.40, "lat": 43.73}
    b = {"x": 1, "y": 1, "lng": 7.40, "lat": 43.729}
    pair = {"anchor": a, "second": b, "building": "Корпус"}
    request = {"plan_id": plan, "definition": pair}

    def inspect(body):
        code, _, result = c.http("POST", endpoint + "/preview", body, admin)
        assert code == 200, result
        return result

    def apply(body, preview, choices):
        return c.http("PUT", endpoint, {**body, "token": preview["token"], "choices": choices}, admin)[0]

    def geographic():
        rows = [p for p in c.http("GET", BASE, headers=admin)[2]["placements"]
                if p["plan_id"] is None and p["point_id"] == point]
        assert len(rows) == 1
        return rows[0]

    assert c.http("POST", endpoint + "/preview", request, viewer)[0] == 403
    assert c.http("PUT", endpoint, request, {**admin, "X-CSRF-Token": ""})[0] == 403
    assert c.http("POST", endpoint + "/preview",
                  {**request, "definition": {"anchor": a, "second": a}}, admin)[0] == 400
    preview = inspect(request)
    footprint = preview["footprint"]
    assert abs(footprint["bearing"] - 90) < 1e-8
    assert abs(footprint["width_m"] - 111.32) < .03
    for actual, expected in zip(footprint["corners"][2:], [b, a], strict=True):
        assert abs(actual[0] - expected["lng"]) < 1e-8 and abs(actual[1] - expected["lat"]) < 1e-8
    assert preview["cameras"][0]["distance_m"] > 10
    manual = geographic()
    assert apply(request, preview, {}) == 400
    assert apply(request, preview, {point: "keep"}) == 204 and geographic() == manual
    assert apply(request, preview, {point: "plan"}) == 409
    assert apply(request, inspect(request), {point: "plan"}) == 204
    geo = geographic()
    assert geo["source_plan_id"] == plan and abs(geo["view"]["bearing"] - 110) < 1e-8
    assert "definition" not in str(c.http("GET", BASE, headers=viewer)[2]["plans"])
    assert c.http("PUT", BASE + "/placement", {"point_id": point, "plan_id": None, "x": 0, "y": 0}, admin)[0] == 409
    single = {"plan_id": plan, "definition": {"anchor": a, "width_m": footprint["width_m"], "bearing": 90}}
    second = inspect(single)
    assert all(math.dist(x,y)<1e-8 for x,y in zip(second["footprint"]["corners"],footprint["corners"],strict=True))
    assert apply(single, second, {point: "plan"}) == 204
    moved = {"point_id": point, "plan_id": plan, "x": 0, "y": 1}
    assert c.http("PUT", BASE + "/placement", moved, admin)[0] == 204
    assert math.dist([geographic()["x"],geographic()["y"]], [a["lng"],a["lat"]]) < 1e-9
    assert apply(single, second, {point: "plan"}) == 409
    unlink = {"plan_id": plan, "definition": None}
    assert apply(unlink, inspect(unlink), {}) == 204 and geographic() == manual

    with browser_session(browser, admin, "/maps?plan=" + plan, workspace_coverage=raw, software_map=True) as (call, _):
        button(call, "Привязать план к карте")
        wait(call, "document.querySelector('.ws-reference .maplibregl-canvas')", "local reference map")
        # Select two different plan and map landmarks without knowing a bearing or scale.
        for x, y in ((.24, .32), (.76, .65)):
            for selector in (".ws-reference .ws-plan img", ".ws-reference .maplibregl-canvas"):
                js(call, """const e=document.querySelector(arguments[0]), r=e.getBoundingClientRect();
                   e.dispatchEvent(new MouseEvent('mousedown',{bubbles:true,clientX:r.left+r.width*arguments[1],clientY:r.top+r.height*arguments[2]}));
                   e.dispatchEvent(new MouseEvent('mouseup',{bubbles:true,clientX:r.left+r.width*arguments[1],clientY:r.top+r.height*arguments[2]}));
                   e.dispatchEvent(new MouseEvent('click',{bubbles:true,clientX:r.left+r.width*arguments[1],clientY:r.top+r.height*arguments[2]}));""", selector, x, y)
        button(call, "Предпросмотр")
        wait(call, "document.body.innerText.includes('Проверка привязки')", "paired landmark preview")
        assert js(call, "return [...document.querySelectorAll('button')].find(b=>b.textContent==='Сохранить привязку').disabled")
        js(call, """const s=document.querySelector('option[value=keep]').parentElement;
            s.value='plan';s.dispatchEvent(new Event('change',{bubbles:true}));""")
        button(call, "Сохранить привязку")
        button(call, "Показать на геокарте")
        wait(call, "document.querySelector('.maplibregl-marker .ws-sector')", "projected geographic camera")
        wait(call, "document.querySelector('input[type=range]')", "floor image opacity")
    # Keep a linked floor and its source choice in the backup and across a regional update.
    assert apply(request, inspect(request), {point: "plan"}) == 204
    geo = geographic()
    install("current")
    assert geographic() == geo

    # Competing floors retain one geographic source, including a camera added after linking.
    second_plan = c.http("POST", BASE + "/plans",
                         {"name": "Второй этаж " + browser, "png": base64.b64encode(FILE.read_bytes()).decode()}, admin)[2]["id"]
    upper = {**request, "plan_id": second_plan}
    assert apply(upper, inspect(upper), {}) == 204
    assert c.http("PUT", BASE + "/placement", {**moved, "plan_id": second_plan, "x": .75}, admin)[0] == 204
    assert geographic() == geo
    competing = inspect(upper)
    assert competing["cameras"][0]["existing_plan_id"] == plan
    assert apply(upper, competing, {point: "plan"}) == 204
    assert geographic()["source_plan_id"] == second_plan
    assert apply(request, inspect(request), {point: "plan"}) == 204
    assert geographic() == geo


def verify_restore():
    database = os.environ["PGDATABASE"] + "_restore"
    environment = {**os.environ, "PGDATABASE": database}
    backup = subprocess.check_output([c.CORE, "backup-config"])
    subprocess.run(["createdb", "--template=template0", database], check=True)
    try:
        subprocess.run([c.CORE, "migrate"], env=environment, check=True)
        subprocess.run([c.CORE, "restore-config"], env=environment, input=backup, check=True)
        queries = (
            "SELECT site_id,url FROM bacuri_maps.map_catalog_settings ORDER BY site_id",
            "SELECT plan_id,name,width,height,md5(image),georeference FROM bacuri_maps.floor_plans ORDER BY plan_id",
            "SELECT site_id,point_id,plan_id,x,y,calibration,geo_primary FROM bacuri_maps.point_placements ORDER BY point_id,plan_id",
        )
        for query in queries:
            actual = subprocess.check_output(
                ["psql", "-XAtq", "-v", "ON_ERROR_STOP=1", "-c", query], env=environment, text=True
            ).strip()
            assert actual and actual == c.query(query)
    finally:
        subprocess.run(["dropdb", database], check=True)


def offline_maps(wait, button, field, raw, capture):
    from uuid import uuid4

    for name, role in (("installer", "administrator"), ("viewer", "operator")):
        subprocess.run([c.CORE, "create-user", name, role], input=c.PASSWORD + "\n", text=True, check=True)
    with subprocess.Popen([c.CORE, "serve"]) as core:
        try:
            c.wait_for(lambda: c.http("GET", "session")[0] == 401, "offline workspace")
            admin, viewer = c.login("installer"), c.login("viewer")
            for browser in ("chrome", "firefox"):
                host = os.environ["BACURI_TEST_CAMERA_HOST"]
                body = {"name": "Offline " + browser, "main_url": f"rtsp://{host}:8554/main", "secondary_url": f"rtsp://{host}:8554/sub"}
                code, _, cam = c.http("POST", f"sites/{c.SITE}/cameras", body, {**admin, "Idempotency-Key": str(uuid4())})
                assert code == 201, cam
                point = cam["point_id"]
                plan = admin_placements(browser, admin, viewer, cam, wait, button, field, raw)
                georeference(browser, admin, viewer, plan, point, wait, button, raw)
                subprocess.run([c.CORE, "grant-point", "viewer", point], check=True)
                with browser_session(
                    browser, viewer, "/maps?point=" + point, workspace_coverage=raw, software_map=True
                ) as (call, _):
                    wait(
                        call,
                        "performance.getEntriesByType('resource').some(r=>r.name.includes('/tiles/'))",
                        "offline OSM",
                    )
                    wait(call, "document.querySelector('.maplibregl-marker')", "authorized geographic marker")
                    region = c.http("GET", BASE, headers=viewer)[2]
                    current = json.loads(Path("tests/maps/current.json").read_text())
                    local_assets = [*region["fonts"], region["symbols"]]
                    result = call(
                        "POST",
                        "/execute/async",
                        {
                            "script": """
                        const [styleUrl,z,x,y,assets,done]=arguments;
                        (async()=>{
                            const style=await (await fetch(styleUrl)).json();
                            const tile=style.sources.region.tiles[0].replace('{z}',z).replace('{x}',x).replace('{y}',y);
                            const image=await fetch(tile);
                            const hash=await crypto.subtle.digest('SHA-256', await image.arrayBuffer());
                            const statuses=await Promise.all(assets.map(async url=>(await fetch(url)).status));
                            done([image.status,image.headers.get('content-type'),Array.from(new Uint8Array(hash),v=>v.toString(16).padStart(2,'0')).join(''),statuses]);
                        })().catch(e=>done(String(e)))
                    """,
                            "args": [
                                region["region"]["style_url"],
                                current["z"],
                                current["x"],
                                current["y"],
                                local_assets,
                            ],
                        },
                    )
                    assert result == [
                        200,
                        "image/png",
                        hashlib.sha256(Path("tests/maps/current.png").read_bytes()).hexdigest(),
                        [200] * len(local_assets),
                    ], result
                    link(call, wait, ".maplibregl-ctrl-zoom-in")
                    wait(call, "new URLSearchParams(location.search).has('z')", "saved map viewport")
                    saved_view = js(
                        call,
                        "const q=new URLSearchParams(location.search);return [q.get('lng'),q.get('lat'),q.get('z')]",
                    )
                    button(call, "Архив")
                    link(call, wait, 'nav a[href^="/maps"]')
                    wait(
                        call,
                        "document.activeElement?.id==='map-point-'+arguments[0]",
                        "regional point focus",
                        point,
                    )
                    wait(call, "document.querySelector('.maplibregl-marker')", "restored map")
                    assert saved_view == js(
                        call,
                        "const q=new URLSearchParams(location.search);return [q.get('lng'),q.get('lat'),q.get('z')]",
                    )
                    capture(call, browser, "region")
                    button(call, "Планы")
                    wait(call, "document.querySelector('.ws-plan img')?.naturalWidth===640", "offline plan")
                    capture(call, browser, "plan")
                    c.query(f"DELETE FROM bacuri_access.point_grants WHERE point_id='{point}'")
                    wait(
                        call,
                        "!document.querySelector('.ws-plan img') && !document.getElementById('map-point-'+arguments[0])",
                        "revoked offline plan",
                        point,
                        timeout=6,
                    )
                print(browser, "offline map import, update, placement and revocation passed", flush=True)
        finally:
            c.stop(core)
    from map_packages import qualify
    qualify(wait, button, raw)
    verify_restore()
