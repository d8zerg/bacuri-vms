"""Verify point-scoped controls and compare the alarm frame to the recorded H.264 block."""
from pathlib import Path
import base64
from datetime import datetime, timezone
import json
import math
import struct
import subprocess
import time
import uuid

from scenarios import button, c, capture, inspect_alert, js, playback, report, snapshot, wait


def stamp(us):
    return datetime.fromtimestamp(us / 1_000_000, timezone.utc).isoformat(timespec="microseconds").replace("+00:00", "Z")


def navigate(call, user, route):
    name, value = user["Cookie"].split("=", 1)
    cookie = {"name": name, "value": value, "path": "/", "secure": True, "httpOnly": True, "sameSite": "Strict"}
    call("POST", "/cookie", {"cookie": cookie})
    call("POST", "/url", {"url": c.ORIGIN + route})


def compare_frame(call, block, index):
    # Decode the recorder's original block independently of the web player's manifest and seek.
    path = playback.ROOT / "blocks" / block / "media.mp4"
    pipeline = ["gst-launch-1.0", "-q", "filesrc", f"location={path}", "!", "qtdemux", "!", "h264parse",
                "!", "avdec_h264", "!", "videoconvert", "!", "video/x-raw,format=GRAY8", "!", "fdsink", "fd=1"]
    raw = subprocess.check_output(pipeline, timeout=15)
    width, height = 1280, 720
    # Sample native pixels across the whole picture, including the moving ball and the overlay.
    samples = js(call, """const v=document.querySelector('video'), c=document.createElement('canvas');
        c.width=v.videoWidth;c.height=v.videoHeight;const ctx=c.getContext('2d');ctx.drawImage(v,0,0);
        const data=ctx.getImageData(0,0,c.width,c.height).data;const points=[];
        for(let y=0;y<c.height;y+=4)for(let x=0;x<c.width;x+=4){
          const i=y*c.width+x;if(data[i*4]>128)points.push(i);
        }return [c.width,c.height,points];""")
    assert samples[:2] == [width, height], samples[:2]
    mask = set(samples[2])
    assert mask
    size = width * height
    assert len(raw) % size == 0
    errors = []
    for start in range(0, len(raw), size):
        points = {y * width + x for y in range(0, height, 4) for x in range(0, width, 4)
                  if raw[start + y * width + x] > 126}
        errors.append(len(points ^ mask) / len(points | mask))
    assert errors.index(min(errors)) == index and errors[index] < 0.12, (index, errors)
    return round(errors[index], 4)


