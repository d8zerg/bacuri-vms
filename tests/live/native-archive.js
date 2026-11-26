// Decodes the first archive part of a media session through Media Source Extensions without converting HEVC.
const [id, done] = arguments;
(async () => {
    const session = "/api/v1/media-sessions/" + id;
    const manifest = await (await fetch(session + "/manifest", { cache: "no-store" })).json();
    const part = manifest.parts[0];
    if (part?.codec !== "h265") throw Error("Native HEVC part expected");
    const bytes = async (name) => {
        const response = await fetch(session + "/parts/" + name, { cache: "no-store" });
        if (!response.ok) throw Error("Part " + response.status);
        return response.arrayBuffer();
    };
    const [init, media] = await Promise.all([bytes(part.init_id), bytes(part.media_id)]);
    const video = document.createElement("video");
    video.muted = true;
    document.body.append(video);
    const source = new MediaSource();
    video.src = URL.createObjectURL(source);
    try {
        await new Promise((resolve) => source.addEventListener("sourceopen", resolve, { once: true }));
        const buffer = source.addSourceBuffer('video/mp4; codecs="hvc1.1.6.L93.B0"');
        for (const data of [init, media]) {
            const appended = new Promise((resolve, reject) => {
                buffer.addEventListener("updateend", resolve, { once: true });
                buffer.addEventListener("error", () => reject(Error("Append failed")), { once: true });
            });
            buffer.appendBuffer(data);
            await appended;
        }
        if (video.readyState < HTMLMediaElement.HAVE_CURRENT_DATA)
            await new Promise((resolve, reject) => {
                video.addEventListener("loadeddata", resolve, { once: true });
                video.addEventListener("error", () => reject(Error("Decode failed")), { once: true });
            });
        return [video.videoWidth, video.videoHeight];
    } finally {
        URL.revokeObjectURL(video.src);
        video.remove();
    }
})().then(done, (error) => done({ error: String(error) }));
