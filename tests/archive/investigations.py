"""Exercise archive investigations and overlapping retention holds through the running services."""

from contextlib import ExitStack
from pathlib import Path
from urllib.parse import urlencode
from http.client import HTTPSConnection
from io import BytesIO
import base64
import json
import os
import subprocess
import shutil
import struct
import wave
import time
import tarfile
import uuid
import video_episodes
import playback
from playback import control as c


def inspect_browser(admin, results):
    coverage = results / "chrome"
    coverage.mkdir(exist_ok=True)
    with playback.browser_session("chrome", admin, "/investigations", workspace_coverage=coverage) as (
        call, version,
    ):

        def js(code, *args):
            return call("POST", "/execute/sync", {"script": code, "args": list(args)})

        def wait(code, label):
            return c.wait_for(lambda: js("return " + code), label)

        wait("document.querySelectorAll('.inv-points input').length===4", "camera filters")
        call(
            "POST",
            "/goog/cdp/execute",
            {
                "cmd": "Emulation.setDeviceMetricsOverride",
                "params": {"width": 1440, "height": 960, "deviceScaleFactor": 1, "mobile": False},
            },
        )
        js("for(const p of document.querySelectorAll('.inv-points input'))p.click()")
        js(
            """const set=Object.getOwnPropertyDescriptor(HTMLInputElement.prototype,'value').set;
        const bounds=document.querySelectorAll('input[type=datetime-local]');
        for(const [i,value] of arguments[0].entries()){
          const d=new Date(value);set.call(bounds[i],new Date(d.getTime()-d.getTimezoneOffset()*60000).toISOString().slice(0,23));
          bounds[i].dispatchEvent(new Event('input',{bubbles:true}));}
        """,
            [playback.utc(-1), playback.utc(1)],
        )
        js("document.querySelector('form[role=search] button[type=submit]').click()")
        wait(
            "document.querySelectorAll('.inv-track video').length===4 && Array.from(document.querySelectorAll('.inv-track video')).every(v=>v.readyState>=2)",
            "four decoded archive players",
        )

        def ordinal():
            return js(
                """return Array.from(document.querySelectorAll('.inv-track video'),v=>{
              if(v.seeking || v.readyState<2)return -1;
              const c=document.createElement('canvas');c.width=320;c.height=240;const ctx=c.getContext('2d');ctx.drawImage(v,0,0,320,240);
              let n=0;for(let b=0;b<5;b++)if(ctx.getImageData(32+64*b,40,1,1).data[0]>128)n+=2**b;return n;})"""
            )

        c.wait_for(lambda: len(set(ordinal())) == 1 and ordinal()[0] >= 0, "identical numbered frames")
        before = ordinal()[0]
        js(
            "Array.from(document.querySelectorAll('button')).find(b=>b.ariaLabel===arguments[0]).click()",
            "Следующий кадр",
        )
        c.wait_for(lambda: ordinal() == [before + 1] * 4, "frame stepping on four cameras")
        js(
            "Array.from(document.querySelectorAll('button')).find(b=>b.ariaLabel===arguments[0]).click()",
            "Добавить закладку и интервалы в материалы дела",
        )
        wait(
            "Array.from(document.querySelectorAll('button')).some(b=>b.textContent.includes('Материалы (4)'))",
            "four bookmarked intervals",
        )
        for w in (1440, 390):
            call(
                "POST",
                "/goog/cdp/execute",
                {
                    "cmd": "Emulation.setDeviceMetricsOverride",
                    "params": {"width": w, "height": 900, "deviceScaleFactor": 1, "mobile": False},
                },
            )
            assert js("return document.documentElement.scrollWidth<=innerWidth"), w
            shot = call(
                "POST",
                "/goog/cdp/execute",
                {"cmd": "Page.captureScreenshot", "params": {"format": "png"}},
            )
            (results / f"investigation-{w}.png").write_bytes(base64.b64decode(shot["data"]))
        js(
            "Array.from(document.querySelectorAll('button')).find(b=>b.ariaLabel==='Синхронный просмотр').click()"
        )
        wait(
            "Array.from(document.querySelectorAll('.inv-track video')).every(v=>!v.paused && !v.seeking)",
            "four players running",
        )
        decoded = []
        sources = js("return Array.from(document.querySelectorAll('.inv-track video'), v=>v.currentSrc)")
        for _ in range(29):
            frames = ordinal()
            if min(frames) >= 0:
                decoded.append(frames)
            time.sleep(0.025)
        assert len(decoded) >= 2 and max(f[0] for f in decoded) > min(f[0] for f in decoded), decoded
        assert sources == js(
            "return Array.from(document.querySelectorAll('.inv-track video'), v=>v.currentSrc)"
        )
        # A frame ordinal restarts at the next block; compute distance modulo the 25-frame fixture.
        drift = max(min(abs(a - b), 25 - abs(a - b)) for f in decoded for a in f for b in f) * 40
        assert any(b[0] < a[0] for a, b in zip(decoded, decoded[1:])), decoded
        assert drift <= 200, decoded
        print("Maximum decoded playback skew (ms)", drift, flush=True)
        print("Chrome", version, "four matching frame ordinals", before, before + 1, flush=True)
        return {"browser": version, "max_skew_ms": drift, "frame_samples": decoded}


