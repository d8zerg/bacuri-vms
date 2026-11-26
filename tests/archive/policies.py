"""Exercise real recording decisions using an RTSP feed with numbered pixels and a microphone tone."""

from contextlib import ExitStack
import base64
import json
import os
from pathlib import Path
import sqlite3
import subprocess
import sys
import time
import uuid

sys.path[:0] = [str(Path(__file__).resolve().parents[1] / x) for x in ("control", "cameras")]
import registry as c
from browser import browser_session
from check import streams

site = "sites/" + c.SITE
root = Path(os.environ["BACURI_ARCHIVE_DIR"])
rule = dict(
    continuous=False,
    event=True,
    manual=True,
    audio=True,
    pre=5,
    post=10,
    schedule=[],
    zone="Europe/Berlin",
    days=7,
)


def api(method, path, user, body=None, status=200, **headers):
    code, _, data = c.http(method, site + "/" + path, body, {**user, **headers})
    assert code == status, (method, path, code, data)
    return data


def wait(check, label, timeout=45):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        if result := check():
            return result
        time.sleep(0.15)
    raise AssertionError(label)


def manifests(point, begin):
    with sqlite3.connect(root / "journal.db") as db:
        rows = db.execute(
            "SELECT manifest_json FROM committed_blocks WHERE manifest_json->>'point_id'=? "
            "AND manifest_json->>'begin_utc_us'>=? ORDER BY manifest_json->>'begin_utc_us'",
            (point, begin),
        )
        return [json.loads(r[0]) for r in rows]


def applied(point, policy_id, revision):
    with sqlite3.connect(root / "journal.db") as db:
        row = db.execute("SELECT payload FROM recording_plan").fetchone()
        policy = json.loads(row[0]).get(point, {}) if row else {}
        return policy.get("id") == policy_id and policy.get("revision") == int(revision)


def event(point, repeat=False):
    sample = dict(
        type="motion",
        schemaVersion=1,
        observationId=str(uuid.uuid4()),
        binding=dict(pointId=point, revision="1"),
        attributesJson="e30=",
    )
    for _ in range(2 if repeat else 1):
        subprocess.run(
            [c.BIN / "bacuri-vms-observation-source"], input=json.dumps(sample), text=True, check=True
        )
    return int(
        c.query(
            f"SELECT observed_at_utc_us FROM bacuri_events.observations WHERE observation_id='{sample['observationId']}'"
        )
    )


def numbered_frames(block):
    raw = subprocess.check_output(
        [
            "gst-launch-1.0",
            "-q",
            "filesrc",
            "location=" + str(root / "blocks" / block["block_id"] / "media.mp4"),
            "!",
            "qtdemux",
            "!",
            "h264parse",
            "!",
            "avdec_h264",
            "!",
            "videocrop",
            "right=1024",
            "bottom=704",
            "!",
            "videoconvert",
            "!",
            "video/x-raw,format=GRAY8,width=256,height=16",
            "!",
            "fdsink",
            "fd=1",
            "sync=false",
        ],
        timeout=10,
    )
    assert raw and len(raw) % 4096 == 0
    return [
        sum((raw[off + 8 * 256 + 16 * bit + 8] > 128) << bit for bit in range(16))
        for off in range(0, len(raw), 4096)
    ]


