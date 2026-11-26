"""Exercise negotiated streams, stored representations and simultaneous operator grids."""

from contextlib import ExitStack
from datetime import datetime, timezone
from http.client import HTTPSConnection
import base64
import select
from pathlib import Path
import os
import subprocess
import sys
import uuid

from streaming import archive_blocks, browser_session, control, coverage

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "archive"))
from playback import binary

SITE = f"sites/{control.SITE}"


def request(method, path, credentials, body=None, status=200, **headers):
    actual, _, response = control.http(method, path, body, {**credentials, **headers})
    assert actual == status, (method, path, status, actual, response)
    return response


def session(point, credentials, body, status=201):
    return request(
        "POST",
        SITE + f"/points/{point}/media-sessions",
        credentials,
        body,
        status,
        **{"Idempotency-Key": str(uuid.uuid4())},
    )


def close(ticket, credentials):
    request("DELETE", "media-sessions/" + ticket["id"], credentials, status=204)


def layout_checks(points, admin, viewer):
    path = SITE + "/monitor-layouts"
    draft = dict(points=points, name="Shift", shared=True, revision="0")
    shared = str(uuid.uuid4())
    request("PUT", path + "/" + shared, viewer, draft, 403)
    request("PUT", path + "/" + shared, admin, draft)
    request("PUT", path + "/" + shared, admin, draft, 409)
    (entry,) = request("GET", path, viewer)
    assert entry["points"] == [None] * len(points) and not entry["editable"], entry
    for point in points:
        subprocess.run([control.CORE, "grant-point", "viewer", point], check=True)
    (entry,) = request("GET", path, viewer)
    assert entry["points"] == points, entry
    private = path + "/" + str(uuid.uuid4())
    request("PUT", private, viewer, {**draft, "shared": False})
    assert len(request("GET", path, admin)) == 1
    request("DELETE", private, viewer, status=409, **{"If-Match": '"0"'})
    request("DELETE", private, viewer, status=204, **{"If-Match": '"1"'})
    return shared


def representation(ticket, credentials, source, codec):
    path = "media-sessions/" + ticket["id"]
    (part,) = request("GET", path + "/manifest", credentials)["parts"]
    assert (part["source_codec"], part["codec"]) == (source, codec), part
    fragments = []
    for field in ("init_id", "media_id"):
        status, data = binary(path + "/parts/" + part[field], credentials)
        assert status == 200, (field, status, data[:200])
        fragments.append(data)
    assert (b"hvcC" if codec == "h265" else b"avcC") in fragments[0]
    return b"".join(fragments)


def gray_frames(data):
    path = control.ROOT / "delivered.mp4"
    path.write_bytes(data)
    return subprocess.check_output(
        [
            "gst-launch-1.0",
            "-q",
            "filesrc",
            f"location={path}",
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
        ],
        timeout=15,
    )


def profiles(point, admin):
    control.wait_for(lambda: len(archive_blocks(point)) >= 2, "negotiated and recorded HEVC source")
    main = session(point, admin, {"mode": "live", "quality": "main", "hevc": True})
    assert main["video"]["codec"] == "h265" and main["video"]["profile"] == "main", main
    close(main, admin)
    sub = session(point, admin, {"mode": "live", "quality": "main", "hevc": False})
    assert sub["video"]["profile"] == "secondary" and not sub["video"]["converted"], sub
    close(sub, admin)
    denied = session(point, admin, {"mode": "live", "bandwidth_kbps": 1, "hevc": False}, 429)
    assert denied["code"] == "video_bandwidth_limit", denied
    block = control.wait_for(lambda: next(iter(archive_blocks(point)), None), "recorded HEVC")
    interval = dict(
        zip(
            ("start", "end"),
            [
                datetime.fromtimestamp(us / 1e6, timezone.utc)
                .isoformat(timespec="microseconds")
                .replace("+00:00", "Z")
                for us in block[:2]
            ],
        )
    )
    body = {"mode": "archive", "interval": interval}
    original = session(point, admin, {**body, "hevc": True})
    data = representation(original, admin, "h265", "h265")
    reference = gray_frames(data)
    assert max(reference) > 195
    close(original, admin)
    copy = session(point, admin, {**body, "hevc": False})
    assert max(gray_frames(representation(copy, admin, "h265", "h264"))) > 195
    assert session(point, admin, {**body, "hevc": False}, 429)["code"] == "video_transcoder_limit"
    close(copy, admin)

    def policy(rectangles):
        rev = request("GET", SITE + "/users", admin)["revision"]
        request(
            "PUT",
            SITE + "/privacy-policies/" + point,
            admin,
            dict(live=True, archive=True, thumbnail=True, export=True, regions=rectangles),
            **{"If-Match": '"' + rev + '"'},
        )

    width, height = main["video"]["width"], main["video"]["height"]
    bright = [offset % width for offset, gray in enumerate(reference[: width * height]) if gray > 195]
    split = (min(bright) + max(bright)) // 2
    policy([dict(left=0, top=0, right=split / width, bottom=1)])
    partial = session(point, admin, {**body, "hevc": False, "accept_masked": True})
    frame = gray_frames(representation(partial, admin, "h265", "h264"))[: width * height]
    assert max(max(frame[y * width : y * width + split - 3]) for y in range(height)) <= 20
    assert max(max(frame[y * width + split + 3 : (y + 1) * width]) for y in range(height)) > 195
    close(partial, admin)
    policy([dict(left=0, top=0, right=1, bottom=1)])
    live_mask = session(
        point, admin, {"mode": "live", "quality": "main", "hevc": True, "accept_masked": True}
    )
    assert live_mask["video"]["reason"] == "privacy_mask" and live_mask["video"]["codec"] == "h264", live_mask
    try:
        with browser_session("chrome", admin, "/") as (call, _):
            visible = call(
                "POST",
                "/execute/async",
                {
                    "script": Path("tests/live/compatible-frame.js").read_text(),
                    "args": [live_mask["id"], admin["X-CSRF-Token"], "h264"],
                },
            )
            assert (
                visible.get("width") == width and visible["height"] == height and visible["maximum"] <= 18
            ), visible
            print("Browser received masked live pixels", visible, flush=True)
    finally:
        close(live_mask, admin)
    masked = session(point, admin, {**body, "hevc": True, "accept_masked": True})
    pixels = gray_frames(representation(masked, admin, "h265", "h264"))
    assert len(pixels) == len(reference) and max(pixels) <= 18, (len(reference), len(pixels), max(pixels))
    policy([])
    request("GET", "media-sessions/" + masked["id"] + "/manifest", admin, status=404)
    close(masked, admin)
    session(point, admin, {"mode": "live", "hevc": True}, 404)
    print(
        "Verified original HEVC, archived H.264 copy, decoded privacy pixels, revocation and quotas",
        flush=True,
    )


