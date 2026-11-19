const api = "/api/v1";
export async function request(path, options = {}) {
    const response = await fetch(api + path, { cache: "no-store", ...options });
    if (!response.ok) {
        const problem = await response.json();
        throw new Error(problem.code || String(response.status));
    }
    return response;
}
export const byId = (id) => document.getElementById(id);
export const status = (text) => {
    byId("status").textContent = text;
};
export let csrf;
let working = false;
export function action(fn) {
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
export const json = async (url, options) => (await request(url, options)).json();
export function post(body, extra = {}) {
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

    status("");
}
byId("login").onsubmit = action(async () => {
    await signedIn(await json("/session", post(Object.fromEntries(new FormData(byId("login"))))));
    byId("login").reset();
});
byId("site").onchange = action(points);

export async function initialize() {
    try {
        await signedIn(await json("/session"));
    } catch (error) {
        if (error.message === "unauthenticated") byId("login").hidden = false;
        else status(error.message);
    }
}