def map_orientation(call, device, cam, admin, viewer, move):
    base = f"sites/{c.SITE}/maps"
    point = cam["point_id"]
    png = base64.b64encode(Path("web/client/stories/floor.png").read_bytes()).decode()
    code, _, plan = c.http("POST", base + "/plans", {"name":"Orientation", "png":png}, admin)
    assert code == 201, plan
    reference = {"plan_id": plan["id"], "definition": {"anchor": {"x": .5, "y": .5, "lng": 7.4, "lat": 43.73}, "width_m": 40, "bearing": 90}}
    preview = c.http("POST", base + "/georeference/preview", reference, admin)[2]
    assert c.http("PUT", base + "/georeference", {**reference, "token": preview["token"], "choices": {}}, admin)[0] == 204
    placement = {"point_id":point,"plan_id":plan["id"],"x":0.5,"y":0.5}
    assert c.http("PUT", base + "/placement", placement, admin)[0] == 204

    def marker(who=admin):
        return next(p for p in c.http("GET", base, headers=who)[2]["placements"] if p["plan_id"] == plan["id"])
    sample = c.wait_for(lambda: (p if (p:=marker()["camera"]).get("fresh") and not p.get("moving") else None), "absolute PTZ reading")
    ref = sample["pan"]
    config = {"point_id":point, "plan_id":plan["id"], "device_id":cam["id"], "bearing":350,"hfov":80,
              "tele_hfov":20,"space":sample["space"],"reference_pan":ref,"degrees_per_unit":180}
    assert c.http("PUT", base+"/calibration", config, viewer)[0] == 403
    assert c.http("PUT", base+"/calibration", {**config,"reference_pan":ref+1}, admin)[0] == 409
    assert c.http("PUT", base+"/calibration", config, admin)[0] == 204
    assert marker(viewer)["view"]["bearing"] == 350 and "camera" not in marker(viewer)

    def geographic_view():
        return next(p["view"] for p in c.http("GET", base, headers=viewer)[2]["placements"] if p.get("source_plan_id") == plan["id"])

    assert geographic_view()["bearing"] == 80
    navigate(call, viewer, "/maps?plan="+plan["id"]+"&point="+point)
    wait(call, "document.querySelector('.ws-sector')?.dataset.state==='live'", "calibrated live sector")
    assert move(admin,"right")==204
    c.wait_for(lambda: device.feedback()[0]>ref+0.03, "local move")
    assert move(admin,"stop")==204
    final=device.feedback()[0]
    c.wait_for(lambda: abs(marker()["view"]["bearing"]-(350+180*(final-ref))%360)<0.1, "confirmed stop position")
    with device.lock:
        device.position=0.5
        device.hfov=27
    expected=(350+180*(0.5-ref))%360
    c.wait_for(lambda: abs(marker()["view"]["bearing"]-expected)<0.01 and marker()["view"]["hfov"]==27, "external PTZ and optical zoom")
    projected = geographic_view()
    assert projected["state"] == "live" and abs(projected["bearing"] - (expected + 90) % 360) < .01 and projected["hfov"] == 27
    with device.lock:
        device.hfov=None
        device.zoom=0.5
    c.wait_for(lambda: marker()["view"].get("estimated") and marker()["view"]["hfov"]==50, "calibrated zoom fallback")
    device.online=False
    wait(call, "document.querySelector('.ws-sector')?.dataset.state==='stale'", "stale direction on loss")
    assert marker()["view"].get("observed_at") and geographic_view()["state"] == "stale"
    device.report_position=False
    device.online=True
    c.wait_for(lambda: marker()["view"]["state"]=="unknown", "device omitted position")
    details = wait(call, "[...document.querySelectorAll('main summary')].find(s => s.textContent === 'Параметры точки')", "selected point details")
    call("POST", "/element/" + details["element-6066-11e4-a52e-4f735466cecf"] + "/click", {})
    wait(call, "!document.querySelector('.ws-sector') && document.body.innerText.includes('Текущее направление неизвестно')","hidden unsupported sector")
    device.report_position=True
    device.hfov=64.0
    print("PTZ map calibration, command, external motion, zoom, stale and unknown states verified",flush=True)


def advanced_controls(device, cam, admin, viewer):
    root = f"sites/{c.SITE}/points/{cam['point_id']}"

    def get_controls():
        code, _, result = c.http("GET", root + "/ptz", headers=admin)
        assert code == 200, (code, result.get("code"))
        return result

    def command(name, target=""):
        return c.http(
            "POST", root + "/ptz", {"device_id": cam["id"], "command": name, "target": target},
            {**admin, "Idempotency-Key": str(uuid.uuid4())},
        )

    catalog = get_controls()
    commands = {action["command"] for action in catalog["actions"]}
    assert commands >= {
        "focus_near", "focus_far", "focus_stop", "preset_goto", "tour_start", "tour_stop",
        "light_on", "light_off", "wiper_on", "wiper_off", "relay_on", "relay_off",
    }
    assert [item["token"] for item in catalog["inputs"]] == ["contact"]
    assert command("preset_goto", "unknown")[0] == 422
    for item in catalog["actions"]:
        name = item["command"]
        code, _, response = command(name, item["target"])
        assert code == 204, (name, code, response)
        if name.startswith("focus_"):
            assert not device.focus
        elif name == "preset_goto":
            assert device.preset == "door" and device.feedback()[0] == 0.2
        elif name.startswith("tour_"):
            assert device.touring == (name == "tour_start")
        elif name.startswith("wiper_"):
            assert device.wiper == (name == "wiper_on")
        elif name.startswith("light_"):
            assert device.light == (name == "light_on")
        elif name.startswith("relay_"):
            active = name == "relay_on"
            assert device.relay == active
            c.wait_for(lambda: get_controls()["inputs"][0].get("active") == active, "digital input feedback")

    assert any(row["outcome"] == "acknowledged" and row["command"] == "relay_off"
               for row in get_controls()["history"])
    assert command("tour_start", "round")[0] == 204
    c.wait_for(lambda: not device.touring, "device-side patrol duration")
    device.miss_focus_stop = True
    code, _, reply = command("focus_near")
    assert code == 502 and reply["code"] == "camera_ptz_unknown"
    c.wait_for(lambda: not device.focus, "deferred focus stop after acknowledgement loss")
    print("Preset, bounded patrol, focus pulse and stop, auxiliary devices, relay feedback and command journal verified",
          flush=True)
    return {"commands": sorted(commands), "input_count": len(catalog["inputs"]), **check_audio(cam, admin, viewer)}


