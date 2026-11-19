const [selection, done] = arguments;
(async () => {
    const wait = async (predicate) => {
        const end = performance.now() + 12000;
        while (!predicate()) {
            if (performance.now() > end)
                throw new Error("Timed out: " + document.querySelector("#status").textContent);
            await new Promise((resolve) => setTimeout(resolve, 25));
        }
    };
    const form = document.querySelector("#search");
    await wait(() => form.elements.point.options.length);
    form.elements.start.value = selection.start;
    form.elements.end.value = selection.end;
    form.requestSubmit();
    await wait(() => {
        const rows = document.querySelector("#coverage");
        return selection.recorded
            ? rows.textContent.includes(selection.start.slice(0, 19)) && rows.querySelector("button")
            : rows.querySelectorAll("button").length === 3;
    });
    const labels = document.querySelector("#coverage").textContent;
    if (!selection.recorded && !labels.includes("Нет связи с камерой"))
        throw new Error("Gap missing from timeline");
    const video = document.querySelector("video");
    async function frame(start, action, predicate) {
        const observed = new Promise((resolve, reject) => {
            let id;
            const timer = setTimeout(() => {
                video.cancelVideoFrameCallback(id);
                reject(
                    new Error(
                        "No reference frame: " +
                            document.querySelector("#status").textContent +
                            " ready=" +
                            video.readyState +
                            " error=" +
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
                    resolve({
                        ordinal,
                        rgb,
                        width: video.videoWidth,
                        elapsedMs: Math.round(performance.now() - start),
                    });
                } else id = video.requestVideoFrameCallback(observe);
            }
            id = video.requestVideoFrameCallback(observe);
        });
        action();
        return observed;
    }
    if (selection.recorded) {
        const sample = await frame(
            performance.now(),
            () => document.querySelector("#coverage button").click(),
            (_, w) => w === 1280,
        );
        return { rtspFrameMs: sample.elapsedMs, rtspWidth: sample.width };
    }
    const first = await frame(
        performance.now(),
        () => document.querySelector("#coverage button").click(),
        ([r, g, b], w) => w === 320 && r > 180 && g < 80 && b < 80,
    );
    const formatChange = await frame(
        performance.now(),
        () => {},
        ([r, g, b], w) => w === 640 && b > 180 && r < 80 && g < 80,
    );
    const seek = document.querySelector("#seek");
    const positioned = await frame(
        performance.now(),
        () => {
            seek.elements.position.value = selection.target;
            seek.requestSubmit();
        },
        ([r, g, b], w) => w === 320 && g > 180 && r < 80 && b < 80,
    );
    const errorMs = Math.round(Math.abs(positioned.ordinal / 25 - 0.64) * 1000);
    if (errorMs > 100) throw new Error("Seek error " + errorMs + " ms");
    seek.elements.position.value = selection.gap;
    seek.requestSubmit();
    await wait(() => document.querySelector("#status").textContent.includes("недоступна"));
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
