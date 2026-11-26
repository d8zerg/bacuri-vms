// Plays the archive parts of a media session through Media Source Extensions. Each published block starts at a
// random access point with its own decode clock.
import type { components } from "../../../../build/contracts/api";
import { t } from "../i18n";
import { HttpFailure } from "./api";
import { describeError } from "./errors";

type Manifest = components["schemas"]["PlaybackManifest"];
type Part = Manifest["parts"][number] & { start: number; end: number };

// Preserves the fractional milliseconds carried by the archive index.
export function seconds(utc: string) {
  const [whole = "", fraction = ""] = utc.slice(0, -1).split(".");
  return Date.parse(whole + "Z") / 1000 + Number("0." + fraction);
}

function completed(target: EventTarget, name: string, signal: AbortSignal) {
  return new Promise<void>((resolve, reject) => {
    function finish(event: Event) {
      target.removeEventListener(name, finish);
      target.removeEventListener("error", finish);
      signal.removeEventListener("abort", finish);
      if (event.type === name) resolve();
      else
        reject(
          signal.reason instanceof Error
            ? signal.reason
            : new Error(t("media.decodeError")),
        );
    }
    signal.throwIfAborted();
    target.addEventListener(name, finish, { once: true });
    target.addEventListener("error", finish, { once: true });
    signal.addEventListener("abort", finish, { once: true });
  });
}

// The MIME type with the codec string of the sample entry in an initialization segment.
function codec(init: ArrayBuffer) {
  const view = new DataView(init);
  let sampleType = "";
  let start = 0,
    end = init.byteLength;
  for (const [type, skip] of [
    ["moov", 0],
    ["trak", 0],
    ["mdia", 0],
    ["minf", 0],
    ["stbl", 0],
    ["stsd", 8],
    ["sample", 78],
    ["config", 0],
  ] as const) {
    let found = false;
    while (start + 8 <= end) {
      const size = view.getUint32(start);
      if (size < 8 || start + size > end) break;
      const name = String.fromCharCode(...new Uint8Array(init, start + 4, 4));
      if (
        name === type ||
        (type === "sample" && ["avc1", "hvc1", "hev1"].includes(name)) ||
        (type === "config" &&
          name === (sampleType === "avc1" ? "avcC" : "hvcC"))
      ) {
        if (type === "sample") sampleType = name;
        end = start + size;
        start += 8 + skip;
        found = true;
        break;
      }
      start += size;
    }
    if (!found || start > end) throw new Error(t("media.invalidInit"));
  }
  if (sampleType !== "avc1") {
    if (end - start < 23 || view.getUint8(start) !== 1)
      throw new Error(t("media.invalidInit"));
    const flags = view.getUint8(start + 1);
    const space = ["", "A", "B", "C"][flags >>> 6] ?? "";
    let compat = 0,
      bits = view.getUint32(start + 2);
    for (let i = 0; i < 32; i++) {
      compat = compat * 2 + (bits & 1);
      bits >>>= 1;
    }
    const constraints = Array.from(new Uint8Array(init, start + 6, 6));
    while (constraints.length && constraints.at(-1) === 0) constraints.pop();
    const fields = [
      sampleType,
      space + (flags & 31),
      compat.toString(16),
      (flags & 32 ? "H" : "L") + view.getUint8(start + 12),
      ...constraints.map((x) => x.toString(16).padStart(2, "0")),
    ];
    return 'video/mp4; codecs="' + fields.join(".") + '"';
  }
  if (end - start < 4) throw new Error(t("media.invalidInit"));
  const profile = [...new Uint8Array(init, start + 1, 3)]
    .map((v) => v.toString(16).padStart(2, "0"))
    .join("");
  return 'video/mp4; codecs="avc1.' + profile + '"';
}

export class ArchivePlayer {
  origin = 0;
  private readonly parts: Part[];
  private controller: AbortController | undefined;
  private url: string | null = null;

  constructor(
    private readonly video: HTMLVideoElement,
    private readonly session: string,
    manifest: Manifest,
    private readonly onStatus: (message: string) => void = () => {},
  ) {
    this.parts = manifest.parts.map((part) => ({
      ...part,
      start: seconds(part.interval.start),
      end: seconds(part.interval.end),
    }));
  }

