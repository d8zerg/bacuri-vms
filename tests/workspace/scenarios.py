"""Check the delivered HTTPS workspaces against independent recorder and frame evidence."""

import base64
from contextlib import ExitStack
import json
import os
from pathlib import Path
import sqlite3
import subprocess
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "alerts"))
from feed import c, browser_session, js, motion, playback, send, snapshot
from maps import admin_placements, link, offline_maps

report = Path(os.environ["BACURI_WORKSPACE_RESULTS"])
raw = report / "chrome"
raw.mkdir(parents=True, exist_ok=True)
buttons = "[...document.querySelectorAll('button')].find(b => b.textContent === arguments[0] || b.getAttribute('aria-label') === arguments[0])"


def wait(call, condition, label, *args, timeout=45):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        value = js(call, "return " + condition, *args)
        if value:
            return value
        time.sleep(0.08)
    raise AssertionError((label, js(call, "return document.body.innerText")))


def button(call, label):
    node = wait(call, buttons + " || null", label, label)
    call("POST", "/element/" + node["element-6066-11e4-a52e-4f735466cecf"] + "/click", {})


def field(call, name, value):
    element = wait(call, "document.querySelector('input[name='+arguments[0]+']:enabled')", name, name)
    call("POST", "/element/" + element["element-6066-11e4-a52e-4f735466cecf"] + "/value", {"text": value})


def login(call, name):
    field(call, "username", name)
    field(call, "password", c.PASSWORD)
    button(call, "Войти")


def capture(call, browser, name):
    (report / (browser + "-" + name + ".png")).write_bytes(base64.b64decode(call("GET", "/screenshot")))


def action_cost(browser, phase, credential, route):
    samples = []
    for _ in range(3):
        with browser_session(browser, credential, route) as (call, version):
            ms = wait(
                call, "performance.getEntriesByName('workspace-action')[0]?.startTime", "action ready"
            )
            external = js(
                call,
                """return performance.getEntriesByType('resource')
                .filter(r=>/^https?:/.test(r.name) && new URL(r.name).origin!==location.origin).map(r=>r.name)""",
            )
            assert not external, external
            resources = js(
                call,
                """return performance.getEntriesByType('resource')
                .filter(r=>/\\.(js|css)$/.test(new URL(r.name).pathname))
                .map(r=>[new URL(r.name).pathname, r.decodedBodySize])""",
            )
            assert resources and all(size > 0 for _, size in resources), resources
            samples.append(
                {"ms": round(ms), "bytes": sum(size for _, size in resources), "resources": resources}
            )
    return {"browser": browser, "version": version, "phase": phase, "route": route, "samples": samples}


def installer(browser, admin, restart):
    with browser_session(browser, c.login("installer"), "/cameras", workspace_coverage=raw) as (call, _):
        button(call, "Подключение")
        camera_name = "Workspace " + browser
        field(call, "name", camera_name)
        base = "rtsp://" + os.environ["BACURI_TEST_CAMERA_HOST"] + ":8554"
        field(call, "main_url", base + "/main")
        field(call, "secondary_url", base + "/sub")
        button(call, "Подключить камеру")
        wait(call, buttons, "both profiles confirmed", "Создать тестовую запись", timeout=65)
        code, _, cams = c.http("GET", f"sites/{c.SITE}/cameras", headers=admin)
        matches = [cam for cam in cams if cam["name"] == camera_name]
        assert code == 200 and len(matches) == 1, matches
        cam = matches[0]
        assert cam["desired_revision"] == cam["applied_revision"] == "1"
        assert "Конфигурация применена · версия 1" in js(call, "return document.body.innerText")
        assert not js(call, "return !!" + buttons, "Подтвердить запись")
        button(call, "Создать тестовую запись")
        button(call, "Открыть запись")
        button(call, "Подтвердить запись")
        wait(call, "document.body.innerText.includes('Запись подтверждена')", "confirmed decoded footage")
        dims = js(
            call, "const v=document.querySelector('video');return [v.videoWidth,v.videoHeight,v.readyState]"
        )
        assert dims[0] > 640 and dims[2] >= 2, dims
        capture(call, browser, "installer")
        restart(
            lambda: wait(
                call,
                "document.querySelector('fieldset')?.disabled && !document.body.innerText.includes('Запись подтверждена')",
                "offline installer",
            )
        )
        wait(call, "document.querySelector('fieldset')?.disabled===false", "installer reconnected")
        assert js(call, "return document.querySelector('input[name=name]').value") == camera_name
        button(call, "Выйти")
        wait(call, "document.querySelector('input[name=username]')", "logged out")
        print(browser, "installer applied revision", cam["applied_revision"], "decoded", dims, flush=True)
        return cam


