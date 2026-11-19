// Route the negotiated UDP pair through the test link while preserving ICE and DTLS credentials.
const [host, port] = arguments;
const original = window.fetch.bind(window);
function route(sdp) {
    const lines = sdp.split("\r\n");
    const candidate = lines.findIndex((line) => line.startsWith("a=candidate:"));
    if (candidate < 0) throw new Error("Missing ICE candidate");
    lines.splice(candidate, 0, "a=candidate:1 1 UDP 2130706431 " + host + " " + port + " typ host");
    return lines.filter((line, i) => i === candidate || !line.startsWith("a=candidate:")).join("\r\n");
}
window.fetch = async (url, init) => {
    if (!String(url).endsWith("/offer")) return original(url, init);
    window.liveSession = String(url).split("/").at(-2);
    const response = await original(url, { ...init, body: route(init.body) });
    return response.ok
        ? new Response(route(await response.text()), { headers: response.headers })
        : response;
};
