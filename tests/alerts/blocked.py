"""Read the upgrade but stall the TCP receive window during a real WSS replay."""

import base64
import json
import os
import socket
import struct
import time

import registry as control


def stalled_reader(viewer, path, cursor, expected_events):
    with socket.socket() as tcp:
        tcp.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 2048)
        tcp.setsockopt(socket.IPPROTO_TCP, socket.TCP_WINDOW_CLAMP, 2048)
        tcp.settimeout(8)
        tcp.connect(("localhost", 8443))
        with control.TLS.wrap_socket(tcp, server_hostname="localhost") as client:
            key = base64.b64encode(os.urandom(16)).decode()
            headers = [
                f"GET /api/v1/{path}/stream HTTP/1.1",
                "Host: localhost:8443",
                "Connection: Upgrade",
                "Upgrade: websocket",
                "Sec-WebSocket-Version: 13",
                "Sec-WebSocket-Protocol: bacuri.vms.alerts.v1",
                "Sec-WebSocket-Key: " + key,
                "Cookie: " + viewer["Cookie"],
                "Origin: " + control.ORIGIN,
                "",
                "",
            ]
            client.sendall("\r\n".join(headers).encode())
            header = b""
            while not header.endswith(b"\r\n\r\n"):
                chunk = client.recv(1)
                assert chunk
                header += chunk
            assert header.startswith(b"HTTP/1.1 101"), header
            data = json.dumps({"type": "subscribe", "after": cursor}).encode()
            mask = os.urandom(4)
            size = bytes([0x80 | len(data)]) if len(data) < 126 else b"\xfe" + struct.pack(">H", len(data))
            client.sendall(b"\x81" + size + mask + bytes(b ^ mask[i % 4] for i, b in enumerate(data)))
            time.sleep(3)
            client.settimeout(2)
            remaining = bytearray()
            while chunk := client.recv(8192):
                remaining.extend(chunk)
            sent = remaining.count(b'"type":"upsert"')
            assert 0 < sent < expected_events, (sent, expected_events)
            print(f"stalled WSS writer disconnected after {sent} of {expected_events} updates", flush=True)
