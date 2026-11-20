import { ArchivePlayer } from "./player.js";
import { byId, status, csrf, action, json, post, request, initialize, points } from "/session.js";

let player, session, cursor, selection, path, searchPath;
let playback = null;
const reasons = {
    source_offline: "Нет связи с камерой",
    overload: "Перегрузка",
    storage_failure: "Ошибка хранения",
    unknown_coverage: "Покрытие неизвестно",
    pending_catalog: "Ожидает регистрации",
    media_offline: "Том недоступен",
    corrupt: "Запись повреждена",
    outside_retention: "Истёк срок хранения",
};

async function playAt(position, autoplay) {
    if (session && Date.parse(session.expires_at) <= Date.now()) {
        player?.stop();
        player = session = null;
    }
    if (!session) {
        session = await json(
            path + "/media-sessions",
            post({ mode: "archive", interval: selection }, { "Idempotency-Key": crypto.randomUUID() }),
        );
    }
    if (!player) {
        const manifest = await json("/media-sessions/" + session.id + "/manifest");
        player = new ArchivePlayer(byId("video"), session.id, manifest, status);
        byId("seek").hidden = false;
    }
    byId("seek").elements.position.value = position;
    if (await player.seek(position)) {
        if (autoplay) await byId("video").play();
        status(autoplay ? "Воспроизведение" : "Кадр тревоги");
    }
}

async function play(position, autoplay = true) {
    const request = { position, autoplay };
    if (playback) {
        playback.next = request;
        return;
    }
    playback = { next: request };
    try {
        while (playback.next) {
            const target = playback.next;
            playback.next = null;
            try {
                await playAt(target.position, target.autoplay);
            } catch (error) {
                if (!playback.next) throw error;
            }
        }
    } finally {
        playback = null;
    }
}
async function page() {
    const params = new URLSearchParams(selection);
    if (cursor) params.set("cursor", cursor);
    const data = await json(searchPath + "?" + params);
    for (const item of data.items) {
        const li = document.createElement("li");
        li.append(item.interval.start + " / " + item.interval.end + " ");
        if (item.state === "recorded") {
            const button = document.createElement("button");
            button.textContent = "Воспроизвести";
            button.onclick = action(() => play(item.interval.start));
            li.append(button);
        } else {
            li.append(reasons[item.reason || item.state] || item.state);
        }
        byId("coverage").append(li);
    }
    cursor = data.next_cursor;
    byId("more").hidden = !cursor;
}
byId("search").onsubmit = action(async () => {
    player?.stop();
    if (session)
        await request("/media-sessions/" + session.id, {
            method: "DELETE",
            headers: { "X-CSRF-Token": csrf },
        });
    player = session = null;
    byId("seek").hidden = true;
    cursor = null;
    byId("coverage").replaceChildren();
    path = "/sites/" + byId("site").value + "/points/" + byId("point").value;
    searchPath = path + "/archive";
    selection = Object.fromEntries(new FormData(byId("search")));
    await page();
    status("");
});
const seek = byId("seek");
const seekToInput = () => play(seek.elements.position.value);
const startSeek = action(seekToInput);
seek.onsubmit = (event) => {
    if (!playback) return startSeek(event);
    event.preventDefault();
    return seekToInput();
};
byId("more").onclick = action(page);

const end = Date.now();
byId("search").elements.start.value = new Date(end - 60000).toISOString();
byId("search").elements.end.value = new Date(end).toISOString();
const origin = new URLSearchParams(location.search);
async function openAlert() {
    if (!origin.has("alert") || origin.get("site") !== byId("site").value) return;
    const link = await json("/sites/" + byId("site").value + "/alerts/" + encodeURIComponent(origin.get("alert")) + "/archive");
    byId("point").value = link.point_id;
    byId("search").elements.start.value = link.archive_interval.start;
    byId("search").elements.end.value = link.archive_interval.end;
    byId("retry-alert").hidden = link.state === "ready";
    if (link.state !== "ready") {
        status(link.state === "uncertain_time" ? "Нельзя точно сопоставить время тревоги с записью" : reasons[link.reason || link.state]);
        return;
    }
    path = "/sites/" + byId("site").value + "/points/" + link.point_id;
    searchPath = path + "/archive";
    selection = link.interval;
    byId("search").elements.start.value = selection.start;
    byId("search").elements.end.value = selection.end;
    await page();
    await play(link.position, false);
}
byId("retry-alert").onclick = action(openAlert);
await initialize(async () => {
    await points();
    byId("retry-alert").hidden = true;
    await openAlert();
}, origin.get("site"));
