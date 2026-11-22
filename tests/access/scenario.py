"""Exercise the public administration and sign-in routes on two independent browser sessions."""

from contextlib import ExitStack
from pathlib import Path
from urllib.error import URLError
from urllib.parse import urlencode
from urllib.request import Request, urlopen
import hmac
import json
import struct
import os
import ssl
import subprocess
import sys
import time
import uuid

sys.path[:0] = [str(Path("tests") / name) for name in ("control", "archive", "live")]
import registry as c
import playback
import streaming
from browser import browser_session
from relay import VideoLink

BASE = f"sites/{c.SITE}/"
ISSUER = "https://keycloak:8443/realms/bacuri"
IDP_PASSWORD = os.environ["BACURI_TEST_IDP_PASSWORD"]
CA = ssl.create_default_context(cafile=os.environ["BACURI_OIDC_CA_FILE"])


def check(status, response):
    actual, _, data = response
    assert actual == status, (actual, status, data)
    return data


def wait(predicate, limit, name):
    deadline = time.monotonic() + limit
    while time.monotonic() < deadline:
        if predicate():
            return
        time.sleep(0.2)
    raise AssertionError(name)


def ready():
    try:
        with urlopen(ISSUER + "/.well-known/openid-configuration", context=CA, timeout=2) as reply:
            return json.load(reply)["issuer"] == ISSUER
    except (OSError, URLError):
        return False


def enabled(value):
    with urlopen(
        Request(
            "https://keycloak:8443/realms/master/protocol/openid-connect/token",
            data=urlencode(
                {
                    "grant_type": "password",
                    "client_id": "admin-cli",
                    "username": "qualification",
                    "password": IDP_PASSWORD,
                }
            ).encode(),
        ),
        context=CA,
        timeout=5,
    ) as reply:
        token = json.load(reply)["access_token"]
    url = "https://keycloak:8443/admin/realms/bacuri/users/" + str(uuid.UUID(int=1))
    with urlopen(
        Request(
            url,
            data=json.dumps({"enabled": value}).encode(),
            method="PUT",
            headers={"Authorization": "Bearer " + token, "Content-Type": "application/json"},
        ),
        context=CA,
        timeout=5,
    ) as reply:
        assert reply.status == 204