def inspect_rule_editor(admin, point, results):
    with playback.browser_session(
        "chrome", admin, "/cameras?setup=rules", workspace_coverage=results / "chrome"
    ) as (call, _):
        def js(code, *args):
            return call("POST", "/execute/sync", {"script": code, "args": list(args)})

        ready = "return !!document.querySelector('.inv-rule:not(:disabled)')"
        c.wait_for(lambda: js(ready), "video rule editor")
        js("""
            const camera=document.querySelector('.inv-work select');
            camera.value=arguments[0]; camera.dispatchEvent(new Event('change',{bubbles:true}));
        """, point)
        c.wait_for(lambda: js(ready), "selected camera")
        js("""
            const el=document.querySelector('.inv-rule');
            [...el.querySelectorAll('label')].find(x=>x.textContent.includes('Пересечение линии')).click();
            [...el.querySelectorAll('summary')].find(x=>x.textContent==='Проверка на записи').click();
            const set=Object.getOwnPropertyDescriptor(HTMLInputElement.prototype,'value').set;
            for(const [i,input] of [...el.querySelectorAll('input[type=datetime-local]')].entries()){
                const d=new Date(arguments[0][i]);
                set.call(input,new Date(d.getTime()-d.getTimezoneOffset()*60000).toISOString().slice(0,23));
                input.dispatchEvent(new Event('input',{bubbles:true}));
            }
            el.querySelector('[aria-label="Проверить на записи"]').click();
        """, [playback.utc(0), playback.utc(1)])
        c.wait_for(
            lambda: js("return !!document.querySelector('.inv-line canvas') && "
                       "!!document.querySelector('.inv-rule p[role=status]')"),
            "decoded rule preview",
        )
        for width in (1440, 390, 320):
            call("POST", "/goog/cdp/execute", {"cmd": "Emulation.setDeviceMetricsOverride", "params": {
                "width": width, "height": 900, "deviceScaleFactor": 1, "mobile": False,
            }})
            overflow = js("""
                return [...document.querySelectorAll('body *')]
                    .filter(e=>e.getBoundingClientRect().right>innerWidth+1)
                    .map(e=>[e.tagName,e.className,e.getBoundingClientRect().right]).slice(0,10);
            """)
            assert js("return document.documentElement.scrollWidth<=innerWidth"), (width, overflow)
            assert js("""
                const menu=document.querySelector('.ws-bar nav');
                return menu.scrollWidth<=menu.clientWidth+1;
            """), width
            assert js("""
                return [...document.querySelectorAll('[aria-label="Подразделы камер"] button')].every(button => {
                    const bounds=button.getBoundingClientRect();
                    return [...button.querySelectorAll('span')].every(span => {
                        const label=span.getBoundingClientRect();
                        return !label.width || label.left>=bounds.left && label.right<=bounds.right;
                    });
                });
            """), width
            (results / f"video-rule-{width}.png").write_bytes(base64.b64decode(call("GET", "/screenshot")))


