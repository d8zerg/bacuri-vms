"""Run the equipment series over the configured camera endpoints."""
from datetime import datetime, timezone
from http.client import HTTPException
import os
from pathlib import Path
import ssl
import subprocess
import time
from urllib.parse import urlsplit
import xml.etree.ElementTree as ET

from soap import NS, call, child, discover, endpoint, node, required

BIN = Path("build/gcc-debug/tests/fixtures")
FEATURES = ("discovery", "identity", "authentication", "profiles", "main", "sub", "ptz", "motion")


class Camera:
    def __init__(self, row, settings, credentials, stimulus):
        self.row = row
        self.url = settings["device_service"]
        self.host = urlsplit(self.url).hostname
        endpoint(self.url, self.host, "https")
        self.port = settings.get("discovery_port", 3702)
        self.ca = Path(settings["ca_file"]).resolve(strict=True)
        self.context = ssl.create_default_context(cafile=str(self.ca))
        self.credentials = credentials
        self.stimulus = stimulus
        self.tokens = settings["profile_tokens"]
        if (not isinstance(self.tokens, dict) or set(self.tokens) != {"main", "sub"}
                or not all(isinstance(v, str) and v.strip() for v in self.tokens.values())
                or self.tokens["main"] == self.tokens["sub"]):
            raise ValueError("expected distinct main and sub profile tokens")
        self.motion_source = settings.get("motion_source")
        if "motion" in row["features"] and (not isinstance(self.motion_source, dict) or
                not all(isinstance(self.motion_source.get(k), str) and self.motion_source[k] for k in ("name", "value"))):
            raise ValueError("missing expected motion source")
        self.services = {}
        self.encoders = {}
        self.observed = {}

    def request(self, service, operation, action=None, reference=()):
        return call(service, self.host, self.context, operation, action or operation.tag[1:].replace("}", "/"),
                    self.credentials, reference)

    def discovery(self):
        first = discover(self.host, self.port, self.url)
        if first != discover(self.host, self.port, self.url):
            raise ValueError("device endpoint reference changed")
        return {"endpoint_reference": first}

    def identity(self):
        info = self.request(self.url, node("tds:GetDeviceInformation"))
        self.observed = {key: required(info, "tds:" + key) for key in
                         ("Manufacturer", "Model", "FirmwareVersion", "SerialNumber", "HardwareId")}
        for key, field in [("Manufacturer", "manufacturer"), ("Model", "model"), ("FirmwareVersion", "firmware")]:
            if self.observed[key] != self.row[field]:
                raise ValueError("observed identity differs from inventory")
        return self.observed

    def authentication(self):
        # GetProfiles requires READ_MEDIA even on devices which expose identity before login.
        try:
            call(self.services["Media"], self.host, self.context, node("trt:GetProfiles"), NS["trt"] + "/GetProfiles",
                 (self.credentials[0], os.urandom(16).hex()))
        except PermissionError:
            return {"invalid_credentials": "rejected"}
        raise ValueError("invalid credentials accepted")

    def profiles(self):
        self.services.clear()
        self.encoders.clear()
        capabilities = node("tds:GetCapabilities")
        child(capabilities, "tds:Category", "All")
        caps = self.request(self.url, capabilities)
        for service in ["Media", "PTZ", "Events"]:
            address = caps.findtext(f"tds:Capabilities/tt:{service}/tt:XAddr", namespaces=NS)
            if address:
                endpoint(address, self.host, "https")
                self.services[service] = address
        profiles = self.request(self.services["Media"], node("trt:GetProfiles"))
        for profile in profiles.findall("trt:Profiles", NS):
            token = profile.attrib["token"]
            configuration = profile.find("tt:VideoEncoderConfiguration", NS)
            if configuration is not None and required(configuration, "tt:Encoding") == "H264":
                self.encoders[token] = [int(required(configuration, "tt:Resolution/tt:" + key))
                                        for key in ("Width", "Height")]
        if any(token not in self.encoders for token in self.tokens.values()):
            raise ValueError("required H.264 profile missing")
        return {role: {"token": token, "resolution": self.encoders[token]} for role, token in self.tokens.items()}

    def stream(self, role):
        token = self.tokens[role]
        op = node("trt:GetStreamUri")
        setup = child(op, "trt:StreamSetup")
        child(setup, "tt:Stream", "RTP-Unicast")
        child(child(setup, "tt:Transport"), "tt:Protocol", "RTSP")
        child(op, "trt:ProfileToken", token)
        uri = required(self.request(self.services["Media"], op), "trt:MediaUri/tt:Uri")
        endpoint(uri, self.host, self.row["media"])
        return self.frame(uri, token)

    def frame(self, uri, token):
        env = {**os.environ, "BACURI_FRAME_URI": uri, "BACURI_CAMERA_USER": self.credentials[0],
               "BACURI_CAMERA_PASSWORD": self.credentials[1]}
        frame = subprocess.run([BIN / "bacuri-vms-camera-frame", self.ca], env=env, capture_output=True, timeout=15)
        if frame.returncode:
            raise ValueError("H.264 frame could not be decoded over the configured transport")
        size = list(map(int, frame.stdout.split()))
        if size != self.encoders[token]:
            raise ValueError("decoded resolution differs from GetProfiles")
        return {"codec": "H264", "resolution": size, "transport": self.row["media"] + "/TCP"}

    def ptz(self):
        service = self.services["PTZ"]
        def command(name):
            op = node("tptz:" + name)
            child(op, "tptz:ProfileToken", self.tokens["main"])
            return op

        def status():
            return self.request(service, command("GetStatus"))

        def position(status):
            pan = status.find("tptz:PTZStatus/tt:Position/tt:PanTilt", NS)
            if pan is None:
                raise ValueError("missing pan position feedback")
            return float(pan.attrib["x"])

        before = position(status())
        move = command("RelativeMove")
        child(child(move, "tptz:Translation"), "tt:PanTilt", x="-0.1" if before > 0 else "0.1", y="0")
        try:
            self.request(service, move)
            for _ in range(10):
                time.sleep(0.1)
                if abs(position(status()) - before) > 0.001:
                    break
            else:
                raise ValueError("no pan movement observed")
        finally:
            stop = command("Stop")
            child(stop, "tptz:PanTilt", "true")
            self.request(service, stop)
        for _ in range(10):
            feedback = status()
            if required(feedback, "tptz:PTZStatus/tt:MoveStatus/tt:PanTilt") == "IDLE":
                return {"pan_before": before, "pan_after": position(feedback), "stop": "IDLE"}
            time.sleep(0.1)
        raise ValueError("stop unconfirmed")

    def motion(self):
        subscribe = node("tev:CreatePullPointSubscription")
        child(subscribe, "tev:InitialTerminationTime", "PT60S")
        result = self.request(self.services["Events"], subscribe,
                              NS["tev"] + "/EventPortType/CreatePullPointSubscriptionRequest")
        url = required(result, "tev:SubscriptionReference/a:Address")
        params = result.findall("tev:SubscriptionReference/a:ReferenceParameters/*", NS)
        for item in params:
            item.set("{" + NS["a"] + "}IsReferenceParameter", "true")
        try:
            start = datetime.now(timezone.utc)
            self.stimulus()
            for _ in range(5):
                pull = node("tev:PullMessages")
                child(pull, "tev:Timeout", "PT2S")
                child(pull, "tev:MessageLimit", "16")
                messages = self.request(url, pull, NS["tev"] + "/PullPointSubscription/PullMessagesRequest", params)
                for msg in messages.findall("wsnt:NotificationMessage", NS):
                    topic = required(msg, "wsnt:Topic")
                    if topic.split(":")[-1] != "RuleEngine/CellMotionDetector/Motion":
                        continue
                    motion = msg.find("wsnt:Message/tt:Message", NS)
                    if motion is None or motion.get("PropertyOperation") != "Changed":
                        continue
                    instant = datetime.fromisoformat(motion.attrib["UtcTime"])
                    sources = motion.findall("tt:Source/tt:SimpleItem", NS)
                    values = motion.findall("tt:Data/tt:SimpleItem", NS)
                    if (start <= instant <= datetime.now(timezone.utc)
                            and any(v.get("Name") == self.motion_source["name"] and v.get("Value") == self.motion_source["value"] for v in sources)
                            and any(v.get("Name") == "IsMotion" and v.get("Value") in ("true", "1") for v in values)):
                        return {"topic": topic, "source": self.motion_source, "utc": instant.isoformat()}
            raise ValueError("stimulated motion event missing")
        finally:
            self.request(url, node("wsnt:Unsubscribe"),
                         "http://docs.oasis-open.org/wsn/bw-2/SubscriptionManager/UnsubscribeRequest", params)


def series(camera):
    results = {}
    operations = {"discovery": camera.discovery, "identity": camera.identity, "profiles": camera.profiles,
                  "authentication": camera.authentication, "main": lambda: camera.stream("main"),
                  "sub": lambda: camera.stream("sub"), "ptz": camera.ptz, "motion": camera.motion}
    for feature, operation in operations.items():
        if feature not in camera.row["features"]:
            continue
        if feature in ("ptz", "motion") and results["identity"]["status"] != "passed":
            results[feature] = {"status": "blocked", "reason": "device identity mismatch"}
            continue
        try:
            results[feature] = {"status": "passed", "observed": operation()}
        except (ValueError, OSError, HTTPException, KeyError, TypeError, subprocess.SubprocessError, ET.ParseError) as exc:
            results[feature] = {"status": "failed", "reason": str(exc)}
    return {"expected": camera.row, "observed_identity": camera.observed, "results": results}
