const [after, site, done] = arguments;
const url = new URL("/api/v1/sites/" + site + "/alerts/stream", location.href);
url.protocol = "wss:";
const probe = { frames: [], code: 0 };
const socket = new WebSocket(url, "bacuri.vms.alerts.v1");
window.probe = probe;
probe.socket = socket;
socket.onmessage = (event) => probe.frames.push(JSON.parse(event.data));
socket.onclose = (event) => {
    probe.code = event.code;
};
socket.onerror = () => done(false);
socket.onopen = () => {
    if (after !== null) socket.send(JSON.stringify({ type: "subscribe", after }));
    done(true);
};
