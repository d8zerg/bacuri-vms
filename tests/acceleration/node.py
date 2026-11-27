"""Verify node probes, persisted acceleration modes, budget decisions and audit through HTTPS."""

from contextlib import ExitStack
import os
import json
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import subprocess
import sys
import time
import uuid

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "control"))
import registry as control
from runtime import check_scheduler


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


def check_modes(route, admin, viewer, expected):
    def request(method, path, body=None, user=admin, revision=None):
        headers = user if revision is None else {**user, "If-Match": f'"{revision}"'}
        return control.http(method, path, body, headers)

    cpu, automatic = {"mode": "cpu_only"}, {"mode": "auto"}
    assert request("GET", route, user=viewer)[2]["mode"] == "cpu_only"
    assert request("POST", route + "/preview", cpu, viewer)[0] == 403
    assert request("PUT", route, cpu, viewer, 0)[0] == 403
    assert request("PUT", route, cpu)[0] == 428
    assert request("PUT", route, cpu, revision=9)[0] == 412
    assert request("PUT", route, cpu, {**admin, "X-CSRF-Token": ""}, 0)[0] == 403
    for bad in ({"mode": "auto", "gpu_tasks": []}, {"mode": "manual", "gpu_tasks": ["bad"]},
                {"mode": "manual", "gpu_tasks": ["export", "export"]}):
        assert request("POST", route + "/preview", bad)[0] == 400
    code, _, preview = request("POST", route + "/preview", automatic)
    if expected == "cpu":
        assert (code, preview["code"]) == (409, "acceleration_device_unavailable")
        assert request("PUT", route, automatic, revision=0)[2]["code"] == preview["code"]
        return 0

    assert code == 200 and preview["mode"] == "auto" and preview["applicable"], preview
    assigned = [row["task"] for row in preview["decisions"] if row["device_id"] != "cpu"]
    assert assigned and len(preview["decisions"]) == 7 and preview["gpu_tasks"] == [], preview
    assert request("GET", route)[2]["mode"] == "cpu_only"
    manual = {"mode": "manual", "gpu_tasks": [assigned[0]]}
    with ThreadPoolExecutor(max_workers=2) as pool:
        writes = list(pool.map(lambda plan: request("PUT", route, plan, revision=0), [automatic, manual]))
    assert sorted(row[0] for row in writes) == [200, 412], writes
    winner = next(row[2] for row in writes if row[0] == 200)
    other = automatic if winner["mode"] == "manual" else manual
    status, _, saved = request("PUT", route, other, revision=1)
    assert status == 200 and saved["revision"] == 2 and saved["mode"] == other["mode"], saved
    assert request("PUT", route, other, revision=2)[2]["revision"] == 2
    all_tasks = {"mode": "manual", "gpu_tasks": [row["task"] for row in preview["decisions"]]}
    proposal = request("POST", route + "/preview", all_tasks)[2]
    if proposal["rejected_tasks"]:
        code, _, failure = request("PUT", route, all_tasks, revision=2)
        assert (code, failure["code"], failure["rejected_tasks"]) == (
            409, "acceleration_budget_exceeded", proposal["rejected_tasks"])
        assert request("GET", route)[2]["revision"] == 2
        print("manual preview and apply rejected:", proposal["rejected_tasks"], flush=True)
    history = control.http("POST", f"sites/{control.SITE}/alert-audit/search", {"query": "acceleration_changed"}, admin)
    assert history[0] == 200, history
    entries = [e for e in history[2]["items"] if e["action"] == "node_acceleration"]
    assert len(entries) == 2 and all(e["initiator"] == "admin" for e in entries), entries
    changes = sorted((e["details"] for e in entries), key=lambda e: e["revision"])
    assert changes[0]["previous_mode"] == "cpu_only" and changes[0]["previous_tasks"] == []
    assert changes[0]["mode"] == changes[1]["previous_mode"] == winner["mode"]
    assert changes[1]["previous_tasks"] == winner["gpu_tasks"] and changes[1]["tasks"] == saved["gpu_tasks"]
    assert changes[1]["node_id"] == saved["node_id"] and changes[1]["mode"] == saved["mode"]
    manual_empty = {"mode": "manual", "gpu_tasks": []}
    saved_empty = request("PUT", route, manual_empty, revision=2)[2]
    assert saved_empty["mode"] == "manual" and saved_empty["gpu_tasks"] == [] and saved_empty["revision"] == 3
    updated = request("PUT", route, manual, revision=3)[2]
    assert updated["gpu_tasks"] == manual["gpu_tasks"] and updated["revision"] == 4
    history = control.http("POST", f"sites/{control.SITE}/alert-audit/search", {"query": "acceleration_changed"}, admin)[2]
    change = next(e["details"] for e in history["items"] if e["details"]["revision"] == 4)
    assert change["mode"] == change["previous_mode"] == "manual"
    assert change["previous_tasks"] == [] and change["tasks"] == manual["gpu_tasks"]
    return 4


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
        for headers, is_admin in ((admin, True), (viewer, False)):
            session = control.http("GET", "session", headers=headers)[2]
            membership = next(row for row in session["site_roles"] if row["site_id"] == control.SITE)
            assert membership.get("node_id") == (node if is_admin else None)


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
        settings = route.removesuffix("/probe")
        assert control.http("GET", settings, headers=viewer)[2]["revision"] == 0
        assert control.http("PUT", settings, {"mode": "auto"}, {**admin, "If-Match": '"0"'})[2]["code"] == "acceleration_probe_required"
        first = completed(read)
        check_measurements(first, expected)
        revision = check_modes(settings, admin, viewer, expected)
        code, _, second = control.http("POST", route, {"reason": "prepare_gpu"}, admin)
        assert code == 202 and second["job"]["reason"] == "prepare_gpu" and second["result"] == first
        job = second["job"]["id"]
        assert control.http("POST", route, headers=admin)[2]["job"]["id"] == job
        assert control.http("DELETE", route + "/" + str(uuid.uuid4()), headers=admin)[0] == 404
        assert control.http("DELETE", route + "/" + job, headers=admin)[2]["job"]["state"] == "cancelled"
        assert read()["result"] == first

        control.stop(media)
        media = launch()
        control.wait_for(lambda: control.http("GET", route, headers=admin)[0] == 200, "restarted node")
        status = read()
        assert status["job"]["reason"] == "startup" and status["result"] == first
        last = completed(read)
        assert first["fingerprint"] == last["fingerprint"]
        compare_budgets(first, last)
        revision = check_scheduler(settings, admin, revision, expected, last, read)
        control.stop(core)
        core = subprocess.Popen([control.CORE, "serve"])
        cleanup.callback(control.stop, core)
        control.wait_for(lambda: control.http("GET", "session")[0] == 401, "restarted core")
        saved = control.http("GET", settings, headers=admin)[2]
        assert saved["revision"] == revision
        control.stop(media)
        snapshot_file = Path(os.environ["BACURI_PROBE_DIRECTORY"]) / f"probe-{node}.json"
        outdated = json.loads(snapshot_file.read_text())
        outdated["fingerprint"] = "0" * 64
        snapshot_file.write_text(json.dumps(outdated))
        media = launch()
        control.wait_for(lambda: control.http("GET", route, headers=admin)[0] == 200, "stale node")
        failure = control.http("PUT", settings, {"mode": "auto"}, {**admin, "If-Match": f'"{revision}"'})
        assert (failure[0], failure[2]["code"]) == (409, "acceleration_probe_stale"), failure
        state = control.http("GET", settings, headers=admin)[2]
        assert state["revision"] == revision and state["mode"] == saved["mode"]
        if revision:
            assert state["issue"] == "acceleration_probe_stale"
        control.stop(media)
        offline = control.http("GET", settings, headers=admin)[2]
        assert offline["revision"] == revision and offline["mode"] == saved["mode"]
        if revision:
            assert offline["issue"] == "media_service_unavailable"
        code, _, fallback = control.http("PUT", settings, {"mode": "cpu_only"}, {**admin, "If-Match": f'"{revision}"'})
        assert code == 200 and fallback["mode"] == "cpu_only", fallback
        print(f"{expected}: node settings, preview, probe freshness and CPU fallback verified")
        print(f"{expected}: {len(last['measurements'])} measurements, repeated codec budgets, authorization, cancellation and restart verified")


if __name__ == "__main__":
    main()

