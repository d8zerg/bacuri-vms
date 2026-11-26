import type { ArchivePlayer } from "/archive/player.js";
import type { Target } from "../contracts/workspace";
import { api, data, HttpFailure } from "./api";
import { t } from "../i18n";
export function mediaFailure(error: unknown) {
  if (error instanceof HttpFailure) {
    if (error.code === "video_bandwidth_limit")
      return t("media.bandwidthLimit");
    if (error.code === "video_session_limit") return t("media.sessionLimit");
    if (error.code === "video_transcoder_limit")
      return t("media.transcoderLimit");
    if (error.code === "video_source_unavailable")
      return t("media.sourceUnavailable");
    if (error.code === "pending_catalog") return t("media.pendingCatalog");
    if (error.code === "recording_gap") return t("media.recordingGap");
    if (error.status === 401) return t("media.sessionExpired");
    if (error.status === 403 || error.status === 404)
      return t("media.forbidden");
    if (error.status === 410) return t("media.viewExpired");
    if (error.status === 429) return t("media.viewLimit");
    if (error.status === 503) return t("media.serviceUnavailable");
  }
  return t("video.unavailable");
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
  format?: (description: string) => void,
  renew?: () => void,
  archiveOrigin?: (
    utcSeconds: number,
    seek: (at: string) => Promise<number>,
  ) => void,
) {
  const supportsHevc =
    target.mode === "archive"
      ? (globalThis.MediaSource?.isTypeSupported(
          'video/mp4; codecs="hvc1.1.6.L93.B0"',
        ) ?? false)
      : (RTCRtpReceiver.getCapabilities("video")?.codecs.some(
          (c) => c.mimeType.toLowerCase() === "video/h265",
        ) ?? false);
  for (const hevc of supportsHevc ? [true, false] : [false]) {
    const attempt = new AbortController();
    const scoped = AbortSignal.any([signal, attempt.signal]);
    let native = false,
      ready = false;
    try {
      await connect(
        video,
        siteId,
        target,
        header,
        scoped,
        (message) => {
          if (scoped.aborted) return;
          if (native && !ready) attempt.abort();
          else fail(message);
        },
        {
          hevc,
          markNative: () => {
            native = true;
          },
        },
        format,
        renew,
        archiveOrigin,
      );
      ready = true;
      return;
    } catch (error) {
      attempt.abort();
      if (signal.aborted || !hevc || !native) throw error;
    }
  }
}

