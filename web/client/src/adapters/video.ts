import type { ArchivePlayer } from "/archive/player.js";
import type { Target } from "../contracts/workspace";
import { api, data, HttpFailure } from "./api";
export function mediaFailure(error: unknown) {
  if (error instanceof HttpFailure) {
    if (error.code === "pending_catalog")
      return "Запись добавляется в каталог. Повторите проверку.";
    if (error.code === "recording_gap")
      return "В выбранном интервале пропуск записи.";
    if (error.status === 401) return "Сеанс входа истёк. Войдите снова.";
    if (error.status === 403 || error.status === 404)
      return "Нет доступа к точке или записи.";
    if (error.status === 410)
      return "Время просмотра истекло. Откройте видео снова.";
    if (error.status === 429)
      return "Достигнут лимит просмотров. Закройте другой просмотр и повторите попытку.";
    if (error.status === 503)
      return "Сервис видео недоступен. Повторите подключение.";
  }
  return "Видео недоступно. Повторите подключение.";
}
function event(target: EventTarget, name: string, signal: AbortSignal) {
  return new Promise<void>((resolve, reject) => {
    signal.throwIfAborted();
    function done() {
      signal.removeEventListener("abort", cancel);
      resolve();
    }
    function cancel() {
      target.removeEventListener(name, done);
      reject(new DOMException("Aborted", "AbortError"));
    }
    target.addEventListener(name, done, { once: true });
    signal.addEventListener("abort", cancel, { once: true });
  });
}
export async function attach(
  video: HTMLVideoElement,
  siteId: string,
  target: Target,
  header: { "X-CSRF-Token": string },
  signal: AbortSignal,
  fail: (reason: string) => void,
) {
  const session = data(
    await api.POST("/sites/{siteId}/points/{pointId}/media-sessions", {
      params: {
        path: { siteId, pointId: target.point },
        header: { ...header, "Idempotency-Key": crypto.randomUUID() },
      },
      body:
        target.mode === "archive"
          ? {
              mode: "archive",
              interval: target.interval,
              ...(target.binding_revision
                ? { binding_revision: target.binding_revision }
                : {}),
            }
          : { mode: "live" },
      signal: AbortSignal.timeout(10000),
    }),
  );
  const path = { mediaSessionId: session.id };
  let peer: RTCPeerConnection | undefined, player: ArchivePlayer | undefined;
  const expire = setTimeout(
    () => fail("Время просмотра истекло. Откройте видео снова."),
    Date.parse(session.expires_at) - Date.now(),
  );
  const timeout = setTimeout(
    () => fail("Время подключения истекло. Повторите подключение."),
    12000,
  );
  let verifyTimer: ReturnType<typeof setTimeout>;
  function stop() {
    clearTimeout(verifyTimer);
    clearTimeout(timeout);
    clearTimeout(expire);
    peer?.close();
    player?.stop();
    video.pause();
    video.srcObject = null;
    void api
      .DELETE("/media-sessions/{mediaSessionId}", {
        params: { path, header },
        keepalive: true,
      })
      .catch(() => {});
  }
  if (signal.aborted) {
    stop();
    signal.throwIfAborted();
  }
  signal.addEventListener("abort", stop, { once: true });
  video.addEventListener("error", () => fail("Ошибка декодирования видео"), {
    signal,
  });
  if (target.mode === "archive") {
    const [{ ArchivePlayer }, manifest] = await Promise.all([
      import("/archive/player.js"),
      api
        .GET("/media-sessions/{mediaSessionId}/manifest", {
          params: { path },
          signal,
        })
        .then(data),
    ]);
    signal.throwIfAborted();
    async function verify() {
      try {
        data(
          await api.GET("/media-sessions/{mediaSessionId}/manifest", {
            params: { path },
            signal: AbortSignal.any([signal, AbortSignal.timeout(1500)]),
          }),
        );
        if (!signal.aborted)
          verifyTimer = setTimeout(() => void verify(), 2000);
      } catch {
        if (!signal.aborted)
          fail("Просмотр прерван. Проверьте доступ и подключитесь снова.");
      }
    }
    verifyTimer = setTimeout(() => void verify(), 2000);
    player = new ArchivePlayer(video, session.id, manifest, fail);
    if (!(await player.seek(target.position))) throw Error("Frame unavailable");
  } else {
    const pc = new RTCPeerConnection({ iceServers: [] });
    peer = pc;
    const transceiver = pc.addTransceiver("video", { direction: "recvonly" });
    const codecs = RTCRtpReceiver.getCapabilities("video")?.codecs.filter(
      (c) =>
        c.mimeType.toLowerCase() === "video/h264" &&
        /packetization-mode=1/.test(c.sdpFmtpLine ?? ""),
    );
    if (!codecs?.length) throw Error("H.264 unavailable");
    transceiver.setCodecPreferences(codecs);
    pc.ontrack = ({ track }) => {
      if (!signal.aborted) {
        video.srcObject = new MediaStream([track]);
        void video.play().catch(() => fail("Не удалось начать просмотр"));
      }
    };
    pc.onconnectionstatechange = () => {
      if (["failed", "disconnected", "closed"].includes(pc.connectionState))
        fail("Связь с камерой потеряна. Повторите подключение.");
    };
    await pc.setLocalDescription(await pc.createOffer());
    while (pc.iceGatheringState !== "complete")
      await event(pc, "icegatheringstatechange", signal);
    const answer = await fetch(
      "/api/v1/media-sessions/" + session.id + "/offer",
      {
        method: "POST",
        headers: { ...header, "Content-Type": "application/sdp" },
        body: pc.localDescription!.sdp,
        signal,
      },
    );
    if (!answer.ok) throw new HttpFailure(answer.status, "");
    await pc.setRemoteDescription({ type: "answer", sdp: await answer.text() });
  }
  if (video.readyState < HTMLMediaElement.HAVE_CURRENT_DATA)
    await event(video, "loadeddata", signal);
  signal.throwIfAborted();
  if (!video.videoWidth) throw Error("No decoded frame");
  clearTimeout(timeout);
}
