"""Exercise node probe authorization, restart, cancellation and repeatability through HTTPS."""

from contextlib import ExitStack
import os
from pathlib import Path
import subprocess
import sys
import time
import uuid

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "control"))
import registry as control


def completed(read):
    deadline = time.monotonic() + 240
    while time.monotonic() < deadline:
        status = read()
        job = status["job"]
        assert not status["stale"] and job["state"] not in ("cancelled", "failed"), job
        if job["state"] == "completed":
            assert all(step["state"] == "completed" and step["completed"] == step["total"] for step in job["steps"])
            return status["result"]
        time.sleep(0.2)
    raise AssertionError("Node probe did not finish: " + str(read()["job"]))


def check_measurements(result, expected):
    devices = {d["id"]: d for d in result["devices"]}
    backends = {d["backend"] for d in devices.values()}
    assert {"cpu", expected} <= backends, devices
    if expected == "cpu":
        assert backends == {"cpu"}, devices
    for row in result["measurements"]:
        device = devices[row["device_id"]]
        operation, codec = row["operation"], row["codec"]
        if operation == "openvino":
            if os.environ.get("BACURI_OPENVINO_LIBRARY"):
                assert row["state"] == "measured", row
                assert any(target["state"] == "measured" and target["frames"] == 32 for target in row["targets"]), row
            continue
        if operation in ("cpu", "cuda"):
            assert row["state"] == "measured" and row["frames"] == 32, row
            continue
        codecs = device["codecs"]
        supported = codec + ("_enc" if operation in ("sessions", "encode") else "_dec") in codecs
        if operation in ("mask", "export", "profile"):
            supported = supported and "h264_enc" in codecs
        assert row["state"] == ("measured" if supported else "unsupported"), row
    if expected != "cpu":
        assert any(d["backend"] == expected and d["codecs"] for d in devices.values()), devices


def compare_budgets(first, last):
    def index(result):
        return {(m["device_id"], m["operation"], m["codec"]): m for m in result["measurements"]}

    before, after = index(first), index(last)
    assert before.keys() == after.keys()
    for key, a in before.items():
        b = after[key]
        assert a["state"] == b["state"], (key, a, b)
        if key[1] in ("encode", "decode") and a["state"] == "measured":
            for measurement in (a, b):
                trials = measurement["elapsed_samples_us"]
                assert len(trials) == 3 and sorted(trials)[1] == measurement["elapsed_us"], measurement
        if "verified_sessions" in a:
            assert a["verified_sessions"] == b["verified_sessions"], (key, a, b)
            print(key[0], key[2], "concurrent encoders:", a["verified_sessions"])
        if "verified_25fps_streams" in a and key[1] in ("encode", "decode"):
            assert a["verified_25fps_streams"] == b["verified_25fps_streams"], (key, a, b)


def main():
    node = os.environ["BACURI_NODE_ID"]
    expected = os.environ.get("BACURI_PROBE_BACKEND", "cpu")
    route = f"sites/{control.SITE}/nodes/{node}/acceleration/probe"
    for name, role in (("admin", "administrator"), ("viewer", "operator")):
        subprocess.run([control.CORE, "create-user", name, role], input=control.PASSWORD + "\n", text=True, check=True)
    with ExitStack() as cleanup:
        core = subprocess.Popen([control.CORE, "serve"])
        cleanup.callback(control.stop, core)

        def launch():
            child = subprocess.Popen([control.BIN / "bacuri-vms-media"])
            cleanup.callback(control.stop, child)
            return child

        media = launch()
        control.wait_for(lambda: control.http("GET", "session")[0] == 401, "core HTTPS")
        admin, viewer = control.login("admin"), control.login("viewer")

        def read():
            code, _, status = control.http("GET", route, headers=admin)
            assert code == 200, (code, status)
            return status

        for method, path in (("GET", route), ("POST", route), ("DELETE", route + "/" + str(uuid.uuid4()))):
            assert control.http(method, path)[0] == 401
            assert control.http(method, path, headers=viewer)[0] == 403
            if method != "GET":
                assert control.http(method, path, headers={**admin, "X-CSRF-Token": ""})[0] == 403
        assert control.http("GET", route.replace(node, str(uuid.uuid4())), headers=admin)[0] == 404

        control.wait_for(lambda: control.http("GET", route, headers=admin)[0] == 200, "media node HTTPS")
        assert read()["job"]["reason"] == "startup"
        first = completed(read)
        check_measurements(first, expected)
        code, _, second = control.http("POST", route, {"reason": "prepare_gpu"}, admin)
        assert code == 202 and second["job"]["reason"] == "prepare_gpu" and second["result"] == first
        job = second["job"]["id"]
        assert control.http("POST", route, headers=admin)[2]["job"]["id"] == job
        assert control.http("DELETE", route + "/" + str(uuid.uuid4()), headers=admin)[0] == 404
        assert control.http("DELETE", route + "/" + job, headers=admin)[2]["job"]["state"] == "cancelled"
        assert read()["result"] == first

        control.stop(media)
        launch()
        control.wait_for(lambda: control.http("GET", route, headers=admin)[0] == 200, "restarted node")
        status = read()
        assert status["job"]["reason"] == "startup" and status["result"] == first
        last = completed(read)
        assert first["fingerprint"] == last["fingerprint"]
        compare_budgets(first, last)
        print(f"{expected}: {len(last['measurements'])} measurements, repeated codec budgets, authorization, cancellation and restart verified")


if __name__ == "__main__":
    main()

