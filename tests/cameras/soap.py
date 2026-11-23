"""SOAP and WS-Discovery messages used by the ONVIF test stand."""
from http.client import HTTPSConnection
from base64 import b64encode
from datetime import datetime, timezone
import hashlib
import os
import socket
import uuid
from urllib.parse import urlsplit
import xml.etree.ElementTree as ET

NS = {
    "s": "http://www.w3.org/2003/05/soap-envelope",
    "a": "http://www.w3.org/2005/08/addressing",
    "wa": "http://schemas.xmlsoap.org/ws/2004/08/addressing",
    "d": "http://schemas.xmlsoap.org/ws/2005/04/discovery",
    "dn": "http://www.onvif.org/ver10/network/wsdl",
    "tds": "http://www.onvif.org/ver10/device/wsdl",
    "tio": "http://www.onvif.org/ver10/deviceIO/wsdl",
    "timg": "http://www.onvif.org/ver20/imaging/wsdl",
    "trt": "http://www.onvif.org/ver10/media/wsdl",
    "tt": "http://www.onvif.org/ver10/schema",
    "tptz": "http://www.onvif.org/ver20/ptz/wsdl",
    "tev": "http://www.onvif.org/ver10/events/wsdl",
    "wsse": "http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-wssecurity-secext-1.0.xsd",
    "wsu": "http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-wssecurity-utility-1.0.xsd",
    "wsnt": "http://docs.oasis-open.org/wsn/b-2",
}
for prefix, uri in NS.items():
    ET.register_namespace(prefix, uri)


def tag(name):
    prefix, local = name.split(":")
    return f"{{{NS[prefix]}}}{local}"


def node(name, value=None, **attrs):
    element = ET.Element(tag(name), attrs)
    element.text = value
    return element


def child(parent, name, value=None, **attrs):
    element = node(name, value, **attrs)
    parent.append(element)
    return element


def required(parent, path):
    value = parent.findtext(path, namespaces=NS)
    if not value or not value.strip():
        raise ValueError(f"missing {path}")
    return value.strip()


def envelope(body, headers=()):
    root = node("s:Envelope")
    child(root, "s:Header").extend(headers)
    child(root, "s:Body").append(body)
    return ET.tostring(root, encoding="utf-8", xml_declaration=True)


def message_id():
    return f"urn:uuid:{uuid.uuid4()}"


def discover(host, port, expected_url):
    probe = node("d:Probe", **{"xmlns:dn": NS["dn"]})
    child(probe, "d:Types", "dn:NetworkVideoTransmitter")
    mid = message_id()
    data = envelope(probe, [node("wa:MessageID", mid), node("wa:Action", NS["d"] + "/Probe"),
                            node("wa:To", "urn:schemas-xmlsoap-org:ws:2005:04:discovery")])
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.settimeout(3)
        sock.connect((host, port))
        sock.send(data)
        root = ET.fromstring(sock.recv(65535))
    if required(root, "s:Header/wa:RelatesTo") != mid:
        raise ValueError("uncorrelated discovery response")
    expected = endpoint(expected_url, host, "https")
    for match in root.findall("s:Body/d:ProbeMatches/d:ProbeMatch", NS):
        urls = required(match, "d:XAddrs").split()
        secure = [url for url in urls if url.startswith("https://")]
        standard_http = [urlsplit(url) for url in urls if url.startswith("http://")]
        same_service = not secure and expected.port in (None, 443) and any(
            url.hostname == host and url.port in (None, 80) and url.username is None and url.password is None
            and not url.fragment and url.path == expected.path and url.query == expected.query for url in standard_http)
        if expected_url in secure or same_service:
            return required(match, "wa:EndpointReference/wa:Address")
    raise ValueError("configured device service missing from ProbeMatches")


def endpoint(url, host, scheme):
    parts = urlsplit(url)
    if (parts.scheme != scheme or parts.hostname != host or parts.username is not None
            or parts.password is not None or parts.fragment):
        raise ValueError("unexpected camera endpoint or transport")
    return parts


def digest(nonce, created, password):
    # ONVIF UsernameToken fixes the digest algorithm on the wire.
    return b64encode(hashlib.sha1(nonce + created.encode() + password.encode()).digest()).decode()


def security(user, password):
    nonce = os.urandom(24)
    created = datetime.now(timezone.utc).isoformat(timespec="milliseconds").replace("+00:00", "Z")
    header = node("wsse:Security")
    token = child(header, "wsse:UsernameToken")
    child(token, "wsse:Username", user)
    base = "http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-"
    child(token, "wsse:Password", digest(nonce, created, password),
          Type=base + "username-token-profile-1.0#PasswordDigest")
    child(token, "wsse:Nonce", b64encode(nonce).decode(), EncodingType=base + "soap-message-security-1.0#Base64Binary")
    child(token, "wsu:Created", created)
    return header


def call(url, host, context, operation, action, credentials=None, reference=()):
    parts = endpoint(url, host, "https")
    headers = [node("a:Action", action), node("a:MessageID", message_id()), node("a:To", url)]
    child_header = node("a:ReplyTo")
    child(child_header, "a:Address", NS["a"] + "/anonymous")
    headers.append(child_header)
    headers.extend(reference)
    http = HTTPSConnection(host, parts.port, timeout=10, context=context)
    try:
        http.connect()
        fields = {"Content-Type": f'application/soap+xml; charset=utf-8; action="{action}"'}
        if credentials:
            headers.append(security(*credentials))
        http.request("POST", parts.path + ("?" + parts.query if parts.query else ""),
                     envelope(operation, headers), fields)
        response = http.getresponse()
        payload = response.read(1024 * 1024 + 1)
        if response.status in (401, 403):
            raise PermissionError("camera rejected credentials")
        if len(payload) > 1024 * 1024:
            raise ValueError("oversized SOAP response")
        root = ET.fromstring(payload)
        fault = root.find("s:Body/s:Fault", NS)
        if fault is not None:
            code = fault.findtext("s:Code/s:Subcode/s:Value", namespaces=NS) or ""
            if code.split(":")[-1] == "NotAuthorized":
                raise PermissionError("camera rejected credentials")
            raise ValueError("camera returned a SOAP fault")
        result = root.find("s:Body/*", NS)
        if response.status != 200 or result is None or result.tag != operation.tag + "Response":
            raise ValueError("unexpected SOAP response")
        return result
    finally:
        http.close()