def search_timings(api, query):
    c.query("CREATE EXTENSION IF NOT EXISTS pg_buffercache")
    # Evict only the archive catalog, its indexes and TOAST in this disposable database.
    evict = """
        WITH tables AS (SELECT oid,reltoastrelid FROM pg_class WHERE relnamespace='bacuri_archive'::regnamespace AND relkind='r'),
        heaps AS (SELECT oid FROM tables UNION SELECT reltoastrelid FROM tables WHERE reltoastrelid<>0),
        files AS (SELECT oid FROM heaps UNION SELECT indexrelid FROM pg_index WHERE indrelid IN (SELECT oid FROM heaps))
        SELECT count(*) FILTER(WHERE pg_buffercache_evict(bufferid)) FROM pg_buffercache
        WHERE reldatabase=(SELECT oid FROM pg_database WHERE datname=current_database())
        AND relfilenode IN (SELECT pg_relation_filenode(oid) FROM files)
    """
    timings = {"cold_catalog": [], "warm": []}
    for _ in range(200):
        assert int(c.query(evict)) > 0
        for mode in timings:
            begin = time.monotonic()
            assert len(api("POST", "archive-search", query)) == 4
            timings[mode].append(1000 * (time.monotonic() - begin))
    out = {}
    for mode, samples in timings.items():
        samples.sort()
        out[f"four_camera_{mode}_search_ms"] = {
            k: round(samples[i], 2) for k, i in (("p50", 99), ("p95", 189), ("p99", 197))
        }
        assert samples[189] <= 2000, (mode, samples[189])
    return {**out, "search_requests_per_series": 200}


def restore_snapshot():
    snapshot = subprocess.check_output([c.CORE, "backup-config"])
    database = "bacuri_case_restore_" + uuid.uuid4().hex
    subprocess.run(["createdb", "--template=template0", database], check=True)
    try:
        env = {**os.environ, "PGDATABASE": database}
        subprocess.run([c.CORE, "migrate"], env=env, check=True)
        subprocess.run([c.CORE, "restore-config"], env=env, input=snapshot, check=True)
        for table, ordering in (
            ("bacuri_investigations.investigations", "id"),
            ("bacuri_archive.archive_holds", "id"),
            ("bacuri_archive.held_blocks", "hold_id,block_id"),
            ("bacuri_access.scoped_grants", "1"),
            ("bacuri_access.privacy_policies", "1"),
            ("bacuri_access.export_grants", "1"),
            ("bacuri_events.video_rules", "1"),
        ):
            sql = f"SELECT row_to_json(t)::text FROM {table} t ORDER BY {ordering}"
            assert subprocess.check_output(
                ["psql", "-XAtq", "-v", "ON_ERROR_STOP=1", "-c", sql], text=True, env=env
            ).strip() == c.query(sql), table
    finally:
        subprocess.run(["dropdb", database], check=True)


