"""Programmable ONVIF test device with directed discovery, media, PTZ and PullPoint."""
from base64 import b64decode
from contextlib import contextmanager
from datetime import datetime, timedelta, timezone
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import hmac
import secrets
import socket
from socketserver import BaseRequestHandler, UDPServer
import ssl
from threading import Thread
import xml.etree.ElementTree as ET

from soap import NS, child, digest, envelope, message_id, node, required, tag


class Device:
    def __init__(self):
        self.password = secrets.token_urlsafe(24)
        self.serial = message_id()
        self.model = "bacuri.vms ONVIF emulator"
        self.firmware = "1.0"
        self.position = 0.0
        self.active = False
        self.notifications = []
        self.services = ["Media", "PTZ", "Events"]
        self.codec = "H264"
        self.device_path = "/device_service"
        self.media_path = "/media"
        self.stream_paths = {}
        self.stream_uris = {}
        self.tokens = {"main": (1280, 720), "sub": (640, 360)}

    def respond(self, request, path):
        body = request.find("s:Body/*", NS)
        if body is None:
            raise ValueError("empty body")
        action = body.tag.split("}")[-1]
        if body.tag.startswith("{" + NS["tds"] + "}") and path != self.device_path:
            raise ValueError("unknown device service")
        if body.tag.startswith("{" + NS["trt"] + "}") and path != self.media_path:
            raise ValueError("unknown media service")
        result = ET.Element(body.tag + "Response")
        if body.tag == tag("tds:GetDeviceInformation"):
            for key, value in [("Manufacturer", "bacuri.vms"), ("Model", self.model),
                               ("FirmwareVersion", self.firmware), ("SerialNumber", self.serial), ("HardwareId", "test")]:
                child(result, "tds:" + key, value)
        elif body.tag == tag("tds:GetCapabilities"):
            capabilities = child(result, "tds:Capabilities")
            for name in self.services:
                child(child(capabilities, "tt:" + name), "tt:XAddr", self.base + (self.media_path if name == "Media" else "/" + name.lower()))
        elif body.tag == tag("trt:GetProfiles"):
            for token, (width, height) in self.tokens.items():
                profile = child(result, "trt:Profiles", token=token)
                child(profile, "tt:Name", token)
                encoder = child(profile, "tt:VideoEncoderConfiguration", token=token)
                child(encoder, "tt:Encoding", self.codec)
                resolution = child(encoder, "tt:Resolution")
                child(resolution, "tt:Width", str(width))
                child(resolution, "tt:Height", str(height))
                if "PTZ" in self.services:
                    child(profile, "tt:PTZConfiguration", token="ptz")
        elif body.tag == tag("trt:GetStreamUri"):
            token = required(body, "trt:ProfileToken")
            if token not in self.tokens:
                raise ValueError("unknown stream")
            uri = child(result, "trt:MediaUri")
            child(uri, "tt:Uri", self.stream_uris.get(token, "rtsps://localhost:8554/" + self.stream_paths.get(token, token)))
            child(uri, "tt:InvalidAfterConnect", "false")
            child(uri, "tt:InvalidAfterReboot", "false")
            child(uri, "tt:Timeout", "PT0S")
        elif action in ("GetStatus", "RelativeMove", "Stop") and "PTZ" in self.services:
            if required(body, "tptz:ProfileToken") not in self.tokens:
                raise ValueError("unknown PTZ profile")
            if action == "RelativeMove":
                pan = body.find("tptz:Translation/tt:PanTilt", NS)
                self.position = max(-1, min(1, self.position + float(pan.attrib["x"])))
            elif action == "GetStatus":
                status = child(result, "tptz:PTZStatus")
                child(child(status, "tt:Position"), "tt:PanTilt", x=str(self.position), y="0")
                child(child(status, "tt:MoveStatus"), "tt:PanTilt", "IDLE")
                child(status, "tt:UtcTime", utc())
        elif body.tag == tag("tev:CreatePullPointSubscription") and "Events" in self.services:
            self.active = True
            self.notifications.clear()
            child(child(result, "tev:SubscriptionReference"), "a:Address", self.base + "/pull")
            child(result, "wsnt:CurrentTime", utc())
            child(result, "wsnt:TerminationTime", utc(60))
        elif path == "/pull" and self.active and body.tag == tag("tev:PullMessages"):
            child(result, "tev:CurrentTime", utc())
            child(result, "tev:TerminationTime", utc(60))
            for source, instant in self.notifications:
                notification = child(result, "wsnt:NotificationMessage")
                child(notification, "wsnt:Topic", "tns1:RuleEngine/CellMotionDetector/Motion",
                      Dialect="http://www.onvif.org/ver10/tev/topicExpression/ConcreteSet",
                      **{"xmlns:tns1": "http://www.onvif.org/ver10/topics"})
                msg = child(child(notification, "wsnt:Message"), "tt:Message", UtcTime=instant, PropertyOperation="Changed")
                child(child(msg, "tt:Source"), "tt:SimpleItem", Name="VideoSourceConfigurationToken", Value=source)
                child(child(msg, "tt:Data"), "tt:SimpleItem", Name="IsMotion", Value="true")
            self.notifications.clear()
        elif path == "/pull" and body.tag == tag("wsnt:Unsubscribe"):
            self.active = False
        else:
            raise ValueError("unsupported operation")
        return result

    def motion(self):
        if self.active:
            self.notifications.append(("main", utc()))