def run():
    for name, role in (("manager", "administrator"), ("observer", "operator")):
        subprocess.run([c.CORE, "create-user", name, role], input=c.PASSWORD + "\n", text=True, check=True)
    with ExitStack() as stack:

        def start(args, **kwargs):
            p = subprocess.Popen(args, **kwargs)
            stack.callback(c.stop, p)
            return p

        stack.enter_context(
            streams(
                Path(os.environ["BACURI_TLS_CERT"]),
                Path(os.environ["BACURI_TLS_KEY"]),
                c.BIN / "tests/fixtures",
                audio=True,
            )
        )
        start([c.CORE, "serve"])
        c.wait_for(lambda: c.http("GET", "session")[0] == 401, "HTTPS")
        admin = c.login("manager")
        observer = c.login("observer")
        recorder_env = {**os.environ, "BACURI_KEY_FILE": str(c.ROOT / "recorder.key")}
        recorder = start([c.RECORDER], env=recorder_env)
        camera = api(
            "POST",
            "cameras",
            admin,
            {
                "name": "Контрольная камера",
                "main_url": "rtsps://localhost:8554/main",
                "secondary_url": "rtsps://localhost:8554/sub",
            },
            201,
            **{"Idempotency-Key": str(uuid.uuid4())},
        )
        point = camera["point_id"]
        wait(lambda: len(manifests(point, 0)) >= 2, "original continuous recording")
        manual = f"points/{point}/recording"
        api("GET", "storage/volumes", observer, status=403)
        current = api("GET", "storage/volumes", admin)
        # The reserve stays well below the free share of the host disk, so the volume reports normal space.
        free = 100 * current["volume"]["available_bytes"] // current["volume"]["capacity_bytes"]
        storage_edit = {
            "days": 30,
            "reserve_percent": max(1, min(15, free - 10)),
            "revision": current["revision"],
        }
        volume = api("PUT", "storage/volumes", admin, storage_edit)
        api("PUT", "storage/volumes", admin, storage_edit, 409)
        assert volume["days"] == 30 and volume["points"]
        # The recorder reports the space state for the new reserve with its next status.
        wait(lambda: api("GET", "storage/volumes", admin)["volume"]["state"] == "normal", "normal volume space")
        _, headers, _ = c.http("GET", site + "/resource-groups", headers=admin)
        group = api(
            "POST",
            "resource-groups",
            admin,
            {"name": "Запись ворот", "points": [point]},
            **{"If-Match": headers["ETag"]},
        )["data"]["id"]
        group_policy, point_policy = str(uuid.uuid4()), str(uuid.uuid4())

        def put(id, scope, target, revision, definition, status=200):
            return api(
                "PUT",
                f"recording-policies/{id}",
                admin,
                dict(scope=scope, target=target, priority=50, revision=revision, rule=definition),
                status,
            )

        put(group_policy, "group", group, "0", {**rule, "audio": False})
        assert api("GET", "recording-policies", admin)["effective"][point]["id"] == group_policy
        c.query(f"INSERT INTO bacuri_archive.archive_recovery VALUES('{c.SITE}','verifying')")
        put(point_policy, "point", point, "0", rule)
        wait(
            lambda: applied(point, point_policy, "1"),
            "new recording policy remains available during archive verification",
        )
        c.query(f"DELETE FROM bacuri_archive.archive_recovery WHERE site_id='{c.SITE}'")
        put(point_policy, "point", point, "0", rule, 409)
        api("GET", manual, observer, status=403)
        users = api("GET", "users", admin)["data"]
        uid = next(u["id"] for u in users if u["username"] == "observer")
        for action in ("view", "record"):
            revision = api("GET", "users", admin)["revision"]
            api(
                "PUT",
                "grants/" + str(uuid.uuid4()),
                admin,
                {"group_id": group, "user_id": uid, "action": action},
                **{"If-Match": '"' + revision + '"'},
            )
        api("PUT", manual, observer, {"active": True, "seconds": 3}, 403)
        time.sleep(7)
        first = event(point, True)
        api("PUT", manual, admin, {"active": True, "seconds": 60})
        time.sleep(2)
        last = event(point)
        api("PUT", manual, admin, {"active": False})
        assert c.query("SELECT count(*) FROM bacuri_archive.recording_windows WHERE kind='event'") == "2"
        begin, end = first - 5_000_000, last + 10_000_000

        def complete():
            blocks = manifests(point, begin - 1_100_000)
            return blocks if blocks and blocks[-1]["end_utc_us"] >= end - 90000 else None

        blocks = wait(complete, "event post-roll", 60)
        assert begin - 1_100_000 <= blocks[0]["begin_utc_us"] <= begin + 90000, (begin, blocks[0])
        assert end - 90000 <= blocks[-1]["end_utc_us"] <= end + 90000, (end, blocks[-1])
        frames = [n for m in blocks for n in numbered_frames(m)]
        assert all(b == (a + 1) % 65536 for a, b in zip(frames, frames[1:])), frames
        audio = [m for m in blocks if "audio" in m]
        assert len(audio) >= len(blocks) - 2, (len(audio), len(blocks))
        import array

        sample = array.array("h", (root / "blocks" / audio[0]["block_id"] / "audio.pcm").read_bytes())
        assert max(sample) > 100 and min(sample) < -100
        print(
            "Event union, frame numbers and audio verified",
            len(frames),
            frames[0],
            frames[-1],
            len(audio),
            flush=True,
        )
        c.stop(recorder)
        recorder = start([c.RECORDER], env=recorder_env)

        def restarted():
            assert recorder.poll() is None, ("recorder exit during restart", recorder.returncode)
            state = api("GET", "storage/volumes", admin)["volume"]
            return state and state["point_states"].get(point) in ("waiting_event", "selected")

        wait(restarted, "source and volume after recorder restart")
        second = event(point)
        wait(lambda: bool(manifests(point, second - 500000)), "event after restart", 45)
        api("DELETE", f"recording-policies/{point_policy}", admin, status=204, **{"If-Match": '"1"'})
        wait(lambda: applied(point, group_policy, "1"), "inherited silent rule")
        start_state = api("PUT", manual, observer, {"active": True, "seconds": 4})
        replay = api("PUT", manual, admin, {"active": True, "seconds": 4})
        assert replay["until_us"] == start_state["until_us"]
        start_us = start_state["until_us"] - 4_000_000
        silent = wait(
            lambda: [m for m in manifests(point, start_us) if m["end_utc_us"] >= start_us + 3000000],
            "silent manual recording",
        )
        assert all("audio" not in m for m in silent)
        assert not api("GET", manual, admin)["active"]
        weekly = {
            **rule,
            "audio": False,
            "pre": 0,
            "post": 1,
            "schedule": [{"day": d, "start": 0, "end": 1440} for d in range(1, 8)],
        }
        put(group_policy, "group", group, "1", weekly)
        wait(lambda: applied(point, group_policy, "2"), "weekly rule applied")
        overlap = api("PUT", manual, admin, {"active": True, "seconds": 3})
        event(point)
        begin = overlap["until_us"] - 3_000_000

        def schedule_continues():
            saved = manifests(point, begin)
            return saved if saved and saved[-1]["end_utc_us"] > overlap["until_us"] + 2_000_000 else None

        scheduled = wait(schedule_continues, "calendar recording continues beyond manual and motion windows")
        frames = [n for block in scheduled for n in numbered_frames(block)]
        assert len(frames) > 60 and all(b == (a + 1) % 65536 for a, b in zip(frames, frames[1:]))
        assert all("audio" not in block for block in scheduled)
        assert (
            c.query("SELECT count(*) FROM bacuri_events.alert_audit WHERE action='manual_recording'") == "4"
        )
        assert (
            c.query("SELECT count(*) FROM bacuri_events.alert_audit WHERE action='recording_storage'") == "1"
        )
        report = (
            Path(os.environ["BACURI_WORKSPACE_RESULTS"])
            if os.environ.get("BACURI_WORKSPACE_RESULTS")
            else None
        )
        raw = report / "chrome" if report else None
        if raw:
            raw.mkdir(parents=True, exist_ok=True)
        with browser_session("chrome", admin, "/cameras?setup=recording", workspace_coverage=raw) as (
            call,
            _,
        ):

            def js(script):
                return call("POST", "/execute/sync", {"script": script, "args": []})

            wait(
                lambda: js('return document.querySelector(".rp-group-link")?.innerText.includes("Запись ворот")'),
                "inherited policy in browser",
            )
            call("POST", "/window/rect", {"width": 1440, "height": 1000})
            if report:
                (report / "recording-policy.png").write_bytes(base64.b64decode(call("GET", "/screenshot")))
            call(
                "POST",
                "/goog/cdp/execute",
                {
                    "cmd": "Emulation.setDeviceMetricsOverride",
                    "params": {"width": 390, "height": 844, "deviceScaleFactor": 1, "mobile": True},
                },
            )
            assert js("return document.documentElement.scrollWidth<=window.innerWidth")
            if report:
                (report / "recording-policy-mobile.png").write_bytes(
                    base64.b64decode(call("GET", "/screenshot"))
                )
            call("POST", "/goog/cdp/execute", {"cmd": "Emulation.clearDeviceMetricsOverride", "params": {}})
            assert js('return !document.querySelector(".rp-editor")')
            js('document.querySelector(".rp-group-link").click()')
            wait(lambda: js('return document.querySelector(".rp-members summary")?.innerText.includes("1 из 1")'), "effective group membership")
            js('document.querySelector(".rp-members summary").click()')
            if report:
                (report / "recording-group.png").write_bytes(base64.b64decode(call("GET", "/screenshot")))
            js('document.querySelector(".rp-members li button").click()')
            wait(lambda: js('return !!document.querySelector(".rp-edit-camera")'), "group member camera overview")
            js('document.querySelector(".rp-edit-camera").click()')
            wait(lambda: js('return !!document.querySelector(".rp-editor button[type=submit]")'), "camera override editor")
            js('document.querySelector(".rp-editor button[type=submit]").click()')
            wait(
                lambda: any(
                    p["scope"] == "point" for p in api("GET", "recording-policies", admin)["policies"]
                ),
                "web saved camera override",
            )
            js('Array.from(document.querySelectorAll("button")).find(b=>b.textContent==="Том").click()')
            wait(lambda: js('return !!document.querySelector("meter")'), "real capacity in browser")
        with browser_session(
            "chrome", observer, "/monitor?mode=live&point=" + point, workspace_coverage=raw
        ) as (call, _):

            def press(label):
                return call(
                    "POST",
                    "/execute/sync",
                    {
                        "script": "const b=document.querySelector('[aria-label=\"'+arguments[0]+'\"]'); if(!b || b.disabled)return false; b.click();return true;",
                        "args": [label],
                    },
                )

            wait(lambda: press("Начать ручную запись на 15 минут"), "operator starts silent manual recording")
            wait(lambda: api("GET", manual, observer)["active"], "browser start recorded")
            wait(lambda: press("Остановить ручную запись"), "operator stops manual recording")
            wait(lambda: not api("GET", manual, observer)["active"], "browser stop recorded")
        previous_block = blocks[0]["block_id"]
        current = next(
            p for p in api("GET", "recording-policies", admin)["policies"] if p["scope"] == "point"
        )
        c.query(
            "CREATE OR REPLACE FUNCTION bacuri_archive.retention_now_us() RETURNS bigint LANGUAGE sql STABLE AS "
            "$$ SELECT (extract(epoch FROM statement_timestamp()+interval '2 days')*1000000)::bigint $$"
        )
        put(current["id"], "point", point, current["revision"], {**rule, "days": 1})
        assert (
            c.query(f"SELECT count(*) FROM bacuri_archive.retained_blocks WHERE block_id='{previous_block}'")
            == "0"
        )
        wait(lambda: not (root / "blocks" / previous_block).exists(), "per-camera expiry removes local block")
        current = api("GET", "recording-policies/" + current["id"], admin)
        put(current["id"], "point", point, current["revision"], {**rule, "days": 30})
        unrelated = str(uuid.uuid4())
        assert (
            c.query(
                f"SELECT bacuri_archive.point_cutoff('{c.SITE}','{point}')>bacuri_archive.point_cutoff('{c.SITE}','{unrelated}')"
            )
            == "t"
        )
        print(
            "Scoped HTTPS, manual expiry, replay, inheritance, restart, browser editor and per-camera retirement verified",
            flush=True,
        )


if __name__ == "__main__":
    run()
