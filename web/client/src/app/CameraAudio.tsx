import { useEffect, useRef, useState } from "react";
import type { AudioRequest, Point, Services } from "../contracts/workspace";
import { IconButton } from "../ui-kit/FormControls";
import { pause } from "../workflows/pause";
import { cameraError } from "./cameraError";
import { useT, type MessageKey } from "../i18n";

type Direction = "listen" | "record" | "talk";
type Exchange = (
  request: Omit<AudioRequest, "device_id">,
  signal?: AbortSignal,
) => ReturnType<Services["audio"]>;

function startAudio(context: AudioContext, signal: AbortSignal) {
  const timer = new AbortController();
  return Promise.race([
    context.resume(),
    pause(5000, AbortSignal.any([signal, timer.signal])).then(() => {
      throw new Error("Audio device unavailable");
    }),
  ]).finally(() => timer.abort());
}

function acquireMicrophone(signal: AbortSignal) {
  return new Promise<MediaStream>((resolve, reject) => {
    signal.throwIfAborted();
    const abort = () => reject(new DOMException("Aborted", "AbortError"));
    signal.addEventListener("abort", abort, { once: true });
    navigator.mediaDevices
      .getUserMedia({ audio: true })
      .then((stream) => {
        if (signal.aborted) stream.getTracks().forEach((track) => track.stop());
        else resolve(stream);
      }, reject)
      .finally(() => signal.removeEventListener("abort", abort));
  });
}

async function listen(
  exchange: Exchange,
  context: AudioContext,
  position: number | undefined,
  signal: AbortSignal,
) {
  await startAudio(context, signal);
  let cursor = position,
    scheduled = context.currentTime;
  while (!signal.aborted) {
    const chunk = await exchange(
      {
        mode: position === undefined ? "listen" : "archive",
        ...(cursor !== undefined && { at_us: cursor }),
      },
      signal,
    );
    signal.throwIfAborted();
    if (!chunk.pcm || chunk.start_us === undefined) throw new Error("empty");
    const bytes = Uint8Array.from(atob(chunk.pcm), (c) => c.charCodeAt(0));
    const samples = new DataView(bytes.buffer);
    const buffer = context.createBuffer(1, bytes.length / 2, 8000);
    const output = buffer.getChannelData(0);
    for (let i = 0; i < output.length; i++)
      output[i] = samples.getInt16(i * 2, true) / 32768;
    const source = context.createBufferSource();
    source.buffer = buffer;
    source.connect(context.destination);
    scheduled = Math.max(scheduled, context.currentTime + 0.015);
    source.start(scheduled);
    scheduled += buffer.duration;
    cursor = chunk.start_us + output.length * 125;
    if (scheduled - context.currentTime > 0.3)
      await pause((scheduled - context.currentTime - 0.15) * 1000, signal);
  }
}
async function talk(
  exchange: Exchange,
  context: AudioContext,
  signal: AbortSignal,
) {
  const session = crypto.randomUUID();
  const stream = await acquireMicrophone(signal);
  const stop = () => stream.getTracks().forEach((track) => track.stop());
  signal.addEventListener("abort", stop, { once: true });
  try {
    signal.throwIfAborted();
    await context.audioWorklet.addModule("/assets/speech-capture.js");
    signal.throwIfAborted();
    const input = context.createMediaStreamSource(stream);
    const processor = new AudioWorkletNode(context, "speech-capture");
    input.connect(processor);
    processor.connect(context.destination);
    await startAudio(context, signal);
    signal.throwIfAborted();
    let sequence = 0,
      pending = 0;
    let chain = Promise.resolve();
    await new Promise<void>((resolve, reject) => {
      const closed = () => resolve();
      signal.addEventListener("abort", closed, { once: true });
      processor.port.onmessage = ({ data }: { data: ArrayBuffer }) => {
        if (signal.aborted) return;
        if (pending >= 3) {
          reject(new Error("backpressure"));
          return;
        }
        pending++;
        chain = chain
          .then(async () => {
            signal.throwIfAborted();
            const pcm = btoa(String.fromCharCode(...new Uint8Array(data)));
            await exchange(
              { mode: "talk", session, sequence: sequence++, pcm },
              signal,
            );
          })
          .catch(reject)
          .finally(() => pending--);
      };
      stream.addEventListener("inactive", closed, { once: true });
      stream
        .getAudioTracks()
        .forEach((track) =>
          track.addEventListener("ended", closed, { once: true }),
        );
    });
    processor.disconnect();
  } finally {
    stop();
    signal.removeEventListener("abort", stop);
    void exchange({ mode: "mute", session }).catch(() => {});
  }
}