def run():
    mount = c.ROOT / "replica"
    mount.mkdir()
    os.environ["BACURI_ARCHIVE_REPLICA_DIR"] = str(mount)
    with ExitStack() as stack:
        for name, role in [("admin", "administrator"), ("analyst", "operator")]:
            subprocess.run(
                [c.CORE, "create-user", name, role], input=c.PASSWORD + "\n", text=True, check=True
            )

        def start(args, env=None):
            proc = subprocess.Popen(args, env=env)
            stack.callback(c.stop, proc)
            return proc

        start([c.CORE, "serve"])
        start([playback.MEDIA])
        c.wait_for(lambda: c.http("GET", "session")[0] == 401, "HTTPS")
        admin, analyst = c.login("admin"), c.login("analyst")

        def api(method, path, body=None, user=admin, status=200):
            code, _, reply = c.http(
                method, f"sites/{c.SITE}/" + path, body, {**user, "Idempotency-Key": str(uuid.uuid4())}
            )
            assert code == status, (method, path, code, reply)
            return reply

        cams = [
            api(
                "POST",
                "cameras",
                {
                    "name": name,
                    "main_url": "rtsp://rtsp-camera:8554/main",
                    "secondary_url": "rtsp://rtsp-camera:8554/sub",
                },
                status=201,
            )
            for name in ("Dock", "Lift", "Yard", "Ramp")
        ]
        pcm = struct.pack("<8000h", *(i - 4000 for i in range(8000)))
        blocks = [
            playback.fixture(
                cam,
                0,
                320,
                [(255, 0, 0), (0, 255, 0)],
                codec=codec,
                pcm=pcm,
                padding=(12 * 1024 * 1024 if i == 1 else 0),
                source_offset=2000000 if i == 1 else 0,
                reception=i == 3,
            )
            for i, (cam, codec) in enumerate(zip(cams, ("h265", "h264", "h264", "h264")))
        ]
        for cam, codec in zip(cams, ("h265", "h264", "h264", "h264")):
            playback.fixture(cam, -1, 320, [(255, 0, 0), (0, 255, 0)], codec=codec)
        cohort = video_episodes.prepare(cams[2])
        pending = c.query(
            "SELECT block_id FROM bacuri_archive.motion_indexes WHERE samples IS NULL ORDER BY retry_at,block_id"
        ).splitlines()
        # Four newer fragments were registered before the historic labeled data.
        assert pending[:4] == [b["block_id"] for b in blocks] and pending[-len(cohort[1]):] == cohort[1], pending
        points = [cam["point_id"] for cam in cams]
        first, second = points[:2]
        recorder_env = {**os.environ, "BACURI_KEY_FILE": str(c.ROOT / "recorder.key")}
        recorder = start([c.RECORDER], recorder_env)
        replica = mount / f"bacuri-{c.SITE}-{os.environ['BACURI_NODE_ID']}" / blocks[0]["block_id"]
        c.wait_for(lambda: (replica / "media.mp4").exists(), "verified archive copy")
        query = {"points": points, "interval": playback.interval(0, 1), "motion": False}

        def indexed():
            tracks = api("POST", "archive-search", query)
            return tracks if len(tracks) == 4 and all(r["frames"] for r in tracks) else None

        tracks = c.wait_for(indexed, "both archive indexes")
        assert all(len(r["frames"]) == 25 and r["hits"] and not r["gaps"] for r in tracks), tracks
        assert (
            tracks[3]["coverage"][0]["basis"] == "reception"
            and tracks[3]["coverage"][0]["uncertainty_us"] is None
        )
        assert blocks[1]["time"]["source_time_us"] - blocks[1]["begin_utc_us"] == 2000000
        empty = blocks[3]["block_id"]
        saved = c.query(f"SELECT samples FROM bacuri_archive.motion_indexes WHERE block_id='{empty}'")
        c.query(
            f"UPDATE bacuri_archive.motion_indexes SET samples=NULL,retry_at=now()+interval '1 hour' WHERE block_id='{empty}'"
        )
        stop_us = playback.ORIGIN_US + 1000000
        c.query(
            f"INSERT INTO bacuri_archive.gaps(site_id,point_id,binding_revision,begin_utc_us,end_utc_us,reason) VALUES('{c.SITE}','{second}',1,{stop_us},{stop_us+500000},'source_offline')"
        )
        partial = api("POST", "archive-search", {**query, "interval": playback.interval(0, 1.5)})
        assert (
            partial[3]["coverage"] and not partial[3]["coverage"][0]["indexed"] and not partial[3]["frames"]
        )
        assert (
            len(partial[0]["frames"]) == 25 and partial[1]["gaps"][0]["reason"] == "source_offline"
        ), partial[1]["gaps"]
        c.query(f"UPDATE bacuri_archive.motion_indexes SET samples='{saved}' WHERE block_id='{empty}'")
        plan, event = str(uuid.uuid4()), str(uuid.uuid4())
        timestamp = playback.ORIGIN_US + 280000
        c.query(f"""INSERT INTO bacuri_maps.floor_plans(site_id,plan_id,name,image,width,height)
                    VALUES('{c.SITE}','{plan}','Dock plan','','320','240');
                    INSERT INTO bacuri_maps.point_placements(site_id,point_id,plan_id,x,y)
                    VALUES('{c.SITE}','{first}','{plan}',0.5,0.5);
                    INSERT INTO bacuri_events.observations VALUES('{c.SITE}','80000000-0000-4000-8000-000000000001',
                    '{event}','motion',1,'{first}',1,'onvif',{timestamp},{timestamp},NULL,'reception',NULL,NULL,
                    '{{"topic":"FieldDetector","data":{{"area":"loading-bay"}}}}')""")
        by_event = api("POST", "archive-search", {**query, "plan":plan, "event_query":"LOADING-BAY"})
        assert [track["point_id"] for track in by_event] == [first]
        assert [item["id"] for item in by_event[0]["events"]] == [event]
        assert by_event[0]["hits"]
        assert not api("POST", "archive-search", query, analyst)
        rule = {
            "enabled": False,
            "source": "local",
            "include": [],
            "exclude": [],
            "line": None,
            "notify": True,
            "record": True,
            "preset": "",
            "min_cells": 1,
            "threshold": 5,
            "cooldown": 10,
            "schedule": [],
            "zone": "UTC",
        }
        rule_path = f"points/{first}/video-rule"
        trial = lambda r: api("POST", rule_path + "/simulation", {"rule": r, "interval": query["interval"]})
        assert len(trial(rule)["hits"]) == 1
        assert not trial({**rule, "exclude": [{"left": 0, "top": 0, "right": 1, "bottom": 1}]})["hits"]
        measures = video_episodes.evaluate(cohort, api, points[2], rule)
        assert not c.query("SELECT 1 FROM bacuri_events.video_matches")
        assert api("PUT", rule_path, {"rule": {**rule, "enabled": True}, "revision": "0"})["revision"] == "1"
        live = c.wait_for(
            lambda: c.query(
                f"SELECT observation_id FROM bacuri_events.video_matches WHERE point_id='{first}' AND revision=1 LIMIT 1"
            ),
            "live local detector",
        )
        assert c.query(f"SELECT count(*) FROM bacuri_events.alerts WHERE observation_id='{live}'") == "1"
        api("PUT", rule_path, {"rule": rule, "revision": "0"}, status=409)
        assert api("PUT", rule_path, {"rule": rule, "revision": "1"})["revision"] == "2"
        assert (
            c.query(f"SELECT revision FROM bacuri_events.video_matches WHERE observation_id='{live}'") == "1"
        )
        results = os.environ.get("BACURI_WORKSPACE_RESULTS")
        if results:
            measures["sync"] = inspect_browser(admin, Path(results))
            inspect_rule_editor(admin, first, Path(results))
            measures.update(search_timings(api, query))
            (Path(results) / "measurements.json").write_text(json.dumps(measures, indent=2))

        expiry = playback.utc(86400)
        hold = {
            "interval": playback.interval(0, 1),
            "reason": "Preserve loading sequence",
            "expires_at": expiry,
        }
        extra = api("POST", f"points/{first}/holds", hold, status=201)["id"]

        def uid(name):
            return c.query(f"SELECT user_id FROM bacuri_access.local_users WHERE username='{name}'")

        me, other = uid("admin"), uid("analyst")
        case_id = str(uuid.uuid4())
        route = "investigations/" + case_id
        draft = {
            "number": "LOADING-1",
            "revision": "0",
            "assignee": me,
            "notes": "Compare handover",
            "reason": "Loading dispute",
            "expires_at": expiry,
            "materials": [
                {
                    "point_id": p,
                    "interval": playback.interval(0.2, 0.8),
                    "bookmarks": [{"at": playback.utc(0.4), "name": "Handover"}],
                }
                for p in (first, second)
            ],
        }
        assert api("PUT", route, draft)["revision"] == "1"
        api("PUT", route, draft, status=409)
        assert len(api("GET", route)["materials"]) == 2
        assert api("GET", "investigations", user=analyst) == []
        for point in (first, second):
            subprocess.run([c.CORE, "grant-point", "analyst", point], check=True)
            c.query(
                f"INSERT INTO bacuri_access.scoped_grants VALUES(gen_random_uuid(),'{c.SITE}','{other}','{point}',NULL,'protect',NULL,NULL)"
            )
        assert api("PUT", route, {**draft, "revision": "1", "assignee": other})["revision"] == "2"
        transferred = api("GET", route, user=analyst)
        assert transferred["assignee"] == other
        for point in (first, second):
            subprocess.run(
                [c.CORE, "grant-export", "analyst", point, playback.utc(-2), playback.utc(2)], check=True
            )
            c.query(
                f"INSERT INTO bacuri_access.scoped_grants VALUES(gen_random_uuid(),'{c.SITE}','{other}','{point}',NULL,'audio',NULL,NULL)"
            )
        recipients = api("POST", "investigation-assignees", {"materials": draft["materials"]}, analyst)
        assert {u["name"] for u in recipients} == {"admin", "analyst"}
        media = playback.ROOT / "blocks" / blocks[0]["block_id"] / "media.mp4"
        original = media.read_bytes()
        media.write_bytes(b"damaged copy")

        def export(mode, case, sound=True):
            conn = HTTPSConnection("localhost", 8443, context=c.TLS, timeout=15)
            try:
                conn.request(
                    "POST",
                    f"/api/v1/sites/{c.SITE}/" + (route if case else f"points/{first}") + "/exports",
                    body=json.dumps(
                        {
                            **({"revision": "2"} if case else {"interval": playback.interval(0.2, 0.8)}),
                            "mode": mode,
                            "reason": "Dispute",
                            "audio": sound,
                        }
                    ),
                    headers={**analyst, "Content-Type": "application/json", "Origin": c.ORIGIN},
                )
                res = conn.getresponse()
                payload = res.read()
                assert res.status == 200, (res.status, payload[:400])
                return tarfile.open(fileobj=BytesIO(payload))
            finally:
                conn.close()

        key = c.ROOT / "trusted.json"
        key.write_bytes(subprocess.check_output([c.CORE, "export-public-key"]))

        def verify(mode, case=False):
            root = c.ROOT / (("case-" if case else "") + mode)
            root.mkdir()
            with export(mode, case) as bundle:
                bundle.extractall(root, filter="data")
            return root, json.loads(subprocess.check_output([c.BIN / "bacuri-vms-verify-export", key, root]))

        root, manifest = verify("original")
        clip = next(p for p in manifest["periods"] if p["state"] == "recorded")
        assert clip["codec"] == "h265" and (root / clip["file"]).read_bytes() == original
        case_root, case_manifest = verify("original", True)
        assert len(case_manifest["parts"]) == 2 and case_manifest["case"]["number"] == "LOADING-1"
        with wave.open(str(root / clip["audio"]["file"])) as wav:
            assert wav.getframerate() == 8000 and wav.readframes(8000) == pcm[3200:12800]
        stopped = playback.slow_reader(
            f"sites/{c.SITE}/{route}/exports",
            analyst,
            lambda: subprocess.run([c.CORE, "revoke-export", "analyst", second], check=True),
            {"revision": "2", "mode": "original", "reason": "Active transfer test", "audio": True},
        )
        if results:
            measures["case_revocation_seconds"] = round(stopped, 3)
            (Path(results) / "measurements.json").write_text(json.dumps(measures, indent=2))
        subprocess.run(
            [c.CORE, "grant-export", "analyst", second, playback.utc(-2), playback.utc(2)], check=True
        )
        mask = json.dumps([dict(left=0, top=0, right=1, bottom=1)])
        c.query(
            f"INSERT INTO bacuri_access.privacy_policies VALUES('{c.SITE}','{first}',false,true,true,true,'{mask}')"
        )
        hidden = api("POST", "archive-search", {**query, "motion": True}, analyst)
        assert not next(r for r in hidden if r["point_id"] == first)["hits"]
        previews = api("POST", "archive-search", query, analyst)
        assert not any(
            base64.b64decode(next(r for r in previews if r["point_id"] == first)["hits"][0]["preview"])
        )
        root, redacted = verify("derivative")
        clip = next(p for p in redacted["periods"] if p["state"] == "recorded")
        pixels = subprocess.check_output(
            [
                "gst-launch-1.0",
                "-q",
                "filesrc",
                "location=" + str(root / clip["file"]),
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
        assert pixels and max(pixels) <= 18
        root, case_copy = verify("derivative", True)
        if results:
            shutil.copyfile(key, Path(results) / "trusted-key.json")
            for mode, package in (("original", case_root), ("derivative", root)):
                with tarfile.open(Path(results) / f"case-{mode}.tar", "w") as out:
                    for item in package.iterdir():
                        out.add(item, arcname=item.name)
        c.query(f"DELETE FROM bacuri_access.scoped_grants WHERE site_id='{c.SITE}' AND point_id='{second}' AND action='audio'")
        api("POST", route+"/exports", {"mode":"derivative", "reason":"No audio grant", "audio":True, "revision":"2"}, analyst, 403)
        with export("derivative", True, sound=False) as silent:
            assert not any(entry.name.endswith(".wav") for entry in silent.getmembers())
            for entry in silent.getmembers():
                if entry.name.endswith("/manifest.json"):
                    part=json.load(silent.extractfile(entry))
                    assert not part["audio_requested"] and all(period.get("audio") is None for period in part["periods"])
        damaged = root / case_copy["parts"][0]["directory"] / "manifest.sig"
        damaged.write_text("0" * 128)
        assert (
            subprocess.run([c.BIN / "bacuri-vms-verify-export", key, root], capture_output=True).returncode
            != 0
        )
        api(
            "POST",
            route + "/exports",
            {"mode": "original", "reason": "Denied", "audio": False, "revision": "2"},
            analyst,
            403,
        )
        c.stop(recorder)
        restore_snapshot()
        lost = blocks[2]["block_id"]
        other_copy = replica.parent / lost / "media.mp4"
        assert other_copy.is_file()
        (playback.ROOT / "blocks" / lost / "media.mp4").write_bytes(b"damaged")
        other_copy.write_bytes(b"damaged")
        rebuilt = json.loads(subprocess.check_output([c.CORE, "rebuild-archive"]))
        assert media.read_bytes() == original and (replica / "media.mp4").read_bytes() == original, rebuilt
        assert c.query(f"SELECT unavailable_reason FROM bacuri_archive.blocks WHERE block_id='{lost}'") in (
            "missing",
            "corrupt",
        )
        lost_track = api("POST", "archive-search", {**query, "points": [points[2]]})[0]
        assert not lost_track["frames"] and lost_track["gaps"], lost_track
        assert api("GET", route, user=analyst) == transferred
        start([c.RECORDER], recorder_env)
        end = playback.ORIGIN_US + 2000000

        def reap():
            c.query(
                f"BEGIN; SELECT pg_advisory_xact_lock(hashtextextended('{c.SITE}',1)); INSERT INTO bacuri_archive.retired_through VALUES('{c.SITE}',{end}) ON CONFLICT(site_id) DO UPDATE SET end_us=greatest(bacuri_archive.retired_through.end_us,excluded.end_us); SELECT bacuri_archive.reap_expired('{c.SITE}',{end}); COMMIT"
            )

        c.query(f"UPDATE bacuri_archive.archive_holds SET expires_us=0 WHERE id='{extra}'")
        # Race a fourth-point hold against the same retirement transaction.
        from concurrent.futures import ThreadPoolExecutor
        from threading import Barrier

        barrier = Barrier(2)

        def request_hold():
            barrier.wait()
            return c.http(
                "POST",
                f"sites/{c.SITE}/points/{points[3]}/holds",
                hold,
                {**admin, "Idempotency-Key": str(uuid.uuid4())},
            )[0]

        with ThreadPoolExecutor(max_workers=1) as jobs:
            future = jobs.submit(request_hold)
            barrier.wait()
            reap()
            status = future.result()
        assert status in (201, 409), status
        exists = c.query(
            f"SELECT 1 FROM bacuri_archive.retained_blocks WHERE block_id='{blocks[3]['block_id']}'"
        )
        assert bool(exists) == (status == 201)
        timeline_path = f"points/{first}/archive?" + urlencode({**playback.interval(0, 1), "presentation": "masked"})
        assert [item["state"] for item in api("GET", timeline_path)["items"]] == ["recorded"]
        assert all(
            c.query(f"SELECT 1 FROM bacuri_archive.retained_blocks WHERE block_id='{b['block_id']}'")
            for b in blocks[:2]
        )
        c.query(f"UPDATE bacuri_archive.archive_holds SET expires_us=0 WHERE site_id='{c.SITE}'")
        reap()
        assert all(
            not c.query(f"SELECT 1 FROM bacuri_archive.blocks WHERE block_id='{b['block_id']}'")
            for b in blocks
        )
        assert [item["state"] for item in api("GET", timeline_path)["items"]] == ["unavailable"]
        c.wait_for(lambda: not media.exists() and not replica.exists(), "expiry of both copies")
        print(
            "Verified dual-camera indexes, privacy, case transfer, overlapping holds, replica delivery and signed exports",
            flush=True,
        )


if __name__ == "__main__":
    run()
