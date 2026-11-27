"""Measure the production codecs using a private core, media service and database."""
from contextlib import ExitStack
from pathlib import Path
import json
import os
import signal
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
from server_acceleration_cost import FRAMES, FPS, make_input
from node import check_measurements, completed, control
from runtime import Task


def save(file, value):
    if file.name == "status.json":
        value = {**value, "worker_container": os.environ.get("BACURI_QUALIFICATION_CONTAINER", os.uname().nodename),
                 "heartbeat_unix_s": int(time.time())}
    temporary = file.with_suffix(".tmp")
    temporary.write_text(json.dumps(value, indent=2) + "\n")
    temporary.replace(file)


def command(kind, file, frames=FRAMES, output=os.devnull):
    return [control.BIN / "bacuri-pipeline-check", kind, file.name.split("-")[0], file, output, str(frames)]


class Node:
    def __init__(self, processes, output, backend):
        self.backend = backend
        self.output = output
        self.route = f"sites/{control.SITE}/nodes/{os.environ['BACURI_NODE_ID']}/acceleration"
        subprocess.run([control.CORE, "create-user", "admin", "administrator"],
                       input=control.PASSWORD + "\n", text=True, check=True)
        self.services = []
        for args in ([control.CORE, "serve"], [control.BIN / "bacuri-vms-media"]):
            child = subprocess.Popen(args)
            self.services.append(child)
            processes.callback(control.stop, child)
        control.wait_for(lambda: control.http("GET", "session")[0] == 401, "core HTTPS")
        self.headers = control.login("admin")
        control.wait_for(lambda: self.request("GET", "/probe")[0] == 200, "media probe")
        self.probe = completed(lambda: self.request("GET", "/probe")[2])
        check_measurements(self.probe, backend)
        save(output / "probe.json", self.probe)
        self.revision = self.request("GET")[2]["revision"]

    def request(self, verb, suffix="", body=None, revision=False):
        headers = self.headers.copy()
        if revision:
            headers["If-Match"] = f'"{self.revision}"'
        return control.http(verb, self.route + suffix, body, headers)

    def apply(self, mode, task=None):
        settings = {"mode": mode}
        if mode == "manual":
            settings["gpu_tasks"] = [task]
        code, _, preview = self.request("POST", "/preview", settings)
        if code == 409 or (mode == "manual" and preview.get("rejected_tasks")):
            return False
        if code != 200:
            raise RuntimeError((code, preview))
        save(self.output / f"budget-{mode}-{task if mode == 'manual' else 'node'}.json", preview)
        code, _, saved = self.request("PUT", body=settings, revision=True)
        if code != 200:
            raise RuntimeError((code, saved))
        self.revision = saved["revision"]
        control.wait_for(lambda: self.request("GET", "/probe")[2]["scheduler"]["mode"] == mode, "policy")
        # A manual task selection can change without changing the mode.
        if mode == "manual":
            worker = Task(task)
            try:
                control.wait_for(lambda: worker.update() != "cpu", "manual GPU selection")
            finally:
                worker.update("FINISHED")
        return True


def main():
    limit = 600
    deadline = time.monotonic() + limit
    backend = os.environ["BACURI_PROBE_BACKEND"]
    phase = os.environ["BACURI_QUALIFICATION_PHASE"]
    base = Path("build/acceleration/qualification")
    base.mkdir(parents=True, exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix=f"{backend}-{phase}-", dir=base))
    print(f"Qualification artifacts: {out}", flush=True)
    binaries = sorted(control.BIN.glob("bacuri-*"))
    with (out / "binaries.sha256").open("w") as output:
        subprocess.run(["sha256sum", *[p.name for p in binaries]], cwd=control.BIN, stdout=output, check=True)
    save(out / "status.json", {"state": "starting", "phase": phase, "backend": backend})
    def interrupted(signum, _frame):
        timeout = signum == signal.SIGALRM or time.monotonic() >= deadline - 1
        error = TimeoutError("Qualification exceeded the ten-minute limit") if timeout else KeyboardInterrupt("Stopped")
        status = json.loads((out / "status.json").read_text())
        save(out / "status.json", {**status, "state": "failed" if timeout else "interrupted", "error": str(error)})
        raise error

    signal.signal(signal.SIGTERM, interrupted)
    signal.signal(signal.SIGALRM, interrupted)
    signal.alarm(limit)
    try:
        with ExitStack() as cleanup:
            duration = int(os.environ.get("BACURI_SOAK_SECONDS") or "120") if phase == "soak" else 0
            if phase == "soak" and not 48 <= duration <= limit:
                raise ValueError("Auto duration must be between 48 and 600 seconds")
            node = Node(cleanup, out, backend)
            node.deadline = deadline
            node.duration = duration
            save(out / "status.json", {"state": "running", "phase": phase, "backend": backend})
            if phase == "capacity":
                from capacity import measure
                measure(node)
            elif phase == "faults":
                from faults import measure
                measure(node)
            elif phase == "soak":
                from soak import measure
                measure(node)
            else:
                raise ValueError(phase)
        status = json.loads((out / "status.json").read_text())
        save(out / "status.json", {**status, "state": "completed"})
    except BaseException as error:
        status = json.loads((out / "status.json").read_text())
        state = "interrupted" if isinstance(error, KeyboardInterrupt) else "failed"
        save(out / "status.json", {**status, "state": state, "error": str(error) or type(error).__name__})
        raise
    finally:
        signal.alarm(0)


if __name__ == "__main__":
    # Keep a single module instance when the phase imports the shared fixture.
    sys.modules["qualification"] = sys.modules[__name__]
    main()
