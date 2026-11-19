"""Exercise the real observation socket, database, WSS browser feed and archive frame."""

from concurrent.futures import ThreadPoolExecutor
from contextlib import ExitStack
import json
import os
import sqlite3
from pathlib import Path
import subprocess
import sys
import time
import uuid

sys.path[:0] = [str(Path(__file__).resolve().parents[1] / x) for x in ("control", "archive")]
import registry as c
import playback
from browser import browser_session
from blocked import stalled_reader

SOURCE = c.BIN / "bacuri-vms-observation-source"
SITE = c.SITE
BASE = f"sites/{SITE}/alerts"
EPOCH = str(uuid.uuid4())


def send(q, error=None):
    p = subprocess.run([SOURCE], input=json.dumps(q), text=True, capture_output=True, timeout=8)
    if error is not None:
        assert p.returncode and p.stderr.startswith(str(error) + " "), p.stderr
    else:
        assert p.returncode == 0, p.stderr
    return p.stdout.strip()


def motion(point, offset=640000):
    return {
        "type": "motion",
        "schemaVersion": 1,
        "observationId": str(uuid.uuid4()),
        "binding": {"pointId": point, "revision": "1"},
        "attributesJson": "e30=",
        "sourceTimeUs": str(playback.ORIGIN_US + offset),
        "clock": {"epochId": EPOCH, "uncertaintyUs": "80"},
    }


def snapshot(user):
    code, _, page = c.http("GET", BASE, headers=user)
    assert code == 200, page
    return page


def js(call, script, *args):
    return call("POST", "/execute/sync", {"script": script, "args": list(args)})


def probe(call, cursor):
    ok = call(
        "POST",
        "/execute/async",
        {"script": Path("tests/alerts/probe.js").read_text(), "args": [cursor, SITE]},
    )
    assert ok


def frames(call, count):
    return c.wait_for(
        lambda: js(call, "return window.probe.frames.length>=arguments[0] && window.probe.frames", count),
        "ordered WSS frames",
    )


def closed(call, code):
    actual = c.wait_for(lambda: js(call, "return window.probe.code"), "socket close")
    assert actual == code, (actual, code)


def bulk(point, count):
    # Import a committed burst of the same motion type in one transaction for deterministic lag.
    c.query(f"""BEGIN;
        UPDATE bacuri_events.feed_heads SET position=position+{count} WHERE site_id='{SITE}';
        WITH batch AS (SELECT generate_series(position-{count}+1,position) AS seq FROM bacuri_events.feed_heads WHERE site_id='{SITE}'),
        ids AS MATERIALIZED (SELECT seq,gen_random_uuid() AS id FROM batch),
        obs AS (INSERT INTO bacuri_events.observations
            SELECT '{SITE}','80000000-0000-4000-8000-000000000001',id,'motion',1,'{point}',1,'local_source',100,100,NULL,'reception',NULL,NULL,'{{}}' FROM ids RETURNING observation_id),
        inserted AS (INSERT INTO bacuri_events.alerts(site_id,position,source_id,observation_id,point_id)
            SELECT '{SITE}',seq,'80000000-0000-4000-8000-000000000001',id,'{point}' FROM ids JOIN obs ON observation_id=id RETURNING site_id,position)
        INSERT INTO bacuri_events.alert_outbox SELECT * FROM inserted; COMMIT""")