def access_settings(browser, admin):
    with browser_session(browser, admin, "/access", workspace_coverage=raw) as (call, _):
        wait(call, "document.querySelectorAll('.ws-users summary').length >= 2", "user list")
        assert not js(call, "return !!document.querySelector('.ws-users details[open]')")
        button(call, "Группы")
        name = "Workspace group " + browser
        field(call, "name", name)
        button(call, "Сохранить группу")
        wait(call, "Array.from(document.querySelectorAll('main select option')).some(option=>option.textContent===arguments[0])",
             "saved group", name)
        for tab, label in [("Разрешения", "Действие"), ("Приватность", "Области маски"),
                           ("Вход", "OpenID Connect"), ("Аудит", "Создана группа")]:
            button(call, tab)
            wait(call, "document.querySelector('main').innerText.includes(arguments[0])", tab, label)

        assert not js(call, "return document.querySelector('.ws-access-audit').innerText.includes('resource_groups.insert')")
        js(call, """const summary=[...document.querySelectorAll('.ws-access-audit summary')].find(s=>s.innerText.includes(arguments[0]));
            summary.click();""", name)
        wait(call, "document.querySelector('.ws-access-audit details[open]').innerText.includes('resource_groups.insert')", "audit source action")


def pending_alert(call, point, viewer, on_map):
    with subprocess.Popen(
        ["psql", "-XAtq", "-v", "ON_ERROR_STOP=1"], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True
    ) as lock:
        lock.stdin.write("BEGIN; LOCK bacuri_archive.blocks IN SHARE MODE; SELECT 'locked';\n")
        lock.stdin.flush()
        assert lock.stdout.readline().strip() == "locked"
        try:

            def pending():
                with sqlite3.connect(playback.ROOT / "journal.db") as db:
                    return db.execute(
                        """SELECT manifest_json->>'begin_utc_us',manifest_json->>'end_utc_us'
                        FROM committed_blocks WHERE registered_at_utc_us IS NULL AND manifest_json->>'point_id'=?
                        AND manifest_json->>'$.time.basis'='source_clock' LIMIT 1""",
                        (point,),
                    ).fetchone()

            begin, end = c.wait_for(pending, "new local archive block")
            send(motion(point, (begin + end) // 2 - playback.ORIGIN_US))
            item = snapshot(viewer)["items"][-1]
            if on_map:
                link(call, wait, 'nav a[href^="/maps"]')
                link(call, wait, "#map-alert-" + item["id"])
            else:
                wait(call, "document.getElementById('alert-'+arguments[0])", "pending alert", item["id"])
                js(call, "document.getElementById('alert-'+arguments[0]).click()", item["id"])
            wait(
                call,
                "document.querySelector('.ws-overlay')?.innerText.includes('добавляется в каталог')",
                "pending catalog",
            )
            assert not js(call, "return document.querySelector('video').videoWidth")
        finally:
            lock.stdin.write("COMMIT;\n\\q\n")
            lock.stdin.flush()
    c.wait_for(
        lambda: c.http("GET", f"sites/{c.SITE}/alerts/{item['id']}/archive", headers=viewer)[2]["state"]
        == "ready",
        "catalog published",
    )
    if not on_map:
        button(call, "Повторить")
    wait(call, "document.body.innerText.includes('Кадр записи получен')", "pending archive recovery")
    if not on_map:
        button(call, "К выбранной тревоге")


def inspect_alert(call, browser, alert, plan, position):
    original = js(call, "return document.getElementById('alert-'+arguments[0])", alert)
    result = "Проверка ворот " + browser
    reason = "Обход завершён " + browser
    button(call, "Взять в работу")
    wait(call, "document.querySelector('.ws-alert-work')?.innerText.includes('Ответственный: viewer')", "claimed alert")
    def note(text):
        node = wait(call, "document.querySelector('.ws-alert-work textarea')", "work note")
        call("POST", "/element/" + node["element-6066-11e4-a52e-4f735466cecf"] + "/value", {"text": text})
    note(result)
    button(call, "Записать результат")
    wait(call, buttons, "result persisted", "Закрыть тревогу")
    note(reason)
    button(call, "Закрыть тревогу")
    wait(call, "document.getElementById('alert-'+arguments[0])?.innerText.includes('Закрыта')", "closed alert in feed", alert)
    assert js(call, "return document.getElementById('alert-'+arguments[0])", alert) == original
    button(call, "Открыть журнал этой тревоги")
    field(call, "query", result)
    button(call, "Найти")
    wait(call, "document.querySelectorAll('.ws-history li').length===1 && document.querySelector('.ws-history li').innerText.includes(arguments[0])", "audit search", result)
    assert "viewer" in js(call, "return document.querySelector('.ws-history li').innerText")
    capture(call, browser, "alert-audit")
    button(call, "Открыть тревогу")
    wait(call, "document.body.innerText.includes('Кадр записи получен')", "archive from audit")
    assert js(call, "return document.querySelector('.ws-transport time').dateTime") == position
    assert js(call, "return new URLSearchParams(location.search).get('plan')") == plan


def archive_frame_pattern(call):
    return js(
        call,
        """const v=document.querySelector('video'),c=document.createElement('canvas');
        c.width=v.videoWidth;c.height=v.videoHeight;const ctx=c.getContext('2d');ctx.drawImage(v,0,0);
        let n=0;for(let i=0;i<5;i++)if(ctx.getImageData((i+.5)*c.width/5,40,1,1).data[0]>128)n|=1<<i;return n""",
    )


def operator(browser, cam, viewer, restart, plan):
    point = cam["point_id"]
    subprocess.run([c.CORE, "grant-point", "viewer", point], check=True)
    playback.fixture(cam, 0, 320, [(255, 0, 0), (0, 255, 0)])
    with browser_session(browser, None, "/", workspace_coverage=raw, software_map=True) as (call, _):
        login(call, "viewer")
        wait(call, "document.querySelector('.ws-stream-choice select')", "live stream selector")
        js(call, """const profile=document.querySelector('.ws-stream-choice select');
            profile.value='secondary';profile.dispatchEvent(new Event('change',{bubbles:true}));""")
        wait(
            call,
            """document.querySelector('video')?.videoWidth === 640 &&
                document.querySelector('.ws-player header p')?.textContent.includes('640×360')""",
            "selected secondary WebRTC frame",
        )
        wait(call, "document.body.innerText.includes('Тревог пока нет')", "empty live feed")
        send(motion(point))
        alert = snapshot(viewer)["items"][-1]
        link(call, wait, 'nav a[href^="/maps"]')
        button(call, "Планы")
        wait(call, "new URLSearchParams(location.search).get('plan') === arguments[0]", "visible operator plan", plan)
        link(call, wait, "#map-point-"+point)
        wait(call,"document.querySelector('.ws-plan img')?.naturalWidth===640","operator plan image")
        wait(call, "document.querySelector('.ws-plan .ws-pin-alert')?.textContent==='1'", "new alarm on the plan")
        link(call, wait, "#map-alert-" + alert["id"])
        wait(call, "document.querySelector('.ws-event-preview .ws-transport')?.innerText.includes('Кадр записи получен')", "archive frame on the plan")
        assert js(call, "return location.pathname") == "/maps" and archive_frame_pattern(call) == 16
        send(motion(point, 1500000))
        wait(call, "document.querySelectorAll('.ws-event-row').length===2", "incoming alarm during review")
        assert js(call, "return document.querySelector('.ws-event-row[aria-expanded=true]').id") == "map-alert-" + alert["id"]
        button(call, "Открыть тревогу в мониторинге")
        wait(call, "document.querySelector('.ws-watch .ws-transport')?.innerText.includes('Кадр записи получен')", "archive ready")
        position = js(call, "return document.querySelector('.ws-transport time').dateTime")
        assert position == alert["observed_at"], (position, alert)
        frame = archive_frame_pattern(call)
        assert frame == 16, (browser, frame)
        url = js(call, "return location.pathname+location.search")
        js(call, "window.previousVideo = document.querySelector('.ws-player video'); return true")
        button(call, "Эфир")
        wait(
            call,
            """!document.querySelector('.bt-timeline') && (() => {
                const video = document.querySelector('.ws-player video');
                const info = document.querySelector('.ws-player header p');
                return [640,1280].includes(video?.videoWidth) &&
                    info?.textContent.includes(video.videoWidth + '×' + video.videoHeight);
            })()""",
            "negotiated live frame without archive timeline",
        )
        assert js(call, "return !previousVideo.isConnected && previousVideo.paused && !previousVideo.getAttribute('src')")
        js(call, "window.previousVideo = document.querySelector('.ws-player video'); return true")
        button(call, "Архив")
        wait(call, "document.querySelector('.bt-track input') && document.querySelector('.ws-player')?.innerText.includes('Кадр записи получен')", "archive and timeline")
        assert js(call, "return !previousVideo.isConnected && previousVideo.paused && !previousVideo.srcObject")
        capture(call, browser, "operator")
        link(call,wait,'nav a[href^="/maps"]')
        wait(call,"document.activeElement?.id==='map-alert-'+arguments[0]", "restored event focus", alert["id"])
        assert js(call,"return new URLSearchParams(location.search).get('plan')")==plan
        button(call, "Архив")
        wait(call, "document.querySelector('.ws-watch .ws-transport')?.innerText.includes('Кадр записи получен')", "restored archive frame")
        assert js(call, "return document.querySelector('.ws-transport time').dateTime") == position
        inspect_alert(call, browser, alert["id"], plan, position)
        button(call, "К выбранной тревоге")
        wait(call, "document.activeElement?.id==='alert-'+arguments[0]", "restored feed focus", alert["id"])
        params = js(call, "return Object.fromEntries(new URLSearchParams(location.search))")
        assert (
            params["point"] == point
            and params["at"] == position
            and params["from"] == alert["archive_interval"]["start"]
            and params["to"] == alert["archive_interval"]["end"]
        ), params
        c.query(
            f"INSERT INTO bacuri_archive.gaps VALUES('{c.SITE}','{point}',1,{playback.ORIGIN_US + 2000000},{playback.ORIGIN_US + 3000000},'source_offline')"
        )
        send(motion(point, 2500000))
        gap = snapshot(viewer)["items"][-1]
        wait(call, "document.getElementById('alert-'+arguments[0])", "gap alert", gap["id"])
        js(call, "document.getElementById('alert-'+arguments[0]).click()", gap["id"])
        wait(
            call,
            "document.querySelector('.ws-overlay')?.innerText.includes('Нет связи с камерой')",
            "gap reason",
        )
        assert not js(call, "return document.querySelector('video').videoWidth")
        pending_alert(call, point, viewer, on_map=browser == "firefox")
    with browser_session(browser, viewer, url, workspace_coverage=raw) as (call, _):
        wait(call, "document.body.innerText.includes('Кадр записи получен')", "direct archive link")
        call("POST", "/refresh", {})
        wait(call, "document.body.innerText.includes('Кадр записи получен')", "refresh authorized archive")
        assert js(call, "return document.querySelector('.ws-transport time').dateTime") == position
        restart(
            lambda: wait(
                call,
                "!document.querySelector('video') && document.body.innerText.includes('Восстанавливаем')",
                "connection lost",
                timeout=5,
            )
        )
        wait(call, "document.body.innerText.includes('Кадр записи получен')", "connection restored")
        assert js(call, "return location.pathname+location.search") == url
        began = time.monotonic()
        c.query(f"DELETE FROM bacuri_access.point_grants WHERE point_id='{point}'")
        wait(
            call,
            "!document.querySelector('video') && !document.querySelector('.ws-alerts li')",
            "revoked point",
            timeout=5,
        )
        cutoff = time.monotonic() - began
        assert cutoff <= 5, cutoff
        link(call, wait, 'nav a[href^="/audit"]')
        field(call, "alert", alert["id"])
        button(call, "Найти")
        wait(call, "document.body.innerText.includes('Записи не найдены')", "audit after point access revocation")
        assert not js(call, "return !!document.querySelector('.ws-history li')")

    with browser_session(browser, None, "/cameras", workspace_coverage=raw) as (call, _):
        login(call, "viewer")
        wait(call, "document.body.innerText.includes('Недостаточно прав')", "installer access denied")
        c.query(
            "UPDATE bacuri_access.browser_sessions SET expires_at=now() WHERE user_id=(SELECT user_id FROM bacuri_access.local_users WHERE username='viewer')"
        )
        wait(call, "document.querySelector('input[name=username]')", "expired session", timeout=5)
        call("POST", "/url", {"url": c.ORIGIN + url})
        call("POST", "/refresh", {})
        wait(
            call,
            "document.querySelector('input[name=username]') && !document.querySelector('video')",
            "expired archive deep link",
        )
    with browser_session(browser, None, "/cameras", workspace_coverage=raw) as (call, _):
        login(call, "viewer")
        wait(call, "document.body.innerText.includes('Недостаточно прав')", "idle operator")
        c.query(
            "UPDATE bacuri_access.browser_sessions SET last_active=now()-interval '29 minutes 57 seconds' WHERE user_id=(SELECT user_id FROM bacuri_access.local_users WHERE username='viewer')"
        )
        wait(
            call,
            "document.querySelector('input[name=username]')",
            "idle expiry despite background polling",
            timeout=8,
        )
    print(
        browser,
        "operator matched alert",
        alert["id"],
        "point",
        point,
        "UTC",
        position,
        "frame",
        frame,
        "revoked after",
        round(cutoff, 2),
        "s",
        flush=True,
    )


def run():
    for name, role in [("installer", "administrator"), ("viewer", "operator")]:
        subprocess.run([c.CORE, "create-user", name, role], input=c.PASSWORD + "\n", text=True, check=True)
    fixture_root = c.ROOT / "reference"
    (fixture_root / "build").mkdir(parents=True)
    (fixture_root / "build/web-client").symlink_to(Path("build/workspace-fixture").resolve())
    (fixture_root / "web").symlink_to(Path("web").resolve())
    evidence = []
    browsers = ("chrome", "firefox")
    routes = ("/cameras", "/monitor")

    def measure(phase, admin):
        for browser in browsers:
            for route in routes:
                evidence.append(action_cost(browser, phase, admin, route))
        (report / "performance.json").write_text(json.dumps(evidence, indent=2))

    with subprocess.Popen([c.CORE.resolve(), "serve"], cwd=fixture_root) as reference:
        try:
            c.wait_for(lambda: c.http("GET", "session")[0] == 401, "HTTPS reference")
            measure("fixture", c.login("installer"))
        finally:
            c.stop(reference)
    with ExitStack() as stack:

        def start(command, **options):
            proc = subprocess.Popen(command, **options)
            stack.callback(lambda: c.stop(proc) if proc.poll() is None else None)
            return proc

        core = start([c.CORE, "serve"])

        def restart(disconnected):
            nonlocal core
            c.stop(core)
            disconnected()
            core = start([c.CORE, "serve"])

        c.wait_for(lambda: c.http("GET", "session")[0] == 401, "HTTPS workspace")
        admin = c.login("installer")
        start([c.RECORDER], env={**os.environ, "BACURI_KEY_FILE": str(c.ROOT / "recorder.key")})
        start([playback.MEDIA])
        for browser in browsers:
            cam = installer(browser, admin, restart)
            viewer = c.login("viewer")
            plan = admin_placements(browser, admin, viewer, cam, wait, button, field, raw)
            operator(browser, cam, viewer, restart, plan)
            access_settings(browser, admin)
        measure("integrated", admin)


if __name__ == "__main__":
    if sys.argv[-1] == "maps":
        offline_maps(wait, button, field, raw, capture)
    else:
        run()
