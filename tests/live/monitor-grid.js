const [action, count, width, done] = arguments;
(async () => {
    const start = performance.now();
    const grid = () => document.querySelector(".mw-workspace");
    const tiles = () => [...(grid()?.querySelectorAll(".mw-tile:not([hidden])") ?? [])];
    if (action === "restore") {
        document.dispatchEvent(new KeyboardEvent("keydown", { key: "Escape", bubbles: true }));
    } else if (action === "main" || action === "secondary") {
        const select = tiles()[0].querySelector('select[aria-label="Качество эфира"]');
        select.value = action;
        select.dispatchEvent(new Event("change", { bubbles: true }));
    } else if (action === "expand") {
        tiles()[0].querySelector('button[aria-label="Развернуть камеру"]').click();
    }
    while (performance.now() - start < 26000) {
        const videos = tiles().map((el) => el.querySelector("video"));
        if (
            videos.length === count &&
            videos.every(
                (v) =>
                    v.readyState >= 2 &&
                    v.style.visibility === "visible" &&
                    (!width || v.videoWidth === width),
            )
        ) {
            const frames = await Promise.all(
                videos.map(
                    (video) =>
                        new Promise((resolve, reject) => {
                            const timeout = setTimeout(() => reject(Error("No presented frame")), 2500);
                            video.requestVideoFrameCallback((_, frame) => {
                                clearTimeout(timeout);
                                resolve({
                                    width: frame.width,
                                    height: frame.height,
                                    presented: frame.presentedFrames,
                                    dropped: video.getVideoPlaybackQuality().droppedVideoFrames,
                                });
                            });
                        }),
                ),
            );
            if (!width || frames.every((frame) => frame.width === width)) {
                return { elapsedMs: Math.round(performance.now() - start), frames };
            }
        }
        await new Promise((resolve) => setTimeout(resolve, 75));
    }
    throw Error("Grid not ready: " + (grid()?.innerText ?? document.body.innerText).slice(0, 1200));
})().then(done, (error) => done({ error: String(error) }));
