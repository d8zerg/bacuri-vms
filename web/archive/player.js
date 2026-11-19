// Each published block starts at a random access point with its own decode clock.
const api = "/api/v1";
export async function request(path, options = {}) {
    const response = await fetch(api + path, { cache: "no-store", ...options });
    if (!response.ok) {
        const problem = await response.json();
        throw new Error(problem.code || String(response.status));
    }
    return response;
}

// Preserve the fractional milliseconds carried by the archive index.
export function seconds(utc) {
    const [whole, fraction = ""] = utc.slice(0, -1).split(".");
    return Date.parse(whole + "Z") / 1000 + Number("0." + fraction);
}

function completed(target, name, signal) {
    return new Promise((resolve, reject) => {
        function finish(event) {
            target.removeEventListener(name, finish);
            target.removeEventListener("error", finish);
            signal.removeEventListener("abort", finish);
            if (event.type === name) resolve();
            else reject(signal.reason || new Error("Ошибка декодирования"));
        }
        signal.throwIfAborted();
        target.addEventListener(name, finish, { once: true });
        target.addEventListener("error", finish, { once: true });
        signal.addEventListener("abort", finish, { once: true });
    });
}

function codec(init) {
    const view = new DataView(init);
    let start = 0,
        end = init.byteLength;
    for (const [type, skip] of [
        ["moov", 0],
        ["trak", 0],
        ["mdia", 0],
        ["minf", 0],
        ["stbl", 0],
        ["stsd", 8],
        ["avc1", 78],
        ["avcC", 0],
    ]) {
        let found = false;
        while (start + 8 <= end) {
            const size = view.getUint32(start);
            if (size < 8 || start + size > end) break;
            if (String.fromCharCode(...new Uint8Array(init, start + 4, 4)) === type) {
                end = start + size;
                start += 8 + skip;
                found = true;
                break;
            }
            start += size;
        }
        if (!found || start > end) throw new Error("Некорректная инициализация H.264");
    }
    if (end - start < 4) throw new Error("Неполное описание H.264");
    const profile = [...new Uint8Array(init, start + 1, 3)]
        .map((v) => v.toString(16).padStart(2, "0"))
        .join("");
    return 'video/mp4; codecs="avc1.' + profile + '"';
}

export class ArchivePlayer {
    constructor(video, session, manifest, onStatus = () => {}) {
        this.video = video;
        this.session = session;
        this.parts = manifest.parts.map((part) => ({
            ...part,
            start: seconds(part.interval.start),
            end: seconds(part.interval.end),
        }));
        this.onStatus = onStatus;
    }
    stop() {
        this.controller?.abort();
        this.video.pause();
        this.video.removeAttribute("src");
        this.video.load();
        if (this.url) URL.revokeObjectURL(this.url);
        this.url = null;
    }
    async seek(utc) {
        const at = typeof utc === "string" ? seconds(utc) : utc;
        const index = this.parts.findIndex((p) => p.start <= at && at < p.end);
        if (index < 0) {
            this.stop();
            this.onStatus("В выбранной позиции запись недоступна");
            return false;
        }
        const relative = at - this.origin;
        const buffered = this.video.buffered;
        for (let i = 0; i < buffered.length; ++i) {
            if (relative >= buffered.start(i) && relative < buffered.end(i)) {
                this.video.pause();
                if (Math.abs(this.video.currentTime - relative) > 0.000001) {
                    const settled = completed(this.video, "seeked", this.controller.signal);
                    this.video.currentTime = relative;
                    await settled;
                }
                return true;
            }
        }
        this.stop();
        this.controller = new AbortController();
        const signal = this.controller.signal;
        const source = new MediaSource();
        const opened = completed(source, "sourceopen", signal);
        this.url = URL.createObjectURL(source);
        this.video.src = this.url;
        await opened;
        this.origin = this.parts[index].start;
        let buffer,
            mime,
            next = index;
        const bytes = async (id) =>
            (await request("/media-sessions/" + this.session + "/parts/" + id, { signal })).arrayBuffer();
        const append = async (data) => {
            const ready = completed(buffer, "updateend", signal);
            buffer.appendBuffer(data);
            await ready;
        };
        const appendBlock = async (part) => {
            const init = await bytes(part.init_id);
            const format = codec(init);
            if (!MediaSource.isTypeSupported(format)) throw new Error("Браузер не поддерживает " + format);
            if (!buffer) buffer = source.addSourceBuffer(format);
            else if (mime !== format) buffer.changeType(format);
            mime = format;
            buffer.timestampOffset = part.start - this.origin;
            await append(init);
            await append(await bytes(part.media_id));
        };
        await appendBlock(this.parts[next++]);
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
                const at = this.origin + this.video.currentTime;
                const following = this.parts.findIndex((p) => p.start > at + 0.08);
                if (following > 0 && at >= this.parts[following - 1].end - 0.08) {
                    this.video.pause();
                    this.onStatus("Разрыв записи. Продолжение с " + this.parts[following].interval.start);
                }
            },
            { signal },
        );
        let busy = false;
        const advance = async () => {
            if (busy || signal.aborted || source.readyState !== "open") return;
            busy = true;
            try {
                const now = this.video.currentTime;
                if (buffer.buffered.length && now - buffer.buffered.start(0) > 15) {
                    const removed = completed(buffer, "updateend", signal);
                    buffer.remove(0, now - 10);
                    await removed;
                }
                while (next < this.parts.length && this.parts[next].start - this.origin < now + 3) {
                    await appendBlock(this.parts[next++]);
                }
                if (next === this.parts.length && source.readyState === "open") source.endOfStream();
            } catch (error) {
                if (!signal.aborted) {
                    this.video.pause();
                    this.onStatus(error.message);
                }
            } finally {
                busy = false;
            }
        };
        for (const name of ["timeupdate", "waiting"])
            this.video.addEventListener(name, advance, { signal });
        void advance();
        return true;
    }
}