def corporate(name, accepted=True, otp=False):
    with browser_session("chrome", None, "/monitor") as (call, _):

        def js(script, args=None):
            return call("POST", "/execute/sync", {"script": script, "args": args or []})

        wait(
            lambda: js("const a=document.querySelector('a[href*=oidc]'); if(a){a.click();return true}"),
            10,
            "sign-in link",
        )
        wait(lambda: js("return !!document.querySelector('#kc-login')"), 10, "Keycloak sign-in form")
        js(
            "document.querySelector('#username').value=arguments[0]; document.querySelector('#password').value=arguments[1]; document.querySelector('#kc-login').click()",
            [name, IDP_PASSWORD],
        )
        if otp:
            wait(lambda: js("return !!document.querySelector('#otp')"), 10, "Keycloak second factor")
            digest = hmac.digest(
                Path("/stand/test-otp").read_bytes(), struct.pack(">Q", int(time.time()) // 30), "sha1"
            )
            offset = digest[-1] & 15
            code = (int.from_bytes(digest[offset : offset + 4], "big") & 0x7FFFFFFF) % 1000000
            js(
                "document.querySelector('#otp').value=arguments[0];document.querySelector('#kc-login').click()",
                [f"{code:06d}"],
            )
        if not accepted:
            wait(
                lambda: js(
                    "return location.pathname.endsWith('/callback') && document.body.innerText.includes('invalid_identity')"
                ),
                10,
                "unbound identity or absent MFA",
            )
            return None
        wait(
            lambda: js("return location.pathname==='/monitor' && !!document.querySelector('.ws-brand')"),
            15,
            "corporate Bacuri session",
        )
        identity = call(
            "POST",
            "/execute/async",
            {
                "script": "const done=arguments[0];fetch('/api/v1/session').then(r=>r.json()).then(done)",
                "args": [],
            },
        )
        cookie = call("GET", "/cookie/__Host-bacuri_session")
        assert cookie["httpOnly"] and cookie["secure"]
        return {"Cookie": cookie["name"] + "=" + cookie["value"], "X-CSRF-Token": identity["csrf_token"]}


def run():
    wait(ready, 100, "Keycloak readiness")
    with ExitStack() as services:
        for command in ([c.CORE, "serve"], [playback.MEDIA]):
            services.callback(c.stop, subprocess.Popen(command))
        services.callback(
            c.stop,
            subprocess.Popen(
                [c.RECORDER], env={**os.environ, "BACURI_KEY_FILE": str(c.ROOT / "recorder.key")}
            ),
        )
        subprocess.run(
            [c.CORE, "create-user", "admin", "administrator"], input=c.PASSWORD + "\n", text=True, check=True
        )
        c.wait_for(lambda: c.http("GET", "session")[0] == 401, "Core")
        admin = c.login("admin")
        assert check(200, c.http("GET", "session/oidc/methods")) == {"local": True, "oidc": False}

        def read(path):
            return check(200, c.http("GET", BASE + path, headers=admin))["data"]

        def mutate(method, path, body=None, expected=200):
            revision = check(200, c.http("GET", BASE + "users", headers=admin))["revision"]
            return check(
                expected, c.http(method, BASE + path, body, {**admin, "If-Match": '"' + revision + '"'})
            )

        operator = mutate(
            "POST", "users", {"username": "operator", "role": "operator", "password": c.PASSWORD}
        )["data"]["id"]
        local = c.login("operator")
        check(403, c.http("GET", BASE + "users", headers=local))
        alice = next(u["id"] for u in read("users") if u["username"] == "admin")
        mutate("PATCH", "users/" + alice, {"role": "operator", "enabled": True}, 409)
        subprocess.run(
            [c.CORE, "configure-oidc", ISSUER, "bacuri-site", "2"],
            input=IDP_PASSWORD + "\n",
            text=True,
            check=True,
        )
        subprocess.run([c.CORE, "bind-identity", operator, ISSUER, str(uuid.UUID(int=1))], check=True)
        mutate(
            "PUT",
            "identity/provider",
            {
                "issuer": ISSUER,
                "client_id": "bacuri-site",
                "client_secret": IDP_PASSWORD,
                "admin_acr": "2",
                "enabled": True,
            },
        )
        assert IDP_PASSWORD not in str(read("identity/provider"))
        for user, i in [(operator, 1), (alice, 3)]:
            mutate(
                "PUT", f"users/{user}/external-identity", {"issuer": ISSUER, "subject": str(uuid.UUID(int=i))}
            )
        mutate(
            "PUT",
            f"users/{alice}/external-identity",
            {"issuer": ISSUER, "subject": str(uuid.UUID(int=1))},
            409,
        )
        print("Keycloak sign-in, same-email subject isolation and administrative MFA", flush=True)
        corporate("unbound", False)
        corporate("administrator", False)
        mutate(
            "PUT", f"users/{alice}/external-identity", {"issuer": ISSUER, "subject": str(uuid.UUID(int=4))}
        )
        corporate_admin = corporate("mfa-admin", otp=True)
        check(200, c.http("GET", BASE + "users", headers=corporate_admin))
        check(204, c.http("DELETE", "session", headers=corporate_admin))
        remote = corporate("operator")
        issued = time.monotonic()
        with browser_session("chrome", admin, "/access") as (call, _):
            wait(
                lambda: call(
                    "POST",
                    "/execute/sync",
                    {
                        "script": "return Array.from(document.querySelectorAll('h2')).some(e=>e.textContent==='operator')",
                        "args": [],
                    },
                ),
                12,
                "web administration",
            )

            call(
                "POST",
                "/execute/sync",
                {
                    "script": "Array.from(document.querySelectorAll('nav button')).find(e=>e.textContent==='Группы').click()",
                    "args": [],
                },
            )
            call(
                "POST",
                "/execute/sync",
                {
                    "script": "const field=document.querySelector('form input[name=name]');field.value='Browser group';field.form.requestSubmit()",
                    "args": [],
                },
            )
            wait(
                lambda: any(group["name"] == "Browser group" for group in read("resource-groups")),
                10,
                "create group from web form",
            )

        def camera(name):
            return check(
                201,
                c.http(
                    "POST",
                    BASE + "cameras",
                    {
                        "name": name,
                        "main_url": "rtsp://rtsp-camera:8554/main",
                        "secondary_url": "rtsp://rtsp-camera:8554/sub",
                    },
                    {**admin, "Idempotency-Key": str(uuid.uuid4())},
                ),
            )

        cam = camera("Gate")
        point = cam["point_id"]
        other = camera("Lobby")["point_id"]
        group = mutate("POST", "resource-groups", {"name": "Gate", "points": [point]})["data"]["id"]
        check(
            412,
            c.http(
                "PUT",
                BASE + "resource-groups/" + group,
                {"name": "stale", "points": [other]},
                {**admin, "If-Match": '"0"'},
            ),
        )
        root = BASE + "points/" + point
        gview, gexport = (str(uuid.uuid4()) for _ in range(2))

        def grant(id, action):
            return mutate("PUT", "grants/" + id, {"group_id": group, "user_id": operator, "action": action})

        grant(gview, "view")
        grant(gexport, "export")
        gptz = str(uuid.uuid4())
        grant(gptz, "ptz")
        for action in ["audio", "protect", "configure"]:
            grant(str(uuid.uuid4()), action)
        c.wait_for(lambda: bool(streaming.archive_blocks(point)), "new recording")
        playback.fixture(cam, 5, 320, [(0, 0, 255), (0, 255, 0)], 16 * 1024 * 1024)
        interval = playback.interval(5, 6)
        for session in (local, remote):
            bounded = {"point_id": point, "user_id": operator, "action": "view", "interval": interval}
            mutate("PUT", "grants/" + gview, bounded)
            check(200, c.http("GET", root + "/archive?" + urlencode(interval), headers=session))
            check(
                404, c.http("GET", root + "/archive?" + urlencode(playback.interval(4, 6)), headers=session)
            )
            check(
                404,
                c.http(
                    "POST",
                    root + "/media-sessions",
                    {"mode": "live"},
                    {**session, "Idempotency-Key": str(uuid.uuid4())},
                ),
            )
            grant(gview, "view")
            ptz = {"device_id": cam["id"], "command": "stop"}
            mutate("DELETE", "grants/" + gptz)
            check(403, c.http("POST", root + "/ptz", ptz, {**session, "Idempotency-Key": str(uuid.uuid4())}))
            grant(gptz, "ptz")
            check(422, c.http("POST", root + "/ptz", ptz, {**session, "Idempotency-Key": str(uuid.uuid4())}))
            print("Checking point scopes and active media revocation", flush=True)
            assert [p["id"] for p in check(200, c.http("GET", BASE + "points", headers=session))] == [point]
            assert [p["id"] for p in check(200, c.http("GET", BASE + "cameras", headers=session))] == [
                cam["id"]
            ]
            check(403, c.http("GET", BASE + "users", headers=session))
            check(200, c.http("GET", root + "/archive?" + urlencode(interval), headers=session))
            ticket = check(
                201,
                c.http(
                    "POST",
                    root + "/media-sessions",
                    {"mode": "archive", "interval": interval},
                    {**session, "Idempotency-Key": str(uuid.uuid4())},
                ),
            )
            media = "media-sessions/" + ticket["id"]
            part = check(200, c.http("GET", media + "/manifest", headers=session))["parts"][0]["media_id"]
            playback.slow_reader(
                media + "/parts/" + part, session, lambda: mutate("DELETE", "grants/" + gview)
            )
            check(404, c.http("GET", root + "/archive?" + urlencode(interval), headers=session))
            grant(gview, "view")
            playback.slow_reader(
                root + "/exports",
                session,
                lambda: mutate("DELETE", "grants/" + gexport),
                {"interval": interval},
            )
            grant(gexport, "export")
            with VideoLink("chrome") as link, browser_session("chrome", session, "/live/") as (call, _):
                streaming.linked(call, link)
                streaming.verify_cutoff(
                    link,
                    lambda: mutate("PUT", "resource-groups/" + group, {"name": "Gate", "points": []}),
                    "group removal",
                )
            mutate("PUT", "resource-groups/" + group, {"name": "Gate", "points": [point]})
            mutate(
                "PUT",
                "privacy-policies/" + point,
                dict(
                    live=True,
                    archive=True,
                    thumbnail=True,
                    export=True,
                    regions=[{"left": 0.1, "top": 0.2, "right": 0.5, "bottom": 0.8}],
                ),
            )
            check(404, c.http("GET", root + "/archive?" + urlencode(interval), headers=session))
            check(403, c.http("POST", root + "/exports", {"interval": interval}, session))
            check(
                404,
                c.http(
                    "POST",
                    root + "/media-sessions",
                    {"mode": "live"},
                    {**session, "Idempotency-Key": str(uuid.uuid4())},
                ),
            )
            mutate(
                "PUT",
                "privacy-policies/" + point,
                dict(live=False, archive=False, thumbnail=False, export=False),
            )
        print("Checking renewal before blocking the corporate subject in Keycloak", flush=True)
        time.sleep(max(0, issued + 56 - time.monotonic()))
        check(200, c.http("GET", "session", headers=remote))
        with VideoLink("chrome") as link, browser_session("chrome", remote, "/live/") as (call, _):
            streaming.linked(call, link)
            started = time.monotonic()
            enabled(False)
            wait(lambda: c.http("GET", "session", headers=remote)[0] == 401, 59, "Keycloak block deadline")
            elapsed = time.monotonic() - started
            assert elapsed <= 60
            time.sleep(5.3)
            assert link.last_packet - started <= 60 and time.monotonic() - link.last_packet >= 1
            print(
                "Keycloak block in %.2f s; last SRTP packet after %.2f s"
                % (elapsed, max(0, link.last_packet - started)),
                flush=True,
            )
        check(200, c.http("GET", "session", headers=local))
        enabled(True)
        remote = corporate("operator")
        Path("/stand/outage").touch()
        wait(lambda: Path("/stand/offline").exists(), 30, "provider stopped")
        assert not ready()
        check(503, c.http("GET", "session/oidc/start"))
        c.login("admin")
        wait(
            lambda: c.http("GET", "session", headers=remote)[0] == 401,
            60,
            "offline corporate session expired",
        )
        Path("/stand/restore").touch()
        wait(ready, 100, "provider recovery")
        remote = corporate("operator")
        check(204, c.http("DELETE", "session", headers=remote))
        check(401, c.http("GET", "session", headers=remote))
        audit = check(200, c.http("POST", BASE + "audit/search", {"actor": operator}, admin))["data"]
        assert {"local", "oidc"} <= {a["method"] for a in audit if a["action"] == "session.login"}
        assert any(a["method"] == "oidc" and a["action"] == "session.logout" for a in audit)
        changes = check(200, c.http("POST", BASE + "audit/search", {"actor": alice}, admin))["data"]
        assert any(a["action"] == "scoped_grants.delete" and gview in a["resource"] for a in changes)
        backup = subprocess.check_output([c.CORE, "backup-config"])
        c.login("operator")
        audit_size = int(c.query("SELECT count(*) FROM bacuri_access.access_audit"))
        subprocess.run([c.CORE, "restore-config"], input=backup, check=True)
        check(401, c.http("GET", "session", headers=local))
        assert int(c.query("SELECT count(*) FROM bacuri_access.access_audit")) >= audit_size
        assert check(200, c.http("GET", "session/oidc/methods"))["oidc"]
        restored = check(200, c.http("GET", BASE + "users", headers=c.login("admin")))["data"]
        assert any(u["id"] == operator and u["subject"] == str(uuid.UUID(int=1)) for u in restored)
        print("Local and corporate access matrix, revocation, audit and provider recovery passed", flush=True)


if __name__ == "__main__":
    run()