async function connect(
  video: HTMLVideoElement,
  siteId: string,
  target: Target,
  header: { "X-CSRF-Token": string },
  signal: AbortSignal,
  fail: (message: string) => void,
  decoder: { hevc: boolean; markNative: () => void },
  format?: (description: string) => void,
  renew?: () => void,
  archiveOrigin?: (
    utcSeconds: number,
    seek: (at: string) => Promise<number>,
  ) => void,
) {
  signal.throwIfAborted();
  const open = async () =>
    data(
      await api.POST("/sites/{siteId}/points/{pointId}/media-sessions", {
        params: {
          path: { siteId, pointId: target.point },
          header: { ...header, "Idempotency-Key": crypto.randomUUID() },
        },
        body:
          target.mode === "archive"
            ? {
                mode: "archive",
                hevc: decoder.hevc,
                accept_masked: true,
                interval: target.interval,
                ...(target.binding_revision
                  ? { binding_revision: target.binding_revision }
                  : {}),
              }
            : {
                mode: "live",
                quality: target.quality ?? "auto",
                cell_width: Math.max(
                  1,
                  Math.min(
                    16384,
                    Math.round(target.width ?? video.clientWidth),
                  ),
                ),
                hevc: decoder.hevc,
                accept_masked: true,
              },
        signal: AbortSignal.timeout(10000),
      }),
    );
  async function openAvailable() {
    const deadline = performance.now() + 6000;
    for (;;) {
      signal.throwIfAborted();
      try {
        const result = await open();
        if (
          target.mode === "live" &&
          target.requireSecondary &&
          result.video?.profile !== "secondary"
        ) {
          await api.DELETE("/media-sessions/{mediaSessionId}", {
            params: { path: { mediaSessionId: result.id }, header },
          });
          throw new HttpFailure(503, "video_source_unavailable");
        }
        return result;
      } catch (error) {
        if (
          !(error instanceof HttpFailure) ||
          error.code !== "video_source_unavailable" ||
          performance.now() >= deadline
        )
          throw error;
        await event(AbortSignal.timeout(350), "abort", signal);
      }
    }
  }
  const session = await openAvailable();
  const path = { mediaSessionId: session.id };
  if (session.video) {
    const v = session.video;
    if (v.codec === "h265") decoder.markNative();
    const reason =
      v.reason === "privacy_mask"
        ? " · " + t("media.variant.masked")
        : v.reason === "bandwidth"
          ? " · " + t("media.variant.bandwidth")
          : v.converted
            ? " · " + t("media.variant.converted")
            : v.reason === "compatible_profile"
              ? " · " + t("media.variant.compatibleProfile")
              : "";
    format?.(
      (v.profile === "main"
        ? t("media.profile.main")
        : t("media.profile.secondary")) +
        " · " +
        (v.codec === "h265" ? "HEVC" : "H.264") +
        " · " +
        v.width +
        "×" +
        v.height +
        reason,
    );
  }
  function releaseSession() {
    void api
      .DELETE("/media-sessions/{mediaSessionId}", {
        params: { path, header },
        keepalive: true,
      })
      .catch(() => {});
  }
  if (signal.aborted) {
    releaseSession();
    signal.throwIfAborted();
  }
  let peer: RTCPeerConnection | undefined, player: ArchivePlayer | undefined;
  const expire = setTimeout(
    () =>
      target.mode === "live" && renew ? renew() : fail(t("media.viewExpired")),
    Date.parse(session.expires_at) - Date.now(),
  );
  const timeout = setTimeout(() => fail(t("media.connectTimeout")), 12000);
  let verifyTimer: ReturnType<typeof setTimeout>;
  function stop() {
    clearTimeout(verifyTimer);
    clearTimeout(timeout);
    clearTimeout(expire);
    peer?.close();
    player?.stop();
    video.pause();
    video.srcObject = null;
    releaseSession();
  }
  signal.addEventListener("abort", stop, { once: true });
  video.addEventListener("error", () => fail(t("media.decodeError")), {
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
        if (!signal.aborted) fail(t("media.interrupted"));
      }
    }
    verifyTimer = setTimeout(() => void verify(), 2000);
    const part = manifest.parts.find(
      (p) =>
        Date.parse(target.position) >= Date.parse(p.interval.start) &&
        Date.parse(target.position) < Date.parse(p.interval.end),
    );
    if (part?.codec === "h265") decoder.markNative();
    if (part?.codec)
      format?.(
        (part.codec === "h265" ? "HEVC" : "H.264") +
          (part.source_codec && part.source_codec !== part.codec
            ? " · " + t("media.variant.converted")
            : " · " + t("monitor.archive")),
      );
    player = new ArchivePlayer(video, session.id, manifest, fail);
    if (!(await player.seek(target.position))) throw Error("Frame unavailable");
    const activePlayer = player;
    archiveOrigin?.(player.origin, async (at) => {
      if (!(await activePlayer.seek(at))) throw Error("Frame unavailable");
      return activePlayer.origin;
    });
  } else {
    const pc = new RTCPeerConnection({ iceServers: [] });
    peer = pc;
    const transceiver = pc.addTransceiver("video", { direction: "recvonly" });
    const codecs = RTCRtpReceiver.getCapabilities("video")?.codecs.filter(
      (c) =>
        session.video?.codec === "h265"
          ? c.mimeType.toLowerCase() === "video/h265"
          : c.mimeType.toLowerCase() === "video/h264" &&
            /packetization-mode=1/.test(c.sdpFmtpLine ?? ""),
    );
    if (!codecs?.length) throw Error("Negotiated decoder unavailable");
    transceiver.setCodecPreferences(codecs);
    pc.ontrack = ({ track }) => {
      if (!signal.aborted) {
        video.srcObject = new MediaStream([track]);
        void video.play().catch(() => fail(t("media.playFailed")));
      }
    };
    pc.onconnectionstatechange = () => {
      if (["failed", "disconnected", "closed"].includes(pc.connectionState))
        fail(t("media.cameraLost"));
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
