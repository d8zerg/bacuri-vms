const done = arguments[arguments.length - 1];
(async () => {
    const form = document.getElementById("search"),
        video = document.querySelector("video");
    const deadline = performance.now() + 15000;
    while (!form.elements.point.value && performance.now() < deadline)
        await new Promise((resolve) => setTimeout(resolve, 25));
    const begin = performance.now();
    form.requestSubmit();
    return await new Promise((resolve, reject) => {
        const timer = setTimeout(
            () => reject(new Error(document.getElementById("status").textContent)),
            15000,
        );
        video.requestVideoFrameCallback((_, frame) => {
            clearTimeout(timer);
            if (frame.width !== 640 || frame.height !== 360)
                reject(new Error("Unexpected profile " + frame.width + "x" + frame.height));
            else
                resolve({
                    firstFrameMs: Math.round(performance.now() - begin),
                    width: frame.width,
                    height: frame.height,
                });
        });
    });
})().then(done, (error) => done({ error: String(error) }));