export function CameraAudio({
  services,
  site,
  point,
  capabilities,
  position,
}: {
  services: Services;
  site: string;
  point: string;
  capabilities: NonNullable<Point["audio"]>;
  position: number | undefined;
}) {
  const t = useT();
  const running = useRef(new Map<Direction, AbortController>());
  const [active, setActive] = useState<Partial<Record<Direction, boolean>>>({});
  const [error, setError] = useState("");
  useEffect(() => {
    const operations = running.current;
    const stop = () => operations.forEach((controller) => controller.abort());
    window.addEventListener("pagehide", stop);
    return () => {
      window.removeEventListener("pagehide", stop);
      stop();
    };
  }, []);
  const exchange: Exchange = (request, signal) =>
    services.audio(
      site,
      point,
      { ...request, device_id: capabilities.device_id },
      signal,
    );
  async function toggle(direction: Direction) {
    const existing = running.current.get(direction);
    if (existing) {
      existing.abort();
      return;
    }
    const controller = new AbortController(),
      signal = controller.signal;
    running.current.set(direction, controller);
    setError("");
    setActive((prev) => ({ ...prev, [direction]: true }));
    let context: AudioContext | undefined;
    const timeout =
      direction === "talk"
        ? window.setTimeout(() => controller.abort(), 30000)
        : undefined;
    try {
      if (direction === "record") {
        while (!signal.aborted) {
          await exchange({ mode: "record" }, signal);
          await pause(850, signal);
        }
      } else {
        context = new AudioContext();
        signal.addEventListener(
          "abort",
          () => {
            void context?.close().catch(() => {});
          },
          { once: true },
        );
        if (direction === "talk") await talk(exchange, context, signal);
        else await listen(exchange, context, position, signal);
      }
    } catch (e) {
      if (!signal.aborted)
        setError(
          cameraError(e) ??
            (direction === "talk"
              ? t("audio.talkFailed")
              : t("audio.unavailable")),
        );
    } finally {
      clearTimeout(timeout);
      controller.abort();
      if (direction === "record")
        void exchange({ mode: "record_stop" }).catch(() => {});
      running.current.delete(direction);
      setActive((prev) => ({ ...prev, [direction]: false }));
    }
  }
  const items: [Direction, boolean, MessageKey, MessageKey][] = [
    [
      "listen",
      position === undefined ? capabilities.listen : capabilities.archive,
      "audio.listen",
      "audio.mute",
    ],
    [
      "record",
      position === undefined && capabilities.record,
      "audio.record",
      "audio.stopRecording",
    ],
    [
      "talk",
      position === undefined && capabilities.talk,
      "audio.talk",
      "audio.stopTalking",
    ],
  ];
  if (!items.some(([, visible]) => visible)) return null;
  return (
    <div className="cc-audio" role="group" aria-label={t("audio.label")}>
      {items
        .filter(([, visible]) => visible)
        .map(([direction, , off, on]) => (
          <IconButton
            icon={
              { listen: "headphones", record: "record-sound", talk: "mic" }[
                direction
              ] as "headphones" | "record-sound" | "mic"
            }
            label={t(active[direction] ? on : off)}
            data-direction={direction}
            key={direction}
            aria-pressed={!!active[direction]}
            onClick={() => void toggle(direction)}
          />
        ))}
      {error && <span role="alert">{error}</span>}
    </div>
  );
}
