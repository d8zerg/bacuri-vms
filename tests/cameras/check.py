"""Execute and report the common capability series for the emulator or physical inventory."""
import argparse
from contextlib import contextmanager
from copy import deepcopy
import hashlib
import json
from pathlib import Path
import socket
import ssl
import subprocess
import sys
import tempfile
import time

from emulator import serve
from profile import MATRIX, PROFILE, load_profile, need, validate
from series import BIN, Camera, series
from soap import call, NS, node

STAND = Path("/stand")


def sha256(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def passed(row):
    return (set(row["results"]) == set(row["expected"]["features"]) and
            all(result["status"] == "passed" for result in row["results"].values()))


def equipment_complete(inventory, rows):
    need(inventory and len(rows) == len(inventory), "missing mandatory physical equipment")
    need({"ptz", "motion"} <= {feature for cam in inventory for feature in cam["features"]},
         "physical PTZ and motion coverage required")
    for expected, row in zip(inventory, rows, strict=True):
        need(row["expected"] == expected and passed(row), "incomplete physical camera series")


@contextmanager
def streams(cert, key):
    bundle = cert.parent / "server.pem"
    bundle.write_bytes(cert.read_bytes() + key.read_bytes())
    process = subprocess.Popen([BIN / "bacuri-vms-test-rtsp-camera", "--tls", bundle])
    try:
        for _ in range(50):
            need(process.poll() is None, "RTSP emulator exited")
            try:
                with socket.create_connection(("localhost", 8554), timeout=0.1):
                    break
            except OSError:
                time.sleep(0.1)
        else:
            raise ValueError("RTSP emulator startup timed out")
        yield
    finally:
        process.terminate()
        process.wait(timeout=5)


def expect_rejection(operation, error=ValueError):
    try:
        operation()
    except error:
        return
    raise ValueError("negative control unexpectedly passed")


def emulated(p, report):
    report["video_source"] = "GStreamer videotestsrc"
    with tempfile.TemporaryDirectory() as directory:
        base = Path(directory)
        cert, key = base / "stand.crt", base / "stand.key"
        subprocess.run(["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "1",
                        "-subj", "/CN=localhost", "-addext", "subjectAltName=DNS:localhost",
                        "-keyout", key, "-out", cert], check=True, capture_output=True)
        with streams(cert, key), serve(cert, key) as (device, port):
            url = device.base + "/device_service"
            settings = {"device_service": url, "discovery_port": port, "ca_file": cert,
                        "profile_tokens": {"main": "main", "sub": "sub"},
                        "motion_source": {"name": "VideoSourceConfigurationToken", "value": "main"}}
            camera = Camera(p["emulator"], settings, ("fixture", device.password), device.motion)
            result = series(camera)
            report["devices"].append(result)
            need(passed(result), "emulated camera series failed")
            for host, context in [("localhost", ssl.create_default_context()), ("127.0.0.1", camera.context)]:
                expect_rejection(lambda: call(url.replace("localhost", host), host, context,
                                              node("tds:GetDeviceInformation"), NS["tds"] + "/GetDeviceInformation",
                                              camera.credentials), ssl.SSLCertVerificationError)
            # The media probe verifies its own GStreamer TLS connection.
            ca = camera.ca
            camera.ca = Path("/etc/ssl/certs/ca-certificates.crt")
            expect_rejection(lambda: camera.stream("main"))
            camera.ca = ca
            expect_rejection(lambda: camera.frame("rtsps://127.0.0.1:8554/main", "main"))
            device.codec = "JPEG"
            expect_rejection(camera.profiles)
            device.firmware += ".other"
            expect_rejection(camera.identity)
            expect_rejection(lambda: equipment_complete([], []))
            incomplete = deepcopy(result)
            del incomplete["results"]["sub"]
            expect_rejection(lambda: equipment_complete([p["emulator"]], [incomplete]))
            invalid = deepcopy(p)
            del invalid["emulator"]["firmware"]
            expect_rejection(lambda: validate(invalid))
            report["negative_controls"] = ["untrusted HTTPS issuer", "HTTPS hostname mismatch",
                                           "untrusted RTSPS issuer", "RTSPS hostname mismatch", "unsupported codec",
                                           "firmware mismatch", "empty hardware inventory",
                                           "missing stream result", "incomplete profile"]


def stand_path(relative):
    path = (STAND / relative).resolve(strict=True)
    need(path.is_relative_to(STAND), "stand file outside mount")
    return path


def blocked(row, error):
    return {"expected": row, "observed_identity": {}, "results": {
        name: {"status": "blocked", "reason": str(error)} for name in row["features"]}}


def physical(p, report):
    inventory = p["hardware"]
    need(inventory, "no physical model and firmware assigned")
    try:
        report["bundle_sha256"] = sha256(stand_path("bundle"))
        settings = json.loads(stand_path("connections.json").read_bytes())
        need(isinstance(settings, list) and all(isinstance(s, dict) for s in settings), "invalid connection inventory")
    except (OSError, ValueError) as error:
        report["devices"] = [blocked(row, error) for row in inventory]
        raise
    for row in inventory:
        try:
            matches = [s for s in settings if all(row[k] == s.get(k) for k in ("manufacturer", "model", "firmware"))]
            need(len(matches) == 1, "provide one connection for this model and firmware")
            config = matches[0]
            config["ca_file"] = stand_path(config["ca_file"])
            password = stand_path(config["password_file"]).read_text().rstrip("\r\n")
            need(password and config["username"], "missing camera credentials")
            def stimulate():
                subprocess.run([stand_path(config["stimulus"])], check=True, timeout=10, capture_output=True)
            device = Camera(row, config, (config["username"], password), stimulate)
            result = series(device)
            result["ca_sha256"] = sha256(config["ca_file"])
        except (KeyError, TypeError, ValueError, OSError) as error:
            result = blocked(row, error)
        report["devices"].append(result)
    equipment_complete(inventory, report["devices"])


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("mode", choices=["emulator", "hardware"])
    parser.add_argument("report", type=Path)
    args = parser.parse_args()
    report = {"mode": args.mode, "profile_sha256": sha256(PROFILE), "asvs_sha256": sha256(MATRIX),
              "devices": [], "passed": False}
    try:
        p = load_profile()
        if args.mode == "hardware":
            physical(p, report)
        else:
            emulated(p, report)
        report["passed"] = True
    except (TypeError, ValueError, OSError, KeyError, subprocess.SubprocessError) as error:
        report["error"] = str(error)
    args.report.mkdir(parents=True, exist_ok=True)
    output = args.report / (args.mode + ".json")
    with tempfile.NamedTemporaryFile("w", dir=args.report, delete=False) as file:
        json.dump(report, file, indent=2, ensure_ascii=False)
        file.write("\n")
    Path(file.name).replace(output)
    print(output, "PASS" if report["passed"] else report["error"])
    return int(not report["passed"])


if __name__ == "__main__":
    sys.exit(main())
