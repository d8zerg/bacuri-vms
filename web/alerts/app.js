import { byId, initialize, json, status } from "/session.js";

const list = byId("alerts");
let active;
const displayTime = new Intl.DateTimeFormat("ru", {
    dateStyle: "short",
    timeStyle: "long",
    timeZone: "UTC",
});
function upsert(site, alert, points) {
    const id = "alert-" + alert.id;
    const row = byId(id) || document.createElement("li");
    row.id = id;
    const link = document.createElement("a");
    link.href = "/archive/?" + new URLSearchParams({ site, alert: alert.id });
    link.textContent = "Открыть запись";
    const type = alert.type === "motion" ? "Движение" : alert.type;
    const time = displayTime.format(new Date(alert.observed_at)) + " UTC";
    const quality = alert.time_basis === "reception" ? "по времени приёма" : "по часам источника";
    row.replaceChildren(
        type +
            " · " +
            (points.get(alert.point_id) || alert.point_id) +
            " · " +
            time +
            " (" +
            quality +
            ") ",
        link,
    );
    list.prepend(row);
}

function stop() {
    const state = active;
    active = null;
    if (!state) return;
    clearTimeout(state.timer);
    state.socket?.close();
}
async function start() {
    stop();
    list.replaceChildren();
    const site = byId("site").value;
    if (!site) return;
    const state = { site, cursor: null, points: new Map() };
    active = state;
    async function connect() {
        if (active !== state) return;
        try {
            const path = "/sites/" + site + "/alerts";
            if (!state.cursor) {
                const [snapshot, points] = await Promise.all([
                    json(path),
                    json("/sites/" + site + "/points"),
                ]);
                if (active !== state) return;
                state.points = new Map(points.map((p) => [p.id, p.name]));
                list.replaceChildren();
                for (const alert of snapshot.items) upsert(site, alert, state.points);
                state.cursor = snapshot.cursor;
            }
            const url = new URL("/api/v1" + path + "/stream", location.href);
            url.protocol = "wss:";
            const socket = new WebSocket(url, "bacuri.vms.alerts.v1");
            state.socket = socket;
            let opened = false;
            socket.onopen = () => {
                if (active !== state) return;
                opened = true;
                socket.send(JSON.stringify({ type: "subscribe", after: state.cursor }));
                status(list.childElementCount ? "Лента подключена" : "Тревог пока нет");
            };
            socket.onmessage = ({ data }) => {
                if (active !== state) return;
                const frame = JSON.parse(data);
                if (frame.type === "resync_required") {
                    state.cursor = null;
                    list.replaceChildren();
                    status("Обновление списка тревог");
                    return;
                }
                if (frame.type === "upsert") upsert(site, frame.alert, state.points);
                else if (frame.type === "remove") byId("alert-" + frame.alert_id)?.remove();
                state.cursor = frame.cursor;
                status("Лента подключена");
            };
            socket.onclose = ({ code }) => {
                if (active !== state) return;
                if (!opened || code === 1008 || code === 1013 || code === 1011) {
                    state.cursor = null;
                    list.replaceChildren();
                }
                status("Восстановление связи");
                state.timer = setTimeout(connect, 500);
            };
        } catch (error) {
            if (active !== state) return;
            if (["unauthenticated", "not_found"].includes(error.message)) {
                list.replaceChildren();
                status("Доступ к ленте закрыт");
                if (error.message === "unauthenticated") byId("login").hidden = false;
                return;
            }
            status("Лента недоступна. Повторное подключение");
            state.timer = setTimeout(connect, 1000);
        }
    }
    await connect();
}
byId("search").onsubmit = (event) => event.preventDefault();
window.addEventListener("pagehide", stop);
window.addEventListener("pageshow", (event) => {
    if (event.persisted) void start();
});
await initialize(start);
