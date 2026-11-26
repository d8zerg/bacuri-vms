// Completes HTTPS signaling, then leaves the receiving peer without a remote description.
const done = arguments[arguments.length - 1];
const fetchOriginal = window.fetch.bind(window);
window.fetch = async (url, init) => {
    const response = await fetchOriginal(url, init);
    if (!String(url).endsWith("/offer")) return response;
    done(response.ok ? String(url).split("/").at(-2) : null);
    return new Promise((_, reject) => {
        init.signal.addEventListener("abort", () => reject(init.signal.reason), { once: true });
    });
};
(async () => {
    const until = performance.now() + 10000;
    const live = () =>
        document.querySelector('.ws-submenu button[aria-label="Эфир"]:not(:disabled)');
    while (!live() && performance.now() < until) await new Promise((resolve) => setTimeout(resolve, 25));
    live().click();
})();