def native_webrtc(point, credentials):
    ticket = session(point, credentials, {"mode": "live", "hevc": True, "quality": "main"})
    assert ticket["video"]["codec"] == "h265" and not ticket["video"]["converted"], ticket
    with subprocess.Popen(
        [control.BIN / "tests/fixtures/bacuri-vms-hevc-receiver"],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
    ) as peer:
        try:
            assert select.select([peer.stdout], [], [], 8)[0], "HEVC offer timeout"
            offer = base64.b64decode(peer.stdout.readline(), validate=False)
            assert b"H265/90000" in offer
            http = HTTPSConnection("localhost", 8443, context=control.TLS, timeout=8)
            try:
                http.request(
                    "POST",
                    "/api/v1/media-sessions/" + ticket["id"] + "/offer",
                    offer,
                    headers={**credentials, "Origin": control.ORIGIN, "Content-Type": "application/sdp"},
                )
                reply = http.getresponse()
                answer = reply.read()
                assert reply.status == 200 and b"H265/90000" in answer, reply.status
            finally:
                http.close()
            dimensions, _ = peer.communicate(base64.b64encode(answer) + b"\n", timeout=12)
            assert peer.returncode == 0 and dimensions.split() == [b"640", b"360"], (
                peer.returncode,
                dimensions,
            )
            print("Native HEVC WebRTC decoded 640x360", flush=True)
        finally:
            if peer.poll() is None:
                peer.kill()
            close(ticket, credentials)


def check_native_browser(browser, call, point, credentials):
    caps = call(
        "POST",
        "/execute/sync",
        {
            "script": """
        return {rtc:RTCRtpReceiver.getCapabilities('video').codecs.some(c=>c.mimeType.toLowerCase()==='video/h265'),
            mse: MediaSource.isTypeSupported('video/mp4; codecs="hvc1.1.6.L93.B0"')};
    """,
            "args": [],
        },
    )
    print(browser, "native HEVC", caps, flush=True)
    if caps["rtc"]:
        ticket = session(point, credentials, {"mode": "live", "hevc": True, "quality": "main"})
        try:
            frame = call(
                "POST",
                "/execute/async",
                {
                    "script": Path("tests/live/compatible-frame.js").read_text(),
                    "args": [ticket["id"], credentials["X-CSRF-Token"], "h265"],
                },
            )
            assert frame.get("width") == 640 and frame["height"] == 360, frame
            print(browser, "native HEVC WebRTC", frame, flush=True)
        finally:
            close(ticket, credentials)
    if caps["mse"]:
        start, end, *_ = archive_blocks(point)[0]
        bounds = {
            k: datetime.fromtimestamp(us / 1e6, timezone.utc)
            .isoformat(timespec="microseconds")
            .replace("+00:00", "Z")
            for k, us in [("start", start), ("end", end)]
        }
        ticket = session(point, credentials, {"mode": "archive", "interval": bounds, "hevc": True})
        try:
            result = call(
                "POST",
                "/execute/async",
                {
                    "script": Path("tests/live/native-archive.js").read_text(),
                    "args": [ticket["id"]],
                },
            )
            assert result == [640, 360], result
            print(browser, "native HEVC archive", result, flush=True)
        finally:
            close(ticket, credentials)