def check_audio(cam, admin, viewer):
    point = cam["point_id"]
    root = f"sites/{c.SITE}/points/{point}/audio"

    def audio(action, who=admin, **fields):
        return c.http("POST", root, {"mode": action, "device_id": cam["id"], **fields}, who)

    def recorded():
        return c.query("SELECT manifest->'audio'->>'start_us' FROM bacuri_archive.blocks "
                       f"WHERE point_id='{point}' AND manifest ? 'audio' ORDER BY begin_utc_us DESC LIMIT 1")

    for action in ("listen", "record", "talk"):
        assert audio(action, viewer)[0] == 403
    code, _, data = audio("listen")
    assert code == 200 and len(base64.b64decode(data["pcm"])) > 80, (code, data.get("code"))
    assert audio("record")[0] == 200
    time.sleep(0.9)
    assert audio("record")[0] == 200
    time.sleep(0.8)
    assert audio("record_stop")[0] == 200
    at = int(c.wait_for(recorded, "registered sound attached to recorded video"))
    code, _, clip = audio("archive", at_us=at)
    assert code == 200 and len(base64.b64decode(clip["pcm"])) > 100, (code, clip.get("code"))
    assert audio("archive", viewer, at_us=at)[0] == 403

    speech = str(uuid.uuid4())
    samples = [round(3200 * math.sin(2 * math.pi * 800 * i / 8000)) for i in range(1600)]
    packet = base64.b64encode(struct.pack("<1600h", *samples)).decode()
    code, _, reply = audio("talk", session=speech, sequence=0, pcm=packet)
    assert code == 200, (code, reply)
    assert audio("talk", session=speech, sequence=0, pcm=packet)[0] == 409
    speaker = Path(c.ROOT) / "speaker.pcm"
    c.wait_for(lambda: speaker.exists() and speaker.stat().st_size >= 3200, "decoded speech on the camera output")
    actual = struct.unpack("<1600h", speaker.read_bytes()[-3200:])
    error = sum(abs(a - b) for a, b in zip(actual, samples)) / len(samples)
    assert error < 80, error
    assert audio("mute", session=speech)[0] == 200
    end = speaker.stat().st_size
    assert audio("listen")[0] == 200 and speaker.stat().st_size == end
    assert audio("talk", session=speech, sequence=1, pcm=packet)[0] == 409

    def grant(method, grant_id, action):
        base = f"sites/{c.SITE}/"
        _, _, users = c.http("GET", base + "users", headers=admin)
        user = next(row["id"] for row in users["data"] if row["username"] == "viewer")
        data = {"user_id": user, "point_id": point, "action": action} if method == "PUT" else None
        headers = {**admin, "If-Match": '"' + users["revision"] + '"'}
        code, _, result = c.http(method, base + "grants/" + grant_id, data, headers)
        assert code == 200, (code, result)

    listen_grant, talk_grant = str(uuid.uuid4()), str(uuid.uuid4())
    grant("PUT", listen_grant, "audio")
    grant("PUT", talk_grant, "audio_talk")
    other = str(uuid.uuid4())
    assert audio("talk", viewer, session=other, sequence=0, pcm=packet)[0] == 200
    grant("DELETE", talk_grant, "audio_talk")
    assert audio("talk", viewer, session=other, sequence=1, pcm=packet)[0] == 403
    assert audio("mute", viewer, session=other)[0] == 200
    assert audio("listen", viewer)[0] == 200
    grant("DELETE", listen_grant, "audio")
    print("Separate audio authorization, input samples, persisted archive sound, backchannel and microphone stop verified",
          flush=True)
    return {"archive_samples": len(base64.b64decode(clip["pcm"])) // 2,
            "speaker_samples": len(actual), "speaker_mean_absolute_error": error}


