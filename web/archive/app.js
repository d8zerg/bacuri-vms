import { ArchivePlayer, request } from "./player.js";
const byId = (id) => document.getElementById(id);
const status = (text) => {
    byId("status").textContent = text;
};
let csrf, player, session, cursor, selection, path, searchPath;
let working = false;
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
function action(fn) {
    return async (event) => {
        event?.preventDefault();
        if (working) return;
        working = true;
        document.querySelector("main").setAttribute("aria-busy", "true");
        try {
            await fn();
        } catch (error) {
            status(error.message);
        } finally {
            working = false;
            document.querySelector("main").removeAttribute("aria-busy");
        }
    };
}
const json = async (url, options) => (await request(url, options)).json();
function post(body, extra = {}) {
    return {
        method: "POST",
        headers: { "Content-Type": "application/json", "X-CSRF-Token": csrf || "", ...extra },
        body: JSON.stringify(body),
    };
}
async function points() {
    const data = await json("/sites/" + byId("site").value + "/points");
    byId("point").replaceChildren(...data.map((p) => new Option(p.name, p.id)));
}
async function signedIn(identity) {
    csrf = identity.csrf_token;
    byId("login").hidden = true;
    byId("search").hidden = false;
    byId("site").replaceChildren(...identity.site_roles.map((s) => new Option(s.site_id, s.site_id)));
    if (identity.site_roles.length) await points();
    const end = Date.now();
    byId("search").elements.start.value = new Date(end - 60000).toISOString();
    byId("search").elements.end.value = new Date(end).toISOString();
    status("");
}
byId("login").onsubmit = action(async () => {
    await signedIn(await json("/session", post(Object.fromEntries(new FormData(byId("login"))))));
    byId("login").reset();
});
byId("site").onchange = action(points);
async function play(position) {
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
        await byId("video").play();
        status("Воспроизведение");
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
byId("seek").onsubmit = action(() => play(byId("seek").elements.position.value));
byId("more").onclick = action(page);
try {
    await signedIn(await json("/session"));
} catch (error) {
    if (error.message === "unauthenticated") byId("login").hidden = false;
    else status(error.message);
}
