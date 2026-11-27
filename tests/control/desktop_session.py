"""Desktop sign-in: a signed-in browser issues a one-time code, the desktop client exchanges it with PKCE."""

import base64
import hashlib
import secrets

REDIRECT = "http://127.0.0.1:49152/callback"


def pkce():
    verifier = secrets.token_urlsafe(48)
    challenge = base64.urlsafe_b64encode(hashlib.sha256(verifier.encode()).digest()).rstrip(b"=").decode()
    return verifier, challenge


def code(http, browser, challenge, redirect=REDIRECT):
    status, _, body = http("POST", "session/desktop/codes", {
        "code_challenge": challenge, "code_challenge_method": "S256", "redirect_uri": redirect}, headers=browser)
    assert status == 201, (status, body)
    assert len(body["code"]) == 64 and body["expires_at"].endswith("Z")
    return body["code"]


def exchange(http, value, verifier, redirect=REDIRECT, headers=None):
    return http("POST", "session/desktop", {"code": value, "code_verifier": verifier, "redirect_uri": redirect},
                headers=headers, origin=False)


def refused(response, status, error):
    assert (response[0], (response[2] or {}).get("code")) == (status, error), response


def check(http, query, login, user):
    browser = login(user)
    verifier, challenge = pkce()

    # Only a signed-in browser with the CSRF token issues a code for a loopback address and S256.
    request = {"code_challenge": challenge, "code_challenge_method": "S256", "redirect_uri": REDIRECT}
    refused(http("POST", "session/desktop/codes", request, headers={"Cookie": browser["Cookie"]}), 403, "forbidden")
    refused(http("POST", "session/desktop/codes", request), 401, "unauthenticated")
    for change in ({"redirect_uri": "http://localhost:49152/callback"}, {"redirect_uri": "https://site.test/"},
                   {"code_challenge_method": "plain"}, {"code_challenge": "short"}):
        refused(http("POST", "session/desktop/codes", {**request, **change}, headers=browser), 400, "invalid_request")

    # A wrong verifier consumes the code: the right verifier cannot use it afterwards.
    first = code(http, browser, challenge)
    refused(exchange(http, first, pkce()[0]), 400, "desktop_code_invalid")
    refused(exchange(http, first, verifier), 400, "desktop_code_invalid")
    # Another loopback address, credentials and another origin are refused.
    refused(exchange(http, code(http, browser, challenge), verifier, "http://127.0.0.1:49153/callback"), 400,
            "desktop_code_invalid")
    pending = code(http, browser, challenge)
    refused(exchange(http, pending, verifier, headers={"Cookie": browser["Cookie"]}), 403, "forbidden")
    refused(exchange(http, pending, verifier, headers={"Origin": "https://other.test"}), 403, "forbidden")
    # An expired code is refused.
    expired = code(http, browser, challenge)
    query("UPDATE bacuri_access.desktop_codes SET expires_at=now()-interval '1 second'")
    refused(exchange(http, expired, verifier), 400, "desktop_code_invalid")

    status, headers, session = exchange(http, code(http, browser, challenge), verifier)
    assert status == 201, (status, session)
    assert headers.get("Cache-Control") == "no-store" and "Set-Cookie" not in headers
    assert session["token"].startswith("d1.") and "csrf_token" not in session
    assert [role["role"] for role in session["site_roles"]] == ["operator"]
    desktop = {"Authorization": "Bearer " + session["token"]}
    token = session["token"][3:]

    # The desktop session works without an origin or a CSRF token and only through the header.
    status, _, current = http("GET", "session", headers=desktop, origin=False)
    assert status == 200 and current["user_id"] == session["user_id"], current
    assert http("GET", f"sites/{session['site_roles'][0]['site_id']}/points", headers=desktop, origin=False)[0] == 200
    refused(http("GET", "session", headers={**desktop, "Cookie": browser["Cookie"]}, origin=False), 401,
            "unauthenticated")
    refused(http("GET", "session", headers={"Cookie": "__Host-bacuri_session=" + token}), 401, "unauthenticated")
    browser_token = browser["Cookie"].split("=", 1)[1]
    refused(http("GET", "session", headers={"Authorization": "Bearer d1." + browser_token}, origin=False), 401,
            "unauthenticated")
    refused(http("POST", "session/desktop/codes", request, headers=desktop, origin=False), 403, "forbidden")
    assert query("SELECT count(*) FROM bacuri_access.access_audit WHERE method='desktop' AND "
                 f"action='session.login' AND actor='{session['user_id']}'") != "0"

    # A code dies with the browser session that issued it.
    other = login(user)
    orphan = code(http, other, challenge)
    assert http("DELETE", "session", headers=other)[0] == 204
    refused(exchange(http, orphan, verifier), 400, "desktop_code_invalid")

    # Signing out ends the desktop session.
    assert http("DELETE", "session", headers=desktop, origin=False)[0] == 204
    refused(http("GET", "session", headers=desktop, origin=False), 401, "unauthenticated")
    assert http("GET", "session", headers=browser)[0] == 200
    print("Desktop sign-in: code issue, PKCE exchange, refusals, bearer session and sign-out checked")
