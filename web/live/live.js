import { byId, csrf, initialize, json, post, request, status } from "/session.js";

const video = byId("video");
let active;

async function stop(message) {
    const current = active;
    if (!current) return;
    active = null;
    current.controller.abort();
    clearTimeout(current.timer);
    current.peer.close();
    video.srcObject = null;
    byId("stop").disabled = true;
    status(message);
    if (current.session) await closeSession(current.session);
}

async function closeSession(id) {
    try {
        await request("/media-sessions/" + id, {
            method: "DELETE",
            headers: { "X-CSRF-Token": csrf },
            keepalive: true,
        });
    } catch {
        /* Server expiry also releases disconnected viewers. */
    }
}

function gathered(peer, signal) {
    if (peer.iceGatheringState === "complete") return Promise.resolve();
    return new Promise((resolve, reject) => {
        signal.throwIfAborted();
        const changed = () => {
            if (peer.iceGatheringState === "complete") {
                peer.removeEventListener("icegatheringstatechange", changed);
                signal.removeEventListener("abort", cancelled);
                resolve();
            }
        };
        const cancelled = () => {
            peer.removeEventListener("icegatheringstatechange", changed);
            reject(signal.reason);
        };
        peer.addEventListener("icegatheringstatechange", changed);
        signal.addEventListener("abort", cancelled, { once: true });
    });
}

async function watch() {
    const peer = new RTCPeerConnection({ iceServers: [] });
    const controller = new AbortController();
    const current = { peer, controller };
    active = current;
    const { signal } = controller;
    const path = "/sites/" + byId("site").value + "/points/" + byId("point").value;
    byId("stop").disabled = false;
    status("Подключение к камере");
    current.timer = setTimeout(() => {
        if (active === current) void stop("Время подключения истекло");
    }, 12000);
    try {
        const transceiver = peer.addTransceiver("video", { direction: "recvonly" });
        const codecs = RTCRtpReceiver.getCapabilities("video").codecs.filter(
            (codec) =>
                codec.mimeType.toLowerCase() === "video/h264" &&
                /packetization-mode=1/.test(codec.sdpFmtpLine),
        );
        if (!codecs.length) throw new Error("Браузер не поддерживает H.264 для WebRTC");
        transceiver.setCodecPreferences(codecs);
        peer.ontrack = ({ track }) => {
            if (active === current) video.srcObject = new MediaStream([track]);
        };
        peer.onconnectionstatechange = () => {
            if (active === current && ["failed", "disconnected", "closed"].includes(peer.connectionState))
                void stop("Соединение закрыто. Для повторного подключения нажмите «Смотреть».");
        };
        video.addEventListener(
            "playing",
            () => {
                clearTimeout(current.timer);
                status("Прямой эфир");
            },
            { once: true, signal },
        );
        const session = await json(
            path + "/media-sessions",
            post({ mode: "live" }, { "Idempotency-Key": crypto.randomUUID() }),
        );
        current.session = session.id;
        if (signal.aborted) {
            await closeSession(session.id);
            return;
        }
        await peer.setLocalDescription(await peer.createOffer());
        await gathered(peer, signal);
        const answer = await request("/media-sessions/" + session.id + "/offer", {
            method: "POST",
            headers: { "Content-Type": "application/sdp", "X-CSRF-Token": csrf },
            body: peer.localDescription.sdp,
            signal,
        });
        await peer.setRemoteDescription({ type: "answer", sdp: await answer.text() });
    } catch (error) {
        const messages = {
            not_found: "Доступ к точке закрыт",
            media_session_closed: "Время сеанса истекло. Откройте эфир ещё раз.",
            media_unavailable: "Сервис прямого эфира недоступен",
            rate_limited: "Достигнут лимит сеансов. Закройте один из просмотров и повторите попытку.",
            unauthenticated: "Сеанс входа истёк. Обновите страницу и войдите ещё раз.",
        };
        if (active === current) await stop(messages[error.message] ?? error.message);
    }
}
byId("search").onsubmit = async (event) => {
    event.preventDefault();
    if (active) void stop("");
    await watch();
};
byId("stop").onclick = () => void stop("Просмотр остановлен");
for (const id of ["site", "point"]) byId(id).addEventListener("change", () => void stop(""));
window.addEventListener("pagehide", () => void stop(""));
await initialize();