def run():
    with ExitStack() as services:

        def start(cmd, **options):
            p = subprocess.Popen(cmd, **options)

            def stop():
                c.stop(p)
                assert p.returncode == 0, (cmd, p.returncode)

            services.callback(stop)
            return p

        for name, role in [("admin", "administrator"), ("viewer", "operator")]:
            subprocess.run(
                [c.CORE, "create-user", name, role], input=c.PASSWORD + "\n", text=True, check=True
            )
        core = start([c.CORE, "serve"])
        c.wait_for(lambda: c.http("GET", "session")[0] == 401, "HTTPS")
        start([playback.MEDIA])
        admin, viewer = c.login("admin"), c.login("viewer")

        def camera(name):
            code, _, cam = c.http(
                "POST",
                f"sites/{SITE}/cameras",
                {
                    "name": name,
                    "main_url": "rtsp://rtsp-camera:8554/main",
                    "secondary_url": "rtsp://rtsp-camera:8554/sub",
                },
                {**admin, "Idempotency-Key": str(uuid.uuid4())},
            )
            assert code == 201, cam
            return cam

        cam = camera("Gate")
        point = cam["point_id"]
        playback.fixture(cam, 0, 320, [(255, 0, 0), (0, 255, 0)])
        hidden = camera("Other")["point_id"]
        subprocess.run([c.CORE, "grant-point", "viewer", point], check=True)
        empty = snapshot(viewer)
        assert not empty["items"]
        event = motion(point)
        send({**event, "sourceId": str(uuid.uuid4())}, 7)
        send({**event, "binding": {"pointId": point, "revision": 50}}, 5)
        send({**event, "attributesJson": "W10="}, 3)
        with ThreadPoolExecutor(4) as pool:
            states = list(pool.map(send, [event] * 4))
        assert (
            states.count("REGISTRATION_STATE_COMMITTED") == 1
            and states.count("REGISTRATION_STATE_REPLAYED") == 3
        ), states
        send({**event, "attributesJson": "eyJ4IjoxfQ=="}, 6)
        first = snapshot(viewer)["items"][0]
        assert len(snapshot(viewer)["items"]) == 1
        assert c.query("SELECT count(*) FROM bacuri_events.alert_outbox") == "1"
        assert c.query("SELECT count(*) FROM bacuri_events.observations") == "1"
        c.query(
            "ALTER TABLE bacuri_events.alert_outbox ADD CONSTRAINT test_failure CHECK (position<2) NOT VALID"
        )
        fail = motion(point)
        send(fail, 14)
        assert (
            c.query(
                f"SELECT count(*) FROM bacuri_events.observations WHERE observation_id='{fail['observationId']}'"
            )
            == "0"
        )
        c.query("ALTER TABLE bacuri_events.alert_outbox DROP CONSTRAINT test_failure")
        send({**motion(hidden), "type": "heartbeat"})
        send(motion(hidden))
        other = snapshot(admin)["items"][-1]
        assert c.http("GET", BASE + "/" + other["id"] + "/archive", headers=viewer)[0] == 404
        assert len(snapshot(viewer)["items"]) == 1
        assert (
            c.http("GET", BASE + "/stream", headers={**viewer, "Origin": "https://other.invalid"})[0] == 403
        )
        link = c.http("GET", BASE + "/" + first["id"] + "/archive", headers=viewer)[2]
        assert link["state"] == "ready" and link["point_id"] == point, link
        for browser in ("chrome", "firefox"):
            with browser_session(browser, viewer, "/alerts/") as (call, version):
                c.wait_for(
                    lambda: js(
                        call, "return document.getElementById('alert-'+arguments[0])!==null", first["id"]
                    ),
                    "browser snapshot",
                )
                c.wait_for(
                    lambda: js(
                        call, "return document.getElementById('status').textContent==='Лента подключена'"
                    ),
                    "live browser feed",
                )
                before = js(call, "return document.getElementById('alerts').childElementCount")
                began = time.monotonic()
                send(motion(point))
                c.wait_for(
                    lambda: js(call, "return document.getElementById('alerts').childElementCount") > before,
                    "live alert in DOM",
                )
                print(
                    browser,
                    "commit request to browser alert",
                    round((time.monotonic() - began) * 1000),
                    "ms",
                    flush=True,
                )
                js(
                    call,
                    "document.getElementById('alert-'+arguments[0]).querySelector('a').click()",
                    first["id"],
                )
                c.wait_for(
                    lambda: js(
                        call, "return document.getElementById('status')?.textContent==='Кадр тревоги'"
                    ),
                    "archive target frame",
                )
                frame = js(
                    call,
                    """const v=document.querySelector('video'), c=document.createElement('canvas');c.width=v.videoWidth;c.height=v.videoHeight;const ctx=c.getContext('2d');ctx.drawImage(v,0,0);let n=0;for(let i=0;i<5;i++) if(ctx.getImageData((i+.5)*c.width/5,40,1,1).data[0]>128)n|=1<<i;return n""",
                )
                assert frame == 16, (browser, frame)
                print(browser, version, "alert opened encoded frame", frame, flush=True)
        fallback = motion(point)
        del fallback["clock"]
        send(fallback)

        def last_link():
            alert = snapshot(viewer)["items"][-1]
            return c.http("GET", BASE + "/" + alert["id"] + "/archive", headers=viewer)[2]

        assert last_link()["state"] == "uncertain_time"
        wide = motion(point)
        wide["clock"]["uncertaintyUs"] = "16000000"
        send(wide)
        assert last_link()["state"] == "uncertain_time"
        send(motion(point, 2500000))
        missing = last_link()
        assert (missing["point_id"], missing["state"], missing["reason"]) == (
            point,
            "unavailable",
            "unknown_coverage",
        )
        c.query(
            f"INSERT INTO bacuri_archive.gaps VALUES('{SITE}','{point}',1,{playback.ORIGIN_US + 2000000},{playback.ORIGIN_US + 3000000},'source_offline')"
        )
        assert last_link()["reason"] == "source_offline"
        with browser_session("chrome", viewer, "/alerts/") as (call, _):
            probe(call, empty["cursor"])
            expected = snapshot(viewer)
            actual = frames(call, len(expected["items"]))
            assert [x["alert"] for x in actual] == expected["items"]
            js(call, "window.probe.socket.close()")
            bulk(point, 98)
            page = snapshot(viewer)
            assert len(page["items"]) == 100
            probe(call, page["cursor"])
            send(motion(point))
            delta = frames(call, 2)
            assert delta[0]["type"] == "remove" and delta[0]["alert_id"] == page["items"][0]["id"]
            js(call, "window.probe.socket.close()")
            probe(call, delta[0]["cursor"])
            assert frames(call, 1)[0]["alert"] == delta[1]["alert"]
            assert page["items"][1:] + [delta[1]["alert"]] == snapshot(viewer)["items"]
            bulk(point, 70)
            assert frames(call, 2)[-1] == {"type": "resync_required", "reason": "client_lag"}
            closed(call, 1000)
            current = snapshot(viewer)
            visible_ids = [a["id"] for a in reversed(current["items"])]
            c.wait_for(
                lambda: js(
                    call, "return [...document.querySelectorAll('#alerts li')].map(row=>row.id.slice(6))"
                )
                == visible_ids,
                "browser resnapshot after lag",
            )
            cursor = current["cursor"]
            probe(call, cursor)
            c.query(f"DELETE FROM bacuri_inventory.point_grants WHERE point_id='{point}'")
            assert frames(call, 1)[0]["reason"] == "scope_changed"
            closed(call, 1000)
            assert not snapshot(viewer)["items"]
            c.wait_for(
                lambda: js(call, "return !document.getElementById('alerts').childElementCount"),
                "revoked DOM cleared",
            )
            subprocess.run([c.CORE, "grant-point", "viewer", point], check=True)
            bulk(point, 1030)
            send(motion(point))
            assert c.query("SELECT count(*) FROM bacuri_events.alert_outbox") == "1024"
            probe(call, empty["cursor"])
            assert frames(call, 1)[0]["reason"] == "scope_changed"
            closed(call, 1000)
            probe(call, snapshot(admin)["cursor"])
            assert frames(call, 1)[0]["reason"] == "cursor_invalid"
            closed(call, 1000)
            slow = snapshot(viewer)["cursor"]
            bulk(point, 750)
            stalled_reader(viewer, BASE, slow, 750)
            probe(call, None)
            began = time.monotonic()
            closed(call, 1006)
            assert time.monotonic() - began < 6
            old = snapshot(viewer)["cursor"]
            bulk(point, 1030)
            send(motion(point))
            probe(call, old)
            assert frames(call, 1)[0]["reason"] == "cursor_expired"
            closed(call, 1000)
            probe(call, snapshot(viewer)["cursor"])
            with subprocess.Popen(
                ["psql", "-XAtq", "-v", "ON_ERROR_STOP=1"],
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                text=True,
            ) as lock:
                lock.stdin.write(
                    "BEGIN; LOCK bacuri_events.access_versions IN ACCESS EXCLUSIVE MODE; SELECT 'locked';\n"
                )
                lock.stdin.flush()
                assert lock.stdout.readline().strip() == "locked"
                try:
                    closed(call, 1011)
                finally:
                    lock.stdin.write("ROLLBACK;\n\\q\n")
                    lock.stdin.flush()
            fresh = snapshot(viewer)
            c.stop(core)
            start([c.CORE, "serve"])
            c.wait_for(lambda: c.http("GET", "session")[0] == 401, "restart")
            probe(call, fresh["cursor"])
            send(motion(point))
            assert frames(call, 2)[-1]["alert"] == snapshot(viewer)["items"][-1]
            began = time.monotonic()
            assert c.http("DELETE", "session", headers=viewer)[0] == 204
            closed(call, 1008)
            delay = time.monotonic() - began
            assert delay <= 5
            print(f"persistent cursor, 100-row window and session revocation in {delay:.3f} s", flush=True)

        # Pause only the catalog writer. The alert resolver must track the very same local block across registration.
        with subprocess.Popen(
            ["psql", "-XAtq", "-v", "ON_ERROR_STOP=1"],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            text=True,
        ) as lock:
            lock.stdin.write("BEGIN; LOCK bacuri_archive.blocks IN SHARE MODE; SELECT 'locked';\n")
            lock.stdin.flush()
            assert lock.stdout.readline().strip() == "locked"
            try:
                start([c.RECORDER], env={**os.environ, "BACURI_KEY_FILE": str(c.ROOT / "recorder.key")})
                journal = playback.ROOT / "journal.db"

                def local():
                    if not journal.exists():
                        return None
                    with sqlite3.connect(journal) as db:
                        if not db.execute(
                            "SELECT 1 FROM sqlite_master WHERE name='committed_blocks'"
                        ).fetchone():
                            return None
                        return db.execute(
                            """SELECT manifest_json->>'begin_utc_us',manifest_json->>'end_utc_us' FROM committed_blocks
                               WHERE registered_at_utc_us IS NULL AND manifest_json->>'point_id'=? AND
                               manifest_json->>'$.time.basis'='source_clock' AND manifest_json->>'begin_utc_us'>? LIMIT 1""",
                            (point, playback.ORIGIN_US + 60_000_000),
                        ).fetchone()

                a, b = c.wait_for(local, "pending source clock block")
                event = motion(point, (a + b) // 2 - playback.ORIGIN_US)
                send(event)
                url = BASE + "/" + snapshot(admin)["items"][-1]["id"] + "/archive"
                pending = c.http("GET", url, headers=admin)[2]
                assert pending["state"] == "pending_catalog", pending
            finally:
                lock.stdin.write("COMMIT;\n\\q\n")
                lock.stdin.flush()
        c.wait_for(
            lambda: c.http("GET", url, headers=admin)[2]["state"] == "ready", "same alert becomes playable"
        )
        print("pending catalog resolved to the recorded source-clock block", flush=True)
        sample = json.loads(subprocess.check_output([SOURCE, "sample", point, "1"], text=True))
        assert send(sample) == "REGISTRATION_STATE_COMMITTED"
        assert send(sample) == "REGISTRATION_STATE_REPLAYED"
        c.query(f"""INSERT INTO bacuri_inventory.point_bindings
            SELECT site_id,point_id,2,channel_id,valid_from_utc_us+1 FROM bacuri_inventory.point_bindings
            WHERE point_id='{point}' AND binding_revision=1;
            INSERT INTO bacuri_archive.gaps VALUES('{SITE}','{point}',2,{playback.ORIGIN_US + 8000000},{playback.ORIGIN_US + 9000000},'source_offline')""")
        send(motion(point, 8500000))
        url = BASE + "/" + snapshot(admin)["items"][-1]["id"] + "/archive"
        assert c.http("GET", url, headers=admin)[2]["reason"] == "unknown_coverage"


if __name__ == "__main__":
    run()
