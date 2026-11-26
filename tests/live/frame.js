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
    // The live and archive switch is in the monitor menu, the retry action in the player.
    const button = (root, text) => [...(root?.querySelectorAll("button") ?? [])].find((b) => b.textContent.trim() === text);
    await wait(() => button(document.querySelector("main"), "Эфир"), "Live mode button");
    const begin = performance.now();
    button(document.querySelector("main"), "Эфир").click();
    button(player(), "Повторить")?.click();
    const quality = await wait(() => player().querySelector(".ws-stream-choice select"), "Quality choice");
    if (quality.value !== "secondary") {
        // React observes the value through the native setter and the change event.
        Object.getOwnPropertyDescriptor(HTMLSelectElement.prototype, "value").set.call(quality, "secondary");
        quality.dispatchEvent(new Event("change", { bubbles: true }));
    }
    const video = await wait(() => {
        const element = player().querySelector("video");
        return element && element.readyState >= 2 && element.videoWidth === 640 && element.videoHeight === 360
            ? element
            : null;
    }, "Secondary live frame");
    const frame = await new Promise((resolve, reject) => {
        const timer = setTimeout(() => reject(new Error("Frames stopped: " + status())), 5000);
        video.requestVideoFrameCallback((_, meta) => {
            clearTimeout(timer);
            resolve(meta);
        });
    });
    return { firstFrameMs: Math.round(performance.now() - begin), width: frame.width, height: frame.height };
})().then(done, (error) => done({ error: String(error) }));