def grid_command(call, action, count, width=None):
    script = Path("tests/live/monitor-grid.js").read_text()
    result = call("POST", "/execute/async", {"script": script, "args": [action, count, width]})
    assert "error" not in result, result
    print(action, result, flush=True)
    return result


def run():
    with ExitStack() as stack:

        def start(args, **kwargs):
            proc = subprocess.Popen(args, **kwargs)
            stack.callback(control.stop, proc)
            return proc

        for name, role in [("admin", "administrator"), ("viewer", "operator")]:
            subprocess.run(
                [control.CORE, "create-user", name, role],
                input=control.PASSWORD + "\n",
                text=True,
                check=True,
            )
        start([control.CORE, "serve"])
        control.wait_for(lambda: control.http("GET", "session")[0] == 401, "core")
        admin, viewer = control.login("admin"), control.login("viewer")
        recorder = start(
            [control.RECORDER], env={**os.environ, "BACURI_KEY_FILE": str(control.ROOT / "recorder.key")}
        )
        delivery = start([control.BIN / "bacuri-vms-media"])
        base = "rtsp://" + os.environ["BACURI_TEST_CAMERA_HOST"] + ":8554"
        points = []
        for index in range(5):
            camera = request(
                "POST",
                SITE + "/cameras",
                admin,
                dict(
                    name=f"Channel {index + 1}",
                    main_url=base + ("/hevc" if index == 4 else f"/channel{index + 1}/main"),
                    secondary_url=base + ("/sub" if index == 4 else f"/channel{index + 1}/sub"),
                ),
                201,
                **{"Idempotency-Key": str(uuid.uuid4())},
            )
            points.append(camera["point_id"])
        shared = layout_checks(points[:4], admin, viewer)
        profiles(points[4], admin)
        hevc = request(
            "POST",
            SITE + "/cameras",
            admin,
            dict(name="HEVC pair", main_url=base + "/hevc", secondary_url=base + "/hevc-sub"),
            201,
            **{"Idempotency-Key": str(uuid.uuid4())},
        )["point_id"]
        control.wait_for(lambda: len(archive_blocks(hevc)) >= 2, "both HEVC profiles")
        copy = session(hevc, admin, {"mode": "live", "quality": "secondary", "hevc": False})
        assert (
            copy["video"]["converted"]
            and copy["video"]["source_codec"] == "h265"
            and copy["video"]["width"] == 320
        ), copy
        assert session(hevc, admin, {"mode": "live", "hevc": False}, 429)["code"] == "video_transcoder_limit"
        with browser_session("chrome", admin, "/") as (call, _):
            frame = call(
                "POST",
                "/execute/async",
                {
                    "script": Path("tests/live/compatible-frame.js").read_text(),
                    "args": [copy["id"], admin["X-CSRF-Token"], "h264"],
                },
            )
            assert frame.get("width") == 320 and frame["height"] == 180, frame
            print("HEVC to H.264 WebRTC", frame, flush=True)
        close(copy, admin)
        native_webrtc(hevc, admin)
        begin = len(archive_blocks(points[0]))
        url = "/monitor?grid=1&layout=" + shared + "&site=" + control.SITE
        evidence = os.environ.get("BACURI_WORKSPACE_RESULTS")
        with (
            browser_session(
                "chrome", admin, url, workspace_coverage=Path(evidence) / "chrome" if evidence else None
            ) as (chrome, _),
            browser_session("firefox", viewer, url + "&wall=1") as (firefox, _),
        ):
            for call in [chrome, firefox]:
                call("POST", "/window/rect", {"width": 1280, "height": 900})
                grid_command(call, "grid", 4)
            grid_command(chrome, "expand", 1, 1280)
            grid_command(chrome, "main", 1, 1280)
            grid_command(chrome, "secondary", 1, 640)
            grid_command(chrome, "restore", 4)
            grid_command(firefox, "grid", 4)
            subprocess.run([control.CORE, "grant-point", "viewer", hevc], check=True)
            check_native_browser("Chrome", chrome, hevc, admin)
            check_native_browser("Firefox", firefox, hevc, viewer)
            for name, proc in [("recorder", recorder), ("media", delivery)]:
                print(
                    name,
                    next(
                        line
                        for line in Path(f"/proc/{proc.pid}/status").read_text().splitlines()
                        if line.startswith("VmRSS")
                    ),
                    flush=True,
                )
        control.wait_for(lambda: len(archive_blocks(points[0])) >= begin + 3, "recording across layouts")
        assert coverage(points[0], archive_blocks(points[0])[begin:]) == (0, 0, 0, 0)


if __name__ == "__main__":
    run()
