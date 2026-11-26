// Plays the archive of the point open in the monitor of the workspace. The page opens at the start of the
// selection; seeks go through the position slider of the archive timeline.
const [selection, done] = arguments;
(async () => {
    const player = () => document.querySelector(".ws-player");
    const status = () => player()?.querySelector('[role="status"]')?.textContent ?? "no player";
    const wait = async (predicate, what) => {
        const end = performance.now() + 12000;
        let value;
        while (!(value = predicate())) {
            if (performance.now() > end) throw new Error("Timed out: " + what + ": " + status());
            await new Promise((resolve) => setTimeout(resolve, 25));
        }
        return value;
    };
    const video = await wait(() => player()?.querySelector("video"), "archive player");
    async function frame(start, action, predicate) {
        const observed = new Promise((resolve, reject) => {
            let id;
            const timer = setTimeout(() => {
                video.cancelVideoFrameCallback(id);
                reject(
                    new Error(
                        "No reference frame: " + status() + " ready=" + video.readyState + " error=" +
                            video.error?.message,
                    ),
                );
            }, 12000);
            const canvas = document.createElement("canvas");
            canvas.width = 5;
            canvas.height = 2;
            const ctx = canvas.getContext("2d", { willReadFrequently: true });
            function observe(now, meta) {
                ctx.drawImage(video, 0, 0, 5, 2);
                const rgb = Array.from(ctx.getImageData(2, 1, 1, 1).data.slice(0, 3));
                const bands = ctx.getImageData(0, 0, 5, 1).data;
                let ordinal = 0;
                for (let bit = 0; bit < 5; ++bit) if (bands[bit * 4] > 128) ordinal |= 1 << bit;
                if (predicate(rgb, video.videoWidth, meta.mediaTime)) {
                    clearTimeout(timer);
                    resolve({ ordinal, rgb, width: video.videoWidth, elapsedMs: Math.round(performance.now() - start) });
                } else id = video.requestVideoFrameCallback(observe);
            }
            id = video.requestVideoFrameCallback(observe);
        });
        action();
        return observed;
    }
    // The first frame decodes as soon as the player is ready; playback carries it to the following blocks.
    const play = () => void video.play().catch(() => {});
    if (selection.recorded) {
        const sample = await frame(performance.now(), play, (_, w) => w === 1280);
        return { rtspFrameMs: sample.elapsedMs, rtspWidth: sample.width };
    }
    const first = await frame(performance.now(), () => {}, ([r, g, b], w) => w === 320 && r > 180 && g < 80 && b < 80);
    const formatChange = await frame(performance.now(), play, ([r, g, b], w) => w === 640 && b > 180 && r < 80 && g < 80);
    const slider = await wait(() => document.querySelector(".bt-timeline input[type=range]"), "archive timeline");
    const seek = (utc) => {
        // React observes the value through the native setter and the input event.
        Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, "value").set.call(slider, String(Date.parse(utc)));
        slider.dispatchEvent(new Event("input", { bubbles: true }));
    };
    const positioned = await frame(
        performance.now(),
        () => seek(selection.target),
        ([r, g, b], w) => w === 320 && g > 180 && r < 80 && b < 80,
    );
    const errorMs = Math.round(Math.abs(positioned.ordinal / 25 - 0.64) * 1000);
    if (errorMs > 100) throw new Error("Seek error " + errorMs + " ms");
    // Both positions are selected in one turn while the first seek is still in flight.
    seek(selection.target);
    seek(selection.gap);
    await wait(() => video.paused && player().querySelector(".ws-overlay"), "stop at the gap");
    if (slider.valueAsNumber !== Date.parse(selection.gap))
        throw new Error("The player did not stop at the last requested position");
    return {
        firstFrameMs: first.elapsedMs,
        seekErrorMs: errorMs,
        formatWidth: formatChange.width,
        seekFrame: positioned.ordinal,
    };
})().then(
    (value) => done({ value }),
    (error) => done({ error: String(error) }),
);
