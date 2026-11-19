"""Forward one browser's ICE/DTLS/SRTP datagrams and shape only outbound video."""

import socket
from threading import Event, Thread
import time


class VideoLink:
    def __init__(self, browser):
        self.host = socket.gethostbyname(socket.gethostname())
        self.client_host = socket.gethostbyname("playback-" + browser)
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.bind((self.host, 0))
        self.sock.settimeout(0.1)
        self.port = self.sock.getsockname()[1]
        self.stop = Event()
        self.limited = Event()
        self.dropped = 0
        self.last_packet = 0
        self.worker = Thread(target=self.forward)

    def __enter__(self):
        self.worker.start()
        return self

    def __exit__(self, *_):
        self.stop.set()
        self.worker.join()
        self.sock.close()

    def forward(self):
        server = client = None
        available = 1200
        last = time.monotonic()
        while not self.stop.is_set():
            try:
                data, sender = self.sock.recvfrom(65535)
            except TimeoutError:
                continue
            now = time.monotonic()
            if sender[0] == self.host:
                server = sender
                target = client
                # RTP and RTCP share the ICE component. Pass control traffic at full rate.
                if len(data) >= 12 and data[0] >> 6 == 2 and not 192 <= data[1] <= 223:
                    self.last_packet = now
                    if self.limited.is_set():
                        available = min(2400, available + (now - last) * 8192)
                        last = now
                        if available < len(data):
                            self.dropped += 1
                            continue
                        available -= len(data)
            elif sender[0] == self.client_host:
                client = sender
                target = server
            else:
                continue
            if target:
                self.sock.sendto(data, target)
