// Starts live video of the point open in the monitor of the workspace and waits for a decoded frame of the
// secondary profile. The player is reused, so the script also reconnects after a lost connection.
const done = arguments[arguments.length - 1];
(async () => {
    const player = () => document.querySelector(".ws-player");
    const status = () => player()?.querySelector('[role="status"]')?.textContent ?? "no player";
    const wait = async (predicate, what) => {
        const end = performance.now() + 15000;
        let value;
        while (!(value = predicate())) {
            if (performance.now() > end) throw new Error(what + ": " + status());
            await new Promise((resolve) => setTimeout(resolve, 25));
        }
        return value;
    };
    // The live and archive switch is in the monitor menu, which the page header can hold, the retry action is in
    // the player.
    const live = () => document.querySelector('.ws-submenu button[aria-label="Эфир"]:not(:disabled)');
    await wait(live, "Live mode button");
    const begin = performance.now();
    live().click();
    const quality = await wait(() => player().querySelector(".ws-stream-choice select"), "Quality choice");
    if (quality.value !== "secondary") {
        // React observes the value through the native setter and the change event.
        Object.getOwnPropertyDescriptor(HTMLSelectElement.prototype, "value").set.call(quality, "secondary");
        quality.dispatchEvent(new Event("change", { bubbles: true }));
    }
    // A frame counts only when it arrives during the wait. After a lost connection the player shows its error a
    // moment later; the script then retries the connection, as an operator does.
    const end = performance.now() + 15000;
    let frame;
    while (!frame) {
        if (performance.now() > end) throw new Error("Frames stopped: " + status());
        [...player().querySelectorAll("button")].find((b) => b.textContent.trim() === "Повторить")?.click();
        const video = player().querySelector("video");
        if (video && video.readyState >= 2 && video.videoWidth === 640 && video.videoHeight === 360)
            frame = await new Promise((resolve) => {
                const timer = setTimeout(() => resolve(null), 1000);
                video.requestVideoFrameCallback((_, meta) => {
                    clearTimeout(timer);
                    resolve(meta);
                });
            });
        else await new Promise((resolve) => setTimeout(resolve, 50));
    }
    return { firstFrameMs: Math.round(performance.now() - begin), width: frame.width, height: frame.height };
})().then(done, (error) => done({ error: String(error) }));