def utc(seconds=0):
    return (datetime.now(timezone.utc) + timedelta(seconds=seconds)).isoformat().replace("+00:00", "Z")


class Control(BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def do_POST(self):
        device = self.server.device
        try:
            size = int(self.headers["Content-Length"])
            if not 0 < size <= 65536:
                raise ValueError("SOAP size")
            root = ET.fromstring(self.rfile.read(size))
            token = root.find("s:Header/wsse:Security/wsse:UsernameToken", NS)
            if token is None:
                self.send_error(401)
                return
            expected = digest(b64decode(required(token, "wsse:Nonce")), required(token, "wsu:Created"), device.password)
            if required(token, "wsse:Username") != "fixture" or not hmac.compare_digest(expected, required(token, "wsse:Password")):
                self.send_error(401)
                return
            response = device.respond(root, self.path)
            action = required(root, "s:Header/a:Action").removesuffix("Request") + "Response"
            output = envelope(response, [node("a:Action", action), node("a:RelatesTo", required(root, "s:Header/a:MessageID")),
                                         node("a:To", NS["a"] + "/anonymous")])
        except (ValueError, ET.ParseError, AttributeError, KeyError, TypeError):
            self.send_error(400)
            return
        self.send_response(200)
        self.send_header("Content-Type", "application/soap+xml")
        self.send_header("Content-Length", str(len(output)))
        self.end_headers()
        self.wfile.write(output)


class Discovery(BaseRequestHandler):
    def handle(self):
        packet, sock = self.request
        root = ET.fromstring(packet)
        if root.find("s:Body/d:Probe", NS) is None:
            return
        device = self.server.device
        matches = node("d:ProbeMatches")
        match = child(matches, "d:ProbeMatch", **{"xmlns:dn": NS["dn"]})
        child(child(match, "wa:EndpointReference"), "wa:Address", device.serial)
        child(match, "d:Types", "dn:NetworkVideoTransmitter")
        child(match, "d:XAddrs", device.base + device.device_path)
        child(match, "d:MetadataVersion", "1")
        headers = [node("wa:RelatesTo", required(root, "s:Header/wa:MessageID")),
                   node("wa:Action", NS["d"] + "/ProbeMatches"), node("wa:MessageID", message_id())]
        sock.sendto(envelope(matches, headers), self.client_address)


@contextmanager
def serve(cert, key, multicast=False):
    device = Device()
    bind = ("239.255.255.250", 3702) if multicast else ("127.0.0.1", 0)
    with ThreadingHTTPServer(("127.0.0.1", 0), Control) as http, UDPServer(bind, Discovery) as udp:
        if multicast:
            udp.socket.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP,
                                  socket.inet_aton(bind[0]) + socket.inet_aton("127.0.0.1"))
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.load_cert_chain(cert, key)
        http.socket = context.wrap_socket(http.socket, server_side=True)
        device.base = f"https://localhost:{http.server_port}"
        http.device = udp.device = device
        for server in [http, udp]:
            Thread(target=server.serve_forever, daemon=True).start()
        try:
            yield device, udp.server_address[1]
        finally:
            udp.shutdown()
            http.shutdown()
