"""Verify concurrent ownership, durable receipts and delegated commands against the HTTPS service."""

from concurrent.futures import ThreadPoolExecutor
from threading import Barrier
from http.client import HTTPSConnection
import json
import subprocess
import time
import uuid


def check(c, first, other):
    site = f"sites/{c.SITE}"
    point = first["point_id"]
    for name in ("shift-a", "shift-b"):
        subprocess.run(
            [c.CORE, "create-user", name, "operator"], input=c.PASSWORD + "\n", text=True, check=True
        )
        subprocess.run([c.CORE, "grant-point", name, point], check=True)
    users = [c.login("shift-a"), c.login("shift-b")]
    admin = c.login("admin")
    target = first["id"]

    def http(method, resource, who, body=None, status=200):
        code, _, result = c.http(method, site + "/" + resource, body, who)
        assert code == status, (resource, code, result)
        return result

    def work(alert=target):
        return "alerts/" + alert + "/work"

    def command(who, action, version, reason, key=None, alert=target, status=200):
        return http(
            "POST",
            work(alert),
            {**who, "Idempotency-Key": key or str(uuid.uuid4())},
            {"action": action, "version": version, "reason": reason},
            status,
        )

    valid = {"action": "claim", "version": 0, "reason": "Review"}
    headers = {**users[0], "Idempotency-Key": str(uuid.uuid4())}
    http("POST", work(), users[0], valid, 400)
    http("POST", work(), {**headers, "X-CSRF-Token": "incorrect"}, valid, 403)
    for changes in (
        {"version": "0"},
        {"version": False},
        {"version": -1},
        {"reason": " "},
        {"owner": "shift-b"},
        {"action": "reopen"},
    ):
        http("POST", work(), headers, {**valid, **changes}, 400)
    assert http("GET", work(), users[0])["history"] == []
    barrier = Barrier(2)
    keys = [str(uuid.uuid4()), str(uuid.uuid4())]

    def compete(i):
        barrier.wait()
        return c.http(
            "POST",
            site + "/" + work(),
            {"action": "claim", "version": 0, "reason": "Shift"},
            {**users[i], "Idempotency-Key": keys[i]},
        )

    with ThreadPoolExecutor(2) as pool:
        replies = list(pool.map(compete, range(2)))
    assert sorted(r[0] for r in replies) == [200, 409], replies
    winner = next(i for i, r in enumerate(replies) if r[0] == 200)
    owner, peer = users[winner], users[1 - winner]
    assert command(owner, "claim", 0, "Shift", keys[winner]) == replies[winner][2]
    command(owner, "claim", 0, "Different", keys[winner], status=409)
    command(owner, "close", 1, "Checked", status=409)
    command(peer, "record_result", 1, "Other operator", status=403)
    http("GET", work(other["id"]), owner, status=404)
    command(peer, "claim", 0, "Hidden", alert=other["id"], status=404)
    assert not http("POST", "alert-audit/search", owner, {"alert_id": other["id"]})["items"]
    detail = http("GET", work(), peer)
    assert (
        detail["owner"] == ("shift-a", "shift-b")[winner]
        and len(detail["history"]) == 1
        and not detail["actions"]
    )

    def issue(who=owner, ttl=60):
        grant = http(
            "POST",
            "alert-delegations",
            who,
            {"point_id": point, "service": "classifier", "action": "record_result", "ttl_seconds": ttl},
            201,
        )
        return grant["id"], {"Authorization": "Bearer " + grant["token"]}

    grant, service = issue()
    key = str(uuid.uuid4())
    http("GET", work(), service, status=401)
    command({**service, **owner}, "record_result", 1, "Mixed credentials", status=401)
    # Drop the HTTPS connection after sending the command, before receiving any response.
    conn = HTTPSConnection("localhost", 8443, context=c.TLS, timeout=8)
    try:
        conn.request(
            "POST",
            "/api/v1/" + site + "/" + work(),
            json.dumps({"action": "record_result", "version": 1, "reason": "Verified gate footage"}),
            {"Content-Type": "application/json", **service, "Idempotency-Key": key},
        )
    finally:
        conn.close()
    c.wait_for(
        lambda: http("GET", work(), owner)["version"] == 2, "command applied after disconnected client"
    )
    receipt = command(service, "record_result", 1, "Verified gate footage", key)
    assert command(service, "record_result", 1, "Verified gate footage", key) == receipt
    command(service, "close", 2, "Checked", status=403)
    # An issuer with access to both points still delegates only one of them.
    _, scoped = issue(admin)
    command(scoped, "record_result", 0, "Wrong point", alert=other["id"], status=403)
    http("DELETE", "alert-delegations/" + grant, peer, status=404)
    http("DELETE", "alert-delegations/" + grant, owner, status=204)
    http("DELETE", "alert-delegations/" + grant, owner, status=204)
    command(service, "record_result", 1, "Verified gate footage", key, status=403)
    _, expired = issue(ttl=1)
    time.sleep(1.05)
    command(expired, "record_result", 2, "Expired", status=403)
    _, withdrawn = issue()
    c.query(
        f"DELETE FROM bacuri_inventory.point_grants WHERE user_id=(SELECT user_id FROM bacuri_inventory.local_users WHERE username='shift-{'ab'[winner]}') AND point_id='{point}'"
    )
    command(withdrawn, "record_result", 2, "No grant", status=404)
    subprocess.run([c.CORE, "grant-point", "shift-" + "ab"[winner], point], check=True)
    command(owner, "close", 2, "Inspection complete")
    assert replies[winner][2] == command(owner, "claim", 0, "Shift", keys[winner])
    closed = http("GET", work(), owner)
    assert closed["state"] == "closed" and closed["version"] == 3 and not closed["actions"]
    assert [x["action"] for x in closed["history"]] == ["close", "record_result", "claim"]
    event = http("POST", "alert-audit/search", owner, {"query": "gate footage", "alert_id": target})["items"]
    assert len(event) == 1 and event[0]["id"] == receipt["event_id"] and event[0]["actor_kind"] == "service"
    assert event[0]["service"] == "classifier" and event[0]["initiator"] == closed["owner"]
    events = http("POST", "alert-audit/search", admin, {"alert_id": target})["items"]
    assert {e["outcome"] for e in events} == {"applied", "conflict", "denied"}
    assert "Bearer " not in str(events) and service["Authorization"][7:] not in str(events)
    assert (
        event[0]["delegation_id"] == grant
        and event[0]["delegated_action"] == "record_result"
        and event[0]["expires_at"]
    )
    assert not http("POST", "alert-audit/search", owner, {"query": "%", "alert_id": target})["items"]
    for operation in (
        "UPDATE bacuri_events.alert_audit SET reason=reason WHERE false",
        "DELETE FROM bacuri_events.alert_audit WHERE false",
        "TRUNCATE bacuri_events.alert_audit",
    ):
        rejected = subprocess.run(
            ["psql", "-Xq", "-v", "ON_ERROR_STOP=1", "-c", operation], capture_output=True, text=True
        )
        assert rejected.returncode and "append-only" in rejected.stderr
    # Each stale command leaves its own conflict entry. Walk across the page boundary.
    for _ in range(51):
        command(owner, "close", 2, "Stale", status=409)
    page = http("POST", "alert-audit/search", admin, {"alert_id": target})
    older = http("POST", "alert-audit/search", admin, {"alert_id": target, "before": page["next_cursor"]})
    assert len(page["items"]) == 50 and older["items"]
    assert {x["id"] for x in page["items"]}.isdisjoint(x["id"] for x in older["items"])
    print(
        "Alert ownership race, replay after close, audit, delegated scope, expiry and revocation passed",
        flush=True,
    )