def exercise(call, device, cam, admin):
    point = cam["point_id"]
    viewer = c.login("viewer")
    root = f"sites/{c.SITE}/points"

    def move(who, direction, request_key=None):
        headers = {**who, "Idempotency-Key": request_key or str(uuid.uuid4())}
        command = {"device_id": cam["id"], "command": direction}
        return c.http("POST", root + f"/{point}/ptz", command, headers)[0]

    assert move(viewer, "right") == 404 and not device.feedback()[2]
    subprocess.run([c.CORE, "grant-point", "viewer", point], check=True)
    assert move({**viewer, "X-CSRF-Token": "invalid"}, "right") == 403
    points = c.http("GET", root, headers=viewer)[2]
    visible, = [row for row in points if row["id"] == point]
    commands = visible["ptz"]["commands"]
    assert visible["ptz"]["device_id"] == cam["id"] and set(commands) == {"left", "right", "zoom_in", "zoom_out", "stop"}
    assert move(viewer, "up") == 422 and not device.feedback()[2]
    navigate(call, viewer, "/monitor?point=" + point)
    button(call, "Вправо")
    c.wait_for(lambda: device.feedback()[0] > 0.04, "observed camera movement")
    button(call, "Остановить")
    c.wait_for(lambda: device.stops > 0 and not device.feedback()[2], "explicit stop")
    stopped = device.feedback()[0]
    time.sleep(0.12)
    assert device.feedback()[0] == stopped
    assert not js(call, "return [...document.querySelectorAll('.ws-ptz button')].some(b=>b.textContent==='Вверх')")

    button(call, "Приблизить")
    def zoom_position():
        device.feedback()
        return device.zoom
    c.wait_for(lambda: zoom_position() > 0.03, "zoom position changed")
    previous_stops = device.stops
    button(call, "Остановить")
    c.wait_for(lambda: device.stops > previous_stops and device.zoom_speed == 0, "camera stopped its zoom motor")
    assert device.position == stopped
    assert move(admin, "zoom_out") == 204
    c.wait_for(lambda: zoom_position() == 0 and device.zoom_speed == 0, "bounded zoom returned to wide")
    tested_controls = advanced_controls(device,cam,admin,viewer)
    request_key = str(uuid.uuid4())
    assert move(admin, "left", request_key) == 204 and device.feedback()[2]
    deadline = device.deadline
    assert move(admin, "left", request_key) == 204 and device.deadline == deadline
    assert move(admin, "right", request_key) == 409 and device.deadline == deadline
    assert move(viewer, "right") == 409 and device.deadline == deadline
    assert move(admin, "stop") == 204

    request_key = str(uuid.uuid4())
    device.lose_move_reply = True
    assert move(viewer, "left", request_key) == 502 and device.feedback()[2]
    deadline = device.deadline
    assert move(viewer, "left", request_key) == 502 and device.deadline == deadline
    code, _, audit = c.http("POST", f"sites/{c.SITE}/alert-audit/search", {"query":"left"}, admin)
    assert code == 200 and any(row["action"] == "camera_control" and row["outcome"] == "unknown" for row in audit["items"])
    device.online = False
    code, _, saved = c.http("GET", root + "/" + point + "/ptz", headers=admin)
    assert code == 200 and any(row["outcome"] == "unknown" for row in saved["history"])
    assert move(viewer, "stop") in (502, 504)
    c.wait_for(lambda: not device.feedback()[2], "device-side timeout while offline")
    c.wait_for(lambda: device.subscriptions > 0, "PullPoint allocated")
    before = device.subscriptions
    time.sleep(2.4)
    device.initial_lease_seconds = 3
    device.online = True
    c.wait_for(lambda: device.subscriptions > before, "PullPoint reconnect")
    c.wait_for(lambda: device.renewals > 0, "subscription renewal")

    def recorded():
        row = c.query("SELECT block_id,begin_utc_us FROM bacuri_archive.blocks "
                      f"WHERE point_id='{point}' AND binding_revision=1 AND time_basis='source_clock' "
                      "AND end_utc_us-begin_utc_us>=900000 AND uncertainty_us<200000 "
                      "ORDER BY begin_utc_us DESC LIMIT 1")
        return row.split("|") if row else None

    block, begin = c.wait_for(recorded, "recorded camera block with RTCP clock")
    index = 15
    us = int(begin) + index * 40_000 + 15_000
    instant = stamp(us)
    device.motion(stamp(us - 3_600_000_000))
    device.motion(instant, "unrelated-video-source")
    device.motion(stamp(us - 2000), state="false")
    device.motion(stamp(us - 1000), operation="Initialized")
    device.motion(instant)
    c.wait_for(lambda: snapshot(viewer)["items"], "ONVIF motion journal")
    delivered = device.pulled
    device.motion(instant)
    c.wait_for(lambda: device.pulled > delivered, "redelivered PullPoint message")
    last_poll = device.polls
    c.wait_for(lambda: device.polls > last_poll, "adapter committed redelivery")
    item, = snapshot(viewer)["items"]
    assert item["point_id"] == point and datetime.fromisoformat(item["observed_at"]) == datetime.fromisoformat(instant), item
    assert c.query("SELECT count(*) FROM bacuri_events.observations WHERE origin='onvif'") == "1"
    link = c.http("GET", f"sites/{c.SITE}/alerts/{item['id']}/archive", headers=viewer)[2]
    assert link["state"] == "ready" and link["position"] == item["observed_at"], link
    node = wait(call, "document.getElementById('alert-'+arguments[0])", "WSS motion alert", item["id"])
    call("POST", "/element/" + node["element-6066-11e4-a52e-4f735466cecf"] + "/click", {})
    wait(call, "document.body.innerText.includes('Кадр записи получен')", "alarm archive frame")
    difference = compare_frame(call, block, index)
    capture(call, "chrome", "onvif-motion")
    position = js(call, "return document.querySelector('.ws-transport time').dateTime")
    assert datetime.fromisoformat(position) == datetime.fromisoformat(instant)
    inspect_alert(call, "chrome", item["id"], None, position)
    status, _, card = c.http("GET", f"sites/{c.SITE}/alerts/{item['id']}/work", headers=viewer)
    assert status == 200 and card["state"] == "closed" and card["owner"] == "viewer"
    assert [e["action"] for e in card["history"]] == ["close", "record_result", "claim"]
    observation = c.query("SELECT observation_id FROM bacuri_events.observations WHERE origin='onvif'")
    evidence = {
        "model": cam["model"], "firmware": cam["firmware"], "video_source": "videotestsrc", "device_id": cam["id"],
        "point_id": point, "binding_revision": cam["binding_revision"], "ptz": commands,
        "camera_controls": tested_controls,
        "explicit_stop_position": stopped, "timeout_seconds": 1, "lost_connection": True,
        "lost_move_reply_replayed_without_resending": True, "pullpoint_reconnected": True, "subscription_renewed": True,
        "foreign_point_denied": True, "motion_deliveries": 2, "observations": [observation], "alerts": [item["id"]],
        "work_state": card["state"], "owner": card["owner"], "audit_events": [e["id"] for e in card["history"]],
        "block_id": block, "position": instant, "frame_index": index, "frame_mask_difference": difference,
    }
    (report / "camera-operations.json").write_text(json.dumps(evidence, indent=2) + "\n")
    map_orientation(call,device,cam,admin,viewer,move)
    navigate(call, admin, "/cameras?setup=manual")
