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
from threading import RLock, Thread
from time import monotonic
import xml.etree.ElementTree as ET

from soap import NS, child, digest, envelope, message_id, node, required, tag


class Device:
    def __init__(self):
        self.password = secrets.token_urlsafe(24)
        self.serial = message_id()
        self.model = "bacuri.vms ONVIF emulator"
        self.firmware = "1.1"
        self.lock = RLock()
        self.tilt = 0.0
        self.velocity = (0.0, 0.0)
        self.updated = self.deadline = monotonic()
        self.online = True
        self.lose_move_reply = False
        self.stops = 0
        self.pan_range = (-1, 1)
        self.tilt_range = (-1, 1)
        self.subscriptions = 0
        self.pulled = 0
        self.polls = 0
        self.renewals = 0
        self.initial_lease_seconds = 60
        self.lease_end = 0
        self.source = "main"
        self.sources = {}
        self.discoverable_peers = []
        self.media2 = False
        self.disconnected = set()
        self.brightness = {}
        self.image_writes = []
        self.drop_image_reply = False
        self.report_position = True
        self.hfov = 64.0
        self.zoom = 0.0
        self.zoom_speed = 0.0
        self.miss_focus_stop = False
        self.focus = self.relay = self.light = self.wiper = False
        self.tour_deadline = 0.0
        self.preset = None
        self.inputs = []
        self.zoom_range = None
        self.discovery_urls = []
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

    @property
    def touring(self):
        return monotonic() < self.tour_deadline

    def feedback(self):
        with self.lock:
            now = monotonic()
            elapsed = max(0.0, min(now, self.deadline) - self.updated)
            self.position = max(-1, min(1, self.position + self.velocity[0] * elapsed))
            self.tilt = max(-1, min(1, self.tilt + self.velocity[1] * elapsed))
            self.zoom = max(0, min(1, self.zoom + self.zoom_speed * elapsed))
            self.updated = now
            if now >= self.deadline:
                self.velocity = (0.0, 0.0)
                self.zoom_speed = 0.0
            return self.position, self.tilt, any(self.velocity)

    def respond(self, request, path):
        body = request.find("s:Body/*", NS)
        if body is None:
            raise ValueError("empty body")
        action = body.tag.split("}")[-1]
        if body.tag.startswith("{" + NS["tds"] + "}") and path != self.device_path and path != "/deviceio":
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
                parent = child(capabilities, "tt:Extension") if name=="DeviceIO" else capabilities
                service = child(parent, "tt:" + name)
                if name=="DeviceIO":
                    child(service,"tt:AudioOutputs","1")
                child(service, "tt:XAddr", self.base + (self.media_path if name == "Media" else "/" + name.lower()))
                if name == "Events":
                    child(service, "tt:WSPullPointSupport", "true")
        elif self.media2 and body.tag == tag("tds:GetServices"):
            service=child(result,"tds:Service")
            child(service,"tds:Namespace",NS["tr2"])
            child(service,"tds:XAddr",self.base+"/media2")
        elif self.media2 and path == "/media2" and body.tag == tag("tr2:GetProfiles"):
            if required(body,"tr2:Type")!="All":
                raise ValueError("profile configurations required")
            for index,(token,(width,height)) in enumerate(self.tokens.items()):
                profile=child(result,"tr2:Profiles",token=token)
                child(profile,"tr2:Name",token)
                configs=child(profile,"tr2:Configurations")
                source=self.sources.get(token,self.source)
                child(child(configs,"tr2:VideoSource",token=source),"tt:SourceToken",source)
                enc=child(configs,"tr2:VideoEncoder",token=token)
                child(enc,"tt:Encoding","H264" if index else "H265")
                size=child(enc,"tt:Resolution")
                child(size,"tt:Width",str(width))
                child(size,"tt:Height",str(height))
        elif self.media2 and path=="/media2" and body.tag==tag("tr2:GetStreamUri"):
            token=required(body,"tr2:ProfileToken")
            if required(body,"tr2:Protocol")!="RTSP" or token not in self.tokens:
                raise ValueError("unsupported media profile")
            child(result,"tr2:Uri","rtsps://localhost:8554/"+self.stream_paths.get(token,token))
        elif body.tag == tag("trt:GetProfiles"):
            for token, (width, height) in self.tokens.items():
                profile = child(result, "trt:Profiles", token=token)
                child(profile, "tt:Name", token)
                channel = self.sources.get(token, self.source)
                source = child(profile, "tt:VideoSourceConfiguration", token=channel)
                child(source, "tt:SourceToken", channel)
                encoder = child(profile, "tt:VideoEncoderConfiguration", token=token)
                child(encoder, "tt:Encoding", self.codec)
                resolution = child(encoder, "tt:Resolution")
                child(resolution, "tt:Width", str(width))
                child(resolution, "tt:Height", str(height))
                if "PTZ" in self.services:
                    child(child(profile, "tt:PTZConfiguration", token="ptz"),"tt:NodeToken","head")
                if "DeviceIO" in self.services:
                    child(profile,"tt:AudioSourceConfiguration",token="audio-main")
        elif path == "/imaging" and action == "GetMoveOptions":
            options = child(result, "timg:MoveOptions")
            bounds = child(child(options, "tt:Continuous"), "tt:Speed")
            child(bounds, "tt:Min", "-1")
            child(bounds, "tt:Max", "1")
        elif path == "/imaging" and action == "Move":
            self.focus = float(required(body, "timg:Focus/tt:Continuous/tt:Speed"))
        elif path == "/imaging" and action == "Stop":
            if self.miss_focus_stop:
                self.miss_focus_stop = False
                raise ValueError("focus stop temporarily unavailable")
            self.focus = False
        elif path == "/ptz" and action == "GetNode":
            head = child(result, "tptz:PTZNode", token="head")
            for auxiliary in ("tt:Wiper|On", "tt:Wiper|Off", "tt:IRLamp|On", "tt:IRLamp|Off"):
                child(head, "tt:AuxiliaryCommands", auxiliary)
        elif path == "/ptz" and action == "GetPresets":
            child(child(result, "tptz:Preset", token="door"), "tt:Name", "Door")
        elif path == "/ptz" and action == "GetPresetTours":
            tour = child(result, "tptz:PresetTour", token="round")
            child(child(tour, "tt:StartingCondition"), "tt:RecurringDuration", "PT2S")
            detail = child(child(tour, "tt:TourSpot"), "tt:PresetDetail")
            child(detail, "tt:PresetToken", "door")
        elif path == "/ptz" and action == "GotoPreset":
            self.preset = required(body, "tptz:PresetToken")
            self.position = 0.2
        elif path == "/ptz" and action == "OperatePresetTour":
            self.tour_deadline = monotonic() + 2 if required(body, "tptz:Operation") == "Start" else 0.0
        elif path == "/ptz" and action == "SendAuxiliaryCommand":
            auxiliary = required(body, "tptz:AuxiliaryData")
            if "Wiper" in auxiliary:
                self.wiper = auxiliary.endswith("|On")
            else:
                self.light = auxiliary.endswith("|On")
        elif path == "/deviceio" and action == "GetDigitalInputs":
            child(result, "tio:DigitalInputs", token="contact")
        elif path == "/deviceio" and action == "GetRelayOutputs":
            child(result, "tds:RelayOutputs", token="relay")
        elif path == "/deviceio" and action == "SetRelayOutputState":
            self.relay = required(body, "tds:LogicalState") == "active"
            self.inputs.append(self.relay)
        elif body.tag == tag("trt:GetStreamUri"):
            token = required(body, "trt:ProfileToken")
            if token not in self.tokens or self.sources.get(token) in self.disconnected:
                raise ValueError("unknown stream")
            uri = child(result, "trt:MediaUri")
            child(uri, "tt:Uri", self.stream_uris.get(token, "rtsps://localhost:8554/" + self.stream_paths.get(token, token)))
            child(uri, "tt:InvalidAfterConnect", "false")
            child(uri, "tt:InvalidAfterReboot", "false")
            child(uri, "tt:Timeout", "PT0S")
        elif body.tag.startswith("{" + NS["timg"] + "}") and "Imaging" in self.services:
            source = required(body, "timg:VideoSourceToken")
            if path != "/imaging" or source not in self.sources.values():
                raise ValueError("unknown source")
            if action == "GetOptions":
                options = child(result, "timg:ImagingOptions")
                if source in self.brightness:
                    bounds = child(options, "tt:Brightness")
                    child(bounds, "tt:Min", "0")
                    child(bounds, "tt:Max", "100")
            elif action == "GetImagingSettings":
                child(child(result, "timg:ImagingSettings"), "tt:Brightness", str(self.brightness[source]))
            elif action == "SetImagingSettings":
                value = float(required(body, "timg:ImagingSettings/tt:Brightness"))
                if not 0 <= value <= 100 or required(body, "timg:ForcePersistence") != "true":
                    raise ValueError("invalid brightness")
                self.brightness[source] = value
                self.image_writes.append((source, value))
            else:
                raise ValueError("unknown imaging operation")
        elif body.tag == tag("tptz:GetServiceCapabilities") and "PTZ" in self.services:
            child(result, "tptz:Capabilities", StatusPosition="true", MoveStatus="true")
        elif body.tag == tag("tptz:GetConfigurationOptions") and "PTZ" in self.services:
            if path != "/ptz" or required(body, "tptz:ConfigurationToken") != "ptz":
                raise ValueError("unknown PTZ configuration")
            options = child(result, "tptz:PTZConfigurationOptions")
            spaces = child(options, "tt:Spaces")
            space = child(spaces, "tt:ContinuousPanTiltVelocitySpace")
            child(space, "tt:URI", "http://www.onvif.org/ver10/tptz/PanTiltSpaces/VelocityGenericSpace")
            for name, limits in [("XRange", self.pan_range), ("YRange", self.tilt_range)]:
                bounds = child(space, "tt:" + name)
                for field, limit in zip(["Min", "Max"], limits, strict=True):
                    child(bounds, "tt:" + field, str(limit))
            if self.zoom_range:
                zoom = child(spaces, "tt:ContinuousZoomVelocitySpace")
                child(zoom, "tt:URI", "http://www.onvif.org/ver10/tptz/ZoomSpaces/VelocityGenericSpace")
                bounds = child(zoom, "tt:XRange")
                child(bounds, "tt:Min", str(self.zoom_range[0]))
                child(bounds, "tt:Max", str(self.zoom_range[1]))
            timeout = child(options, "tt:PTZTimeout")
            child(timeout, "tt:Min", "PT0.1S")
            child(timeout, "tt:Max", "PT5S")
        elif action in ("GetStatus", "RelativeMove", "ContinuousMove", "Stop") and "PTZ" in self.services:
            self.feedback()
            if path != "/ptz" or required(body, "tptz:ProfileToken") not in self.tokens:
                raise ValueError("unknown PTZ profile")
            if action == "RelativeMove":
                pan = body.find("tptz:Translation/tt:PanTilt", NS)
                self.position = max(-1, min(1, self.position + float(pan.attrib["x"])))
            elif action == "ContinuousMove":
                velocity = body.find("tptz:Velocity/tt:PanTilt", NS)
                zoom = body.find("tptz:Velocity/tt:Zoom", NS)
                if required(body, "tptz:Timeout") != "PT1S" or (velocity is None) == (zoom is None):
                    raise ValueError("expected one bounded movement")
                if zoom is not None:
                    speed = float(zoom.attrib["x"])
                    if (not self.zoom_range or not self.zoom_range[0] <= speed <= self.zoom_range[1] or
                            zoom.get("space") != "http://www.onvif.org/ver10/tptz/ZoomSpaces/VelocityGenericSpace"):
                        raise ValueError("unsupported zoom speed")
                    self.zoom_speed = speed
                else:
                    self.velocity = (float(velocity.attrib["x"]), float(velocity.attrib["y"]))
                self.deadline = monotonic() + 1
            elif action == "Stop":
                self.stops += 1
                if body.findtext("tptz:PanTilt", default="true", namespaces=NS) == "true":
                    self.velocity = (0.0, 0.0)
                if body.findtext("tptz:Zoom", default="true", namespaces=NS) == "true":
                    self.zoom_speed = 0.0
            elif action == "GetStatus":
                status = child(result, "tptz:PTZStatus")
                if self.report_position:
                    position = child(status, "tt:Position")
                    child(position, "tt:PanTilt", x=str(self.position), y=str(self.tilt),
                          space="http://www.onvif.org/ver10/tptz/PanTiltSpaces/PositionGenericSpace")
                    child(position, "tt:Zoom", x=str(self.zoom),
                          space="http://www.onvif.org/ver10/tptz/ZoomSpaces/PositionGenericSpace")
                motion = child(status, "tt:MoveStatus")
                child(motion, "tt:PanTilt", "MOVING" if any(self.velocity) else "IDLE")
                if self.zoom_range:
                    child(motion, "tt:Zoom", "MOVING" if self.zoom_speed else "IDLE")
                child(status, "tt:UtcTime", utc())
                if self.hfov is not None:
                    child(status, "tt:FieldOfView", hfov=str(self.hfov), vfov="40")
        elif body.tag == tag("tev:CreatePullPointSubscription") and "Events" in self.services:
            if path != "/events" or required(request, "s:Header/a:Action") != NS["tev"] + "/EventPortType/CreatePullPointSubscriptionRequest":
                raise ValueError("subscription action")
            self.subscriptions += 1
            self.lease_end = monotonic() + self.initial_lease_seconds
            self.active = True
            self.notifications.clear()
            reference = child(result, "tev:SubscriptionReference")
            child(reference, "a:Address", self.base + "/pull")
            params = child(reference, "a:ReferenceParameters")
            ET.SubElement(params, "{urn:bacuri:fixture}Subscription").text = str(self.subscriptions)
            child(result, "wsnt:CurrentTime", utc())
            child(result, "wsnt:TerminationTime", utc(self.lease_end - monotonic()))
        elif path == "/pull" and self.active and monotonic() < self.lease_end:
            ref = request.find("s:Header/{urn:bacuri:fixture}Subscription", NS)
            if ref is None or ref.text != str(self.subscriptions) or ref.get(tag("a:IsReferenceParameter")) != "true":
                raise ValueError("pull reference or action")
            if body.tag == tag("wsnt:Unsubscribe"):
                self.active = False
                return result
            if body.tag == tag("wsnt:Renew"):
                self.renewals += 1
                self.lease_end = monotonic() + 60
                child(result, "wsnt:TerminationTime", utc(60))
                return result
            if body.tag==tag("tev:SetSynchronizationPoint"):
                self.inputs.append(self.relay)
                return result
            if body.tag != tag("tev:PullMessages") or required(request, "s:Header/a:Action") != NS["tev"] + "/PullPointSubscription/PullMessagesRequest":
                raise ValueError("unknown subscription operation")
            self.polls += 1
            self.pulled += len(self.notifications)
            child(result, "tev:CurrentTime", utc())
            child(result, "tev:TerminationTime", utc(self.lease_end - monotonic()))
            for source, instant, state, operation in self.notifications:
                notification = child(result, "wsnt:NotificationMessage")
                child(notification, "wsnt:Topic", "tns1:RuleEngine/CellMotionDetector/Motion",
                      Dialect="http://www.onvif.org/ver10/tev/topicExpression/ConcreteSet",
                      **{"xmlns:tns1": "http://www.onvif.org/ver10/topics"})
                msg = child(child(notification, "wsnt:Message"), "tt:Message", UtcTime=instant, PropertyOperation=operation)
                child(child(msg, "tt:Source"), "tt:SimpleItem", Name="VideoSourceConfigurationToken", Value=source)
                child(child(msg, "tt:Data"), "tt:SimpleItem", Name="IsMotion", Value=state)
            for state in self.inputs:
                n=child(result,"wsnt:NotificationMessage")
                child(n,"wsnt:Topic","tns1:Device/Trigger/DigitalInput",**{"xmlns:tns1":"http://www.onvif.org/ver10/topics"})
                msg=child(child(n,"wsnt:Message"),"tt:Message",UtcTime=utc(),PropertyOperation="Changed")
                child(child(msg,"tt:Source"),"tt:SimpleItem",Name="InputToken",Value="contact")
                child(child(msg,"tt:Data"),"tt:SimpleItem",Name="LogicalState",Value="true" if state else "false")
            self.inputs.clear()
            self.notifications.clear()
        else:
            raise ValueError("unsupported operation")
        return result

    def motion(self, instant=None, source=None, state="true", operation="Changed"):
        with self.lock:
            if self.active:
                self.notifications.append((source or self.source, instant or utc(), state, operation))


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
            with device.lock:
                if not device.online:
                    self.close_connection = True
                    return
                response = device.respond(root, self.path)
                if device.drop_image_reply and response.tag == tag("timg:SetImagingSettingsResponse"):
                    device.drop_image_reply = False
                    self.close_connection = True
                    return
                if device.lose_move_reply and response.tag == tag("tptz:ContinuousMoveResponse"):
                    device.lose_move_reply = False
                    self.close_connection = True
                    return
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
        for peer in [device, *device.discoverable_peers]:
            match = child(matches, "d:ProbeMatch", **{"xmlns:dn": NS["dn"]})
            child(child(match, "wa:EndpointReference"), "wa:Address", peer.serial)
            child(match, "d:Types", "dn:NetworkVideoTransmitter")
            child(match, "d:XAddrs", " ".join(peer.discovery_urls) or peer.base + peer.device_path)
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