  stop() {
    this.controller?.abort();
    this.video.pause();
    this.video.removeAttribute("src");
    this.video.load();
    if (this.url) URL.revokeObjectURL(this.url);
    this.url = null;
  }

  async seek(utc: string | number) {
    const at = typeof utc === "string" ? seconds(utc) : utc;
    const index = this.parts.findIndex((p) => p.start <= at && at < p.end);
    if (index < 0) {
      this.stop();
      this.onStatus(t("media.positionUnavailable"));
      return false;
    }
    const relative = at - this.origin;
    const buffered = this.video.buffered;
    for (let i = 0; i < buffered.length; ++i) {
      if (
        this.controller &&
        relative >= buffered.start(i) &&
        relative < buffered.end(i)
      ) {
        this.video.pause();
        if (Math.abs(this.video.currentTime - relative) > 0.000001) {
          const settled = completed(
            this.video,
            "seeked",
            this.controller.signal,
          );
          this.video.currentTime = relative;
          await settled;
        }
        return true;
      }
    }
    this.stop();
    const controller = new AbortController();
    this.controller = controller;
    const signal = controller.signal;
    const source = new MediaSource();
    const opened = completed(source, "sourceopen", signal);
    this.url = URL.createObjectURL(source);
    this.video.src = this.url;
    await opened;
    this.origin = this.parts[index]!.start;
    let buffer: SourceBuffer | undefined;
    let mime = "";
    let next = index;
    const bytes = async (id: string) => {
      const response = await fetch(
        "/api/v1/media-sessions/" + this.session + "/parts/" + id,
        { cache: "no-store", signal },
      );
      if (!response.ok) throw await HttpFailure.fromResponse(response);
      return response.arrayBuffer();
    };
    const append = async (target: SourceBuffer, data: ArrayBuffer) => {
      const ready = completed(target, "updateend", signal);
      target.appendBuffer(data);
      await ready;
    };
    const appendBlock = async (part: Part) => {
      const init = await bytes(part.init_id);
      const format = codec(init);
      if (!MediaSource.isTypeSupported(format))
        throw new Error(t("media.formatUnsupported", { format }));
      if (!buffer) buffer = source.addSourceBuffer(format);
      else if (mime !== format) buffer.changeType(format);
      mime = format;
      buffer.timestampOffset = part.start - this.origin;
      await append(buffer, init);
      await append(buffer, await bytes(part.media_id));
    };
    await appendBlock(this.parts[next++]!);
    if (this.video.readyState < HTMLMediaElement.HAVE_METADATA)
      await completed(this.video, "loadedmetadata", signal);
    if (at > this.origin) {
      const settled = completed(this.video, "seeked", signal);
      this.video.currentTime = at - this.origin;
      await settled;
    }
    this.video.addEventListener(
      "waiting",
      () => {
        const position = this.origin + this.video.currentTime;
        const following = this.parts.findIndex(
          (p) => p.start > position + 0.08,
        );
        const previous = this.parts[following - 1];
        if (following > 0 && previous && position >= previous.end - 0.08) {
          this.video.pause();
          this.onStatus(
            t("media.gapContinues", {
              at: this.parts[following]!.interval.start,
            }),
          );
        }
      },
      { signal },
    );
    let busy = false;
    const advance = async () => {
      if (busy || signal.aborted || source.readyState !== "open" || !buffer)
        return;
      busy = true;
      try {
        const now = this.video.currentTime;
        if (buffer.buffered.length && now - buffer.buffered.start(0) > 15) {
          const removed = completed(buffer, "updateend", signal);
          buffer.remove(0, now - 10);
          await removed;
        }
        while (
          next < this.parts.length &&
          this.parts[next]!.start - this.origin < now + 3
        ) {
          await appendBlock(this.parts[next++]!);
        }
        if (next === this.parts.length && source.readyState === "open")
          source.endOfStream();
      } catch (error) {
        if (!signal.aborted) {
          this.video.pause();
          this.onStatus(
            error instanceof HttpFailure
              ? describeError(error)
              : error instanceof Error
                ? error.message
                : t("media.partUnavailable"),
          );
        }
      } finally {
        busy = false;
      }
    };
    for (const name of ["timeupdate", "waiting"])
      this.video.addEventListener(name, () => void advance(), { signal });
    void advance();
    return true;
  }
}
