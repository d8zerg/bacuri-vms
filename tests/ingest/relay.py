"""Exercise independent RTSP/TCP connections through a disposable local relay."""
import asyncio
import os
import socket
import sys
from contextlib import suppress


async def pump(reader, writer):
    while data := await reader.read(65536):
        writer.write(data)
        await writer.drain()


async def scenario(binary, host):
    enabled = True
    clients = set()

    async def forward(reader, writer):
        peers = [writer]
        pumps = []
        try:
            if enabled:
                clients.add(writer)
                upstream, outgoing = await asyncio.open_connection(host, 8554)
                peers.append(outgoing)
                pumps = [asyncio.create_task(pump(reader, outgoing)), asyncio.create_task(pump(upstream, writer))]
                await asyncio.wait(pumps, return_when=asyncio.FIRST_COMPLETED)
        except OSError:
            pass
        finally:
            clients.discard(writer)
            for task in pumps:
                task.cancel()
            await asyncio.gather(*pumps, return_exceptions=True)
            for peer in peers:
                peer.close()

    async with await asyncio.start_server(forward, "127.0.0.1", 0) as relay:
        port = relay.sockets[0].getsockname()[1]
        parent, child = socket.socketpair()
        with parent, child:
            parent.setblocking(False)
            process = await asyncio.create_subprocess_exec(
                binary, pass_fds=(child.fileno(),),
                env={**os.environ, "BACURI_TEST_RTSP_BASE": f"rtsp://127.0.0.1:{port}",
                     "BACURI_TEST_LINK_FD": str(child.fileno()),
                     **({"BACURI_TEST_JPEG": "1"} if host == "rtsp-camera" else {})})
            child.close()
            loop = asyncio.get_running_loop()
            try:
                async with asyncio.timeout(80):
                    while command := await loop.sock_recv(parent, 1):
                        enabled = command == b"1"
                        if not enabled:
                            for writer in tuple(clients):
                                writer.close()
                        await loop.sock_sendall(parent, command)
                    return await process.wait()
            finally:
                if process.returncode is None:
                    with suppress(ProcessLookupError):
                        process.kill()
                    await process.wait()


if __name__ == "__main__":
    sys.exit(asyncio.run(scenario(*sys.argv[1:])))
