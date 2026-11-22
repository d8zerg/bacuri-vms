"""Exercise both signed releases and recover onto an independent Debian root disk."""

import base64
from contextlib import contextmanager
from datetime import datetime, timezone
import hashlib
import json
import re
from pathlib import Path
import secrets
import shutil
import socket
import ssl
import subprocess
import time
import uuid
from vm import configure_guest, ext4, guest, origin, publish, qemu_command, report, request, until

NEW = "/mnt/bundle/new"
OLD = "/mnt/bundle/old"
MAINT = NEW + "/bacuri-maintain"
KEY = "/safe/recovery.key"
PSQL = "runuser -u postgres -- psql -XqAt -v ON_ERROR_STOP=1 -d bacuri_vms"
LIMITS = json.loads(Path("tests/cameras/qualification.json").read_text())["recovery"]


def disks():
    configure_guest("tests/offline/operations-guest", report / "release-key.gpg")
    (guest / "safe").mkdir()
    with (guest / "etc/fstab").open("a") as f:
        f.write("LABEL=bacuri-safe /safe ext4 defaults 0 2\n")
    shutil.copyfile("/previous/release-key.gpg", guest / "etc/old-release.gpg")
    product = guest / "usr/local/bin/test-camera"
    shutil.copyfile(report / "test-camera", product)
    product.chmod(0o755)
    plugins = guest / "usr/local/lib/bacuri-test"
    plugins.mkdir(parents=True)
    shutil.copyfile(report / "test-overlay.so", plugins / "libgstpango.so")
    (guest / "etc/systemd/system/test-camera.service").write_text(
        "[Unit]\nDescription=Synthetic RTSP source\nAfter=network.target\n[Service]\nEnvironment=GST_PLUGIN_PATH=/usr/local/lib/bacuri-test\nExecStart=/usr/local/bin/test-camera\nRestart=on-failure\n[Install]\nWantedBy=multi-user.target\n"
    )
    root = ext4("bacuri-root", 6144, guest)
    spare = Path("/tmp/standby.img")
    subprocess.run(["cp", "--sparse=always", "--reflink=auto", root, spare], check=True)
    folder = Path("/tmp/distribution")
    folder.mkdir()
    for name, src in (("new", report / "bundle"), ("old", Path("/previous/bundle"))):
        shutil.copytree(src, folder / name)
    size = sum(p.stat().st_size for p in folder.rglob("*") if p.is_file()) // 1048576
    return (
        root,
        spare,
        ext4("bacuri-archive", 256),
        ext4("bacuri-bundle", size + 128, folder),
        ext4("bacuri-safe", 128),
    )


@contextmanager
def boot(root, archive, kit, safe, label, *, power_loss=False):
    print("Booting", label, flush=True)
    local, remote = socket.socketpair()
    with (report / (label + ".log")).open("wb") as log, subprocess.Popen(
        qemu_command([root, archive, kit, safe], kit, remote),
        pass_fds=[remote.fileno()],
        stdout=log,
        stderr=log,
    ) as proc:
        remote.close()
        local.settimeout(360)
        stream = local.makefile("rb")

        def send(value):
            local.sendall(value + b"\n")

        def command(script, *, fails=False):
            send(base64.b64encode(script.encode()))
            reply = stream.readline().strip().split(b" ", 1)
            if not reply[0].isdigit() or int(reply[0]) > 255:
                raise RuntimeError("Lost guest console in " + label)
            if (reply[0] == b"0") == fails:
                raise RuntimeError("Guest command failed in " + label + " (see guest log)")
            return base64.b64decode(reply[1]).decode().strip() if len(reply) > 1 else ""

        try:
            assert stream.readline().strip() == b"READY"
            yield command
            if power_loss:
                proc.kill()
                proc.wait(timeout=5)
            else:
                send(b"POWER_OFF")
                assert proc.wait(timeout=35) == 0
        except Exception:
            if proc.poll() is None:
                diagnostic = command(
                    "journalctl --no-pager -o cat -n 12 -u test-camera -u bacuri-core -u bacuri-recorder; ss -lnt"
                )
                print("Guest status:", diagnostic[-2400:], flush=True)
            raise
        finally:
            if proc.poll() is None:
                proc.kill()
            stream.close()
            local.close()


def install(cmd, kit, key):
    cmd(
        f"gpgv --keyring {key} {kit}/bacuri-install.sig {kit}/bacuri-install && {kit}/bacuri-install {key} {kit}"
    )
    cmd(f"gpgv --keyring /etc/bacuri-test-key.gpg {MAINT}.sig {MAINT}")


class Browser:
    def __init__(self, pin):
        def wd(method, path, data=None):
            status, _, r = request("http://playback-chrome:4444" + path, method, data)
            if status >= 400:
                raise RuntimeError(r["value"]["message"][:300])
            return r["value"]

        self.wd = wd
        until(lambda: wd("GET", "/status")["ready"], "Chrome")
        session = wd(
            "POST",
            "/session",
            {
                "capabilities": {
                    "alwaysMatch": {
                        "browserName": "chrome",
                        "goog:chromeOptions": {"args": ["--ignore-certificate-errors-spki-list=" + pin]},
                    }
                }
            },
        )
        self.prefix = "/session/" + session["sessionId"]
        self.post(
            "/webauthn/authenticator",
            {
                "protocol": "ctap2",
                "transport": "usb",
                "hasResidentKey": True,
                "hasUserVerification": True,
                "isUserConsenting": True,
                "isUserVerified": True,
            },
        )
        self.visit()

    def post(self, path, data):
        return self.wd("POST", self.prefix + path, data)

    def visit(self):
        self.post("/url", {"url": origin + "/cameras"})

    def js(self, script, *args):
        return self.post(
            "/execute/async",
            {
                "script": "const done=arguments[arguments.length-1];(async()=>{"
                + script
                + "})().then(done).catch(e=>done({error:String(e)}))",
                "args": args,
            },
        )

    def sign(self, challenge):
        answer = self.js(
            "return (await import('/webauthn.js')).factorAnswer(arguments[0], 'installer')", challenge
        )
        assert "error" not in answer, answer
        return answer

    def close(self):
        self.wd("DELETE", self.prefix)


def stamp(us):
    return (
        datetime.fromtimestamp(us / 1e6, timezone.utc)
        .isoformat(timespec="microseconds")
        .replace("+00:00", "Z")
    )


class Site:
    def __init__(self, cmd, password):
        self.password = password
        self.site = cmd("cat /etc/bacuri-vms/site-id")
        self.base = "sites/" + self.site
        cert = base64.b64decode(
            cmd("openssl x509 -outform DER -in /etc/bacuri-vms/core/tls.crt | base64 -w0")
        )
        self.trust = ssl.create_default_context(cadata=cert)
        self.browser = Browser(
            cmd(
                "openssl x509 -in /etc/bacuri-vms/core/tls.crt -pubkey -noout | openssl pkey -pubin -outform DER | openssl dgst -binary -sha256 | base64"
            )
        )

    def api(self, method, path, body=None, headers=None):
        args = (origin + "/api/v1/" + path, method, body, {"Origin": origin, **(headers or {})}, self.trust)
        result = request(*args)
        if method == "POST" and path == "session" and result[0] == 429:
            delay = int(result[1]["Retry-After"])
            assert 0 < delay <= 60
            time.sleep(delay)
            result = request(*args)
        return result

    def wait(self):
        until(lambda: self.api("GET", "session")[0] == 401, "site HTTPS")

    def credentials(self, username):
        return {"username": username, "password": self.password}

    def session(self, result):
        status, h, d = result
        assert status == 201, (status, d)
        return {"Cookie": h["Set-Cookie"].split(";")[0], "X-CSRF-Token": d["csrf_token"]}

    def enroll(self, cmd, code):
        status, _, create = self.api(
            "POST", "session", {**self.credentials("installer"), "enrollment_code": code}
        )
        assert status == 202 and create["kind"] == "create"
        answer = self.browser.sign(create)
        assert self.api("POST", "session/webauthn", answer)[0] == 204
        return answer["credential_id"]

    def login(self):
        status, _, challenge = self.api("POST", "session", self.credentials("installer"))
        assert status == 202 and challenge["kind"] == "get"
        return self.session(self.api("POST", "session/webauthn", self.browser.sign(challenge)))

    def viewer(self):
        return self.session(self.api("POST", "session", self.credentials("viewer")))

    def access(self, viewer):
        s, _, points = self.api("GET", self.base + "/points", headers=viewer)
        assert s == 200 and [p["id"] for p in points] == [self.point], points
        assert self.api("GET", self.base + "/cameras", headers=viewer)[0] == 403
        s, _, history = self.api("GET", self.base + "/alerts", headers=viewer)
        assert s == 200 and self.alert in json.dumps(history), history

    def frame(self, m, viewer):
        return until(lambda: self.decode(m, viewer), "archive media service")

    def decode(self, m, viewer):
        interval = {"start": stamp(m["begin_utc_us"]), "end": stamp(m["end_utc_us"])}
        s, _, session = self.api(
            "POST",
            self.base + "/points/" + self.point + "/media-sessions",
            {"mode": "archive", "interval": interval},
            {**viewer, "Idempotency-Key": str(uuid.uuid4())},
        )
        if s == 503:
            return None
        assert s == 201, (s, session)
        base = "media-sessions/" + session["id"]
        try:
            s, _, manifest = self.api("GET", base + "/manifest", headers=viewer)
            if s == 503:
                return None
            assert s == 200
            (part,) = manifest["parts"]
            name, value = viewer["Cookie"].split("=", 1)
            self.browser.post(
                "/cookie", {"cookie": {"name": name, "value": value, "path": "/", "secure": True}}
            )
            urls = ["/api/v1/" + base + "/parts/" + part[k] for k in ("init_id", "media_id")]
            pixels = self.browser.js(
                """
                const parts = await Promise.all(arguments[0].map(async u => {const r=await fetch(u); if(!r.ok) throw Error(r.status);return r.arrayBuffer()}));
                const v=document.createElement('video');v.muted=true;v.playsInline=true;
                document.body.appendChild(v);
                const url=URL.createObjectURL(new Blob(parts,{type:'video/mp4'}));
                try {
                    await new Promise((resolve,reject)=>{v.onloadeddata=resolve;v.onerror=()=>reject(Error('decode'));v.src=url});
                    const start=v.buffered.start(0),end=v.buffered.end(0);
                    await new Promise((resolve,reject)=>{v.onseeked=resolve;v.onerror=()=>reject(Error('seek'));v.currentTime=start+Math.min(0.15,(end-start)/2)});
                    const c=document.createElement('canvas');c.width=v.videoWidth;c.height=v.videoHeight;
                    const ctx=c.getContext('2d');ctx.drawImage(v,0,0);
                    const rgba=ctx.getImageData(0,0,c.width,c.height).data;
                    let min=255,max=0;
                    for(let i=0;i<rgba.length;i+=4){
                        if(rgba[i+3]!==255)throw Error('transparent frame');
                        min=Math.min(min,rgba[i]);max=Math.max(max,rgba[i]);
                    }
                    if(max-min<32)throw Error('missing test pattern');
                    const hash=new Uint8Array(await crypto.subtle.digest('SHA-256',rgba));
                    return Array.from(hash,n=>n.toString(16).padStart(2,'0')).join('');
                } finally {v.remove();URL.revokeObjectURL(url)}
            """,
                urls,
            )
            if pixels == {"error": "Error: 503"}:
                return None
            assert isinstance(pixels, str) and re.fullmatch("[0-9a-f]{64}", pixels), pixels
            return pixels
        finally:
            self.api("DELETE", base, headers=viewer)

    def assert_lost(self, old_id):
        s, _, c = self.api("POST", "session", self.credentials("installer"))
        assert s == 202, (s, c.get("code"))
        assert c["credential_id"] != old_id
        answer = self.browser.sign({**c, "credential_id": old_id})
        assert self.api("POST", "session/webauthn", answer)[0] == 403


def query(cmd, sql):
    return cmd(PSQL + ' -c "' + sql + '"')


def blocks(cmd):
    rows = query(
        cmd,
        "SELECT manifest FROM bacuri_archive.blocks WHERE unavailable_reason IS NULL ORDER BY begin_utc_us",
    )
    return [json.loads(row) for row in rows.splitlines()]


def latest(cmd, after=0):
    return until(
        lambda: next((m for m in reversed(blocks(cmd)) if m["begin_utc_us"] > after), None),
        "completed recording",
        55,
    )


def operation(action, kit, snapshot):
    trust = "/etc/old-release.gpg" if kit == OLD else "/etc/bacuri-test-key.gpg"
    return f"{MAINT} {action} {trust} {kit} /safe/{snapshot} {KEY}"


def scrape_metrics(cmd):
    return cmd("exec 7<>/dev/tcp/127.0.0.1/9464; printf 'GET /metrics HTTP/1.0\\r\\n\\r\\n' >&7; cat <&7")


def diagnostic(cmd, label, check, forbidden):
    path = "/tmp/probe-" + label

    def take():
        raw = cmd(f"rm -rf {path}; {MAINT} diagnose {path}; cat {path}/metrics.prom")
        if not check(raw):
            return None
        logs = [json.loads(v) for v in cmd(f"cat {path}/events.jsonl").splitlines()]
        payload = json.loads(cmd(f"cat {path}/traces.json"))
        resource, = payload["resourceSpans"]
        scope, = resource["scopeSpans"]
        assert resource["resource"] == {"attributes": [{"key": "service.name", "value": {"stringValue": "bacuri-maintain"}}]}
        assert scope["scope"] == {"name": "bacuri.vms.operations"}
        traces = scope["spans"]
        assert len(traces) == 4 and len(logs) == 3
        assert all(
            e["ok"]
            and any(
                s["traceId"] == e["trace_id"]
                and s["spanId"] == e["span_id"]
                and s["parentSpanId"] == traces[-1]["spanId"]
                for s in traces
            )
            for e in logs
        )
        assert all(
            e["trace_id"] in cmd("journalctl -n 30 -t bacuri-maintain -o cat --no-pager") for e in logs
        )
        fields = {e["event"]: e["values"] for e in logs}
        assert fields.keys() == {"systemd.services", "recorder.sources", "archive.catalog"}
        assert fields["systemd.services"].keys() == {"core", "media", "recorder"}
        assert fields["archive.catalog"].keys() == {"available_bytes", "pending", "recording_stopped"}
        assert all(str(uuid.UUID(k)) == k for k in fields["recorder.sources"])
        for e in logs:
            assert set(e) == {"event", "trace_id", "span_id", "ok", "values"} and all(
                type(v) is int for v in e["values"].values()
            )
            span = next(s for s in traces if s["spanId"] == e["span_id"])
            assert {a["key"]: int(a["value"]["intValue"]) for a in span["attributes"]} == e["values"]
        assert all(
            re.fullmatch(r'bacuri_[a-z_]+(?:\{[a-z]+="[a-z0-9.\-]+"\})? [0-9]+', line)
            for line in raw.splitlines()
        )
        serialized = json.dumps([raw, logs, payload])
        assert all(value not in serialized for value in forbidden)
        scrape = scrape_metrics(cmd)
        if not check(scrape):
            return None
        assert cmd(f"ls -1 {path}") == "events.jsonl\nmetrics.prom\ntraces.json"
        return {"metrics": raw, "events": logs, "spans": traces}

    return until(take, label, 40)


def main():
    root, standby, archive, kit, safe = disks()
    password = secrets.token_hex(24)
    with boot(standby, archive, kit, safe, "standby-install") as cmd:
        cmd("printf '%s\\n' 'u bacuri-test-local -' > /run/local-user.conf; systemd-sysusers /run/local-user.conf")
        install(cmd, NEW, "/etc/bacuri-test-key.gpg")
        spare_uid = cmd("id -u bacuri-recorder")
        assert cmd("test ! -f /etc/bacuri-vms/site-id; echo clean") == "clean"
    with boot(root, archive, kit, safe, "upgrade", power_loss=True) as cmd:
        install(cmd, OLD, "/etc/old-release.gpg")
        assert cmd("id -u bacuri-recorder") != spare_uid
        cmd(
            "openssl rand 32 >/safe/recovery.key; openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:P-256 -noenc -days 2 -subj /CN=bacuri.test -addext subjectAltName=DNS:bacuri.test -keyout /root/tls.key -out /root/tls.crt"
        )
        old_admin = cmd("command -v bacuri-vms || command -v bacuri-basic")
        cmd(f"{NEW}/bacuri-install /etc/bacuri-test-key.gpg {NEW}", fails=True)
        code = cmd(
            f"printf '%s\\n' '{password}' | {old_admin} setup bacuri.test installer /root/tls.crt /root/tls.key 10.0.2.0/24 | tail -2 | grep -E '^[a-f0-9]{{64}}$'"
        )
        cmd(f"printf '%s\\n' '{password}' | {old_admin} admin create-user viewer operator")
        cmd(
            "for f in /etc/bacuri-vms/{core,recorder}.env; do echo BACURI_LEGACY_RTSP_ADDRESSES=10.0.2.15 >> $f; done; systemctl restart bacuri-core bacuri-recorder; systemctl enable --now test-camera"
        )
        site = Site(cmd, password)
        site.wait()
        ledger = "bacuri_meta" if query(cmd, "SELECT to_regclass('bacuri_meta.schema_migrations') IS NOT NULL") == "t" else "bacuri_inventory"
        source_schema = int(query(cmd, f"SELECT max(version) FROM {ledger}.schema_migrations"))
        assert source_schema in (14, 15, 16, 17, 18, 19, 20), source_schema
        old_id = site.enroll(cmd, code)
        a = site.login()
        s, _, camera = site.api(
            "POST",
            site.base + "/cameras",
            {
                "name": "Recovery camera",
                "main_url": "rtsp://10.0.2.15:8554/main",
                "secondary_url": "rtsp://10.0.2.15:8554/sub",
                "credentials": {"username": "camera", "password": password},
            },
            {**a, "Idempotency-Key": str(uuid.uuid4())},
        )
        assert s == 201, (s, camera)
        site.point = camera["point_id"]
        cmd(f"{old_admin} admin grant-point viewer {site.point}")
        old = latest(cmd)
        viewer = site.viewer()
        pixels = site.frame(old, viewer)
        cmd(
            f"set -a; source /etc/bacuri-vms/core.env; set +a; tool=/usr/lib/bacuri-vms/bacuri-vms-observation-source; $tool sample {site.point} 1 | $tool"
        )
        status, _, feed = site.api("GET", site.base + "/alerts", headers=viewer)
        assert status == 200
        site.alert = feed["items"][0]["id"]
        site.access(viewer)
        print("Previous release recorded and played back a frame; interrupting migration", flush=True)
        upgrade = operation("upgrade", NEW, "previous.snap")
        lock_table = "bacuri_maps.map_regions" if source_schema == 16 else "bacuri_inventory.webauthn_credentials"
        blocked_statement = "ALTER TABLE bacuri_maps.map_regions" if source_schema == 16 else "ALTER TABLE bacuri_inventory.local_users SET SCHEMA"
        if source_schema == 17:
            lock_table = "bacuri_maps.point_placements"
            blocked_statement = "ALTER TABLE bacuri_maps.point_placements"
        if source_schema == 18:
            lock_table = "bacuri_maps.floor_plans"
            blocked_statement = "ALTER TABLE bacuri_maps.floor_plans"
        lock_mode = "ACCESS SHARE"
        if source_schema == 20:
            lock_table = "bacuri_access.site_roles"
            blocked_statement = "ALTER TABLE bacuri_access.site_roles"
        if source_schema == 19:
            lock_table = "bacuri_maps.map_sources"
            blocked_statement = "CREATE INDEX map_sources_pending"
            lock_mode = "ROW EXCLUSIVE"
        cmd(
            f"""({PSQL} -c "SET application_name='migration_blocker'; BEGIN; LOCK {lock_table} IN {lock_mode} MODE; SELECT pg_sleep(180)" >/dev/null) & waiter=$!
sleep 0.5
{upgrade} >/root/update.log 2>&1 & update=$!
terminated=''
for i in {{1..900}}; do
 terminated=$({PSQL} -c "SELECT pg_terminate_backend(pid) FROM pg_stat_activity WHERE datname='bacuri_vms' AND wait_event_type='Lock' AND query LIKE '%{blocked_statement}%'")
 [[ $terminated != t ]] || break
 kill -0 "$update" 2>/dev/null || break
 sleep .1
done
test "$terminated" = t
if wait $update; then exit 1; fi
{PSQL} -c "SELECT pg_terminate_backend(pid) FROM pg_stat_activity WHERE application_name='migration_blocker'" >/dev/null
wait $waiter || true
test -f /var/lib/bacuri-maintenance/pending
for service in bacuri-core bacuri-recorder bacuri-media; do test "$(systemctl is-active "$service" || true)" = inactive; done
"""
        )
        assert query(cmd, f"SELECT max(version) FROM {ledger}.schema_migrations") == str(max(15, source_schema))
        h = cmd("sha256sum /safe/previous.snap")
        cmd(upgrade)
        assert h == cmd("sha256sum /safe/previous.snap")
        site.wait()
        site.access(viewer)
        assert site.frame(old, viewer) == pixels
        new = latest(cmd, old["end_utc_us"])
        site.frame(new, viewer)
        assert cmd("dpkg-query -S /usr/sbin/bacuri-vms") == "bacuri-vms: /usr/sbin/bacuri-vms"
        print("Upgrade resumed; checking live faults", flush=True)
        cmd("head -c -1 /safe/previous.snap > /safe/truncated.snap")
        cmd(operation("rollback", OLD, "truncated.snap"), fails=True)
        cmd(
            "test ! -f /var/lib/bacuri-maintenance/pending; systemctl is-active --quiet bacuri-core bacuri-media bacuri-recorder"
        )
        assert query(cmd, "SELECT max(version) FROM bacuri_meta.schema_migrations") == "21"
        forbidden = (
            password,
            code,
            a["Cookie"].split("=", 1)[1],
            cmd("sed -n '2p' /root/tls.key"),
            "ftyp",
            "base64,",
        )
        diagnostics = {}
        cmd("systemctl stop test-camera")
        diagnostics["camera"] = diagnostic(
            cmd, "camera", lambda t: f'bacuri_camera_up{{device="{camera["id"]}"}} 0' in t, forbidden
        )
        cmd("systemctl start test-camera")
        latest(cmd, new["end_utc_us"])
        cmd("systemctl stop bacuri-media")
        diagnostics["media"] = diagnostic(
            cmd, "media", lambda t: 'bacuri_service_up{service="media"} 0' in t, forbidden
        )
        cmd("systemctl start bacuri-media")
        cmd("systemctl stop bacuri-core")
        diagnostics["catalog"] = diagnostic(
            cmd,
            "catalog",
            lambda t: any(
                v.startswith("bacuri_catalog_pending_blocks ") and int(v.split()[-1]) > 0
                for v in t.splitlines()
            ),
            forbidden,
        )
        cmd("systemctl start bacuri-core")
        cmd(
            "fallocate -l $(( $(stat -f -c '%a * %S' /srv/bacuri-archive | tr -d '\\n') - 1048576 )) /srv/bacuri-archive/fill"
        )
        diagnostics["archive"] = diagnostic(
            cmd, "space", lambda t: "bacuri_archive_recording_stopped 1" in t, forbidden
        )
        cmd("rm /srv/bacuri-archive/fill")
        def capacity_restored():
            metrics = scrape_metrics(cmd)
            return "bacuri_archive_recording_stopped 0" in metrics and "bacuri_archive_available_bytes " in metrics

        until(capacity_restored, "archive capacity after removing the test file", 40)
        publish("diagnostics.json", diagnostics)
        cmd(operation("rollback", OLD, "previous.snap"))
        site.wait()
        assert query(cmd, f"SELECT max(version) FROM {ledger}.schema_migrations") == str(source_schema)
        assert (
            site.api("GET", "session", headers=a)[0] == 401
            and site.api("GET", "session", headers=viewer)[0] == 401
        )
        viewer = site.viewer()
        site.access(viewer)
        assert site.frame(old, viewer) == pixels
        rollback_frame = latest(cmd, new["end_utc_us"])
        site.frame(rollback_frame, viewer)
        cmd(operation("upgrade", NEW, "second.snap"))
        site.wait()
        restored_id = site.enroll(cmd, cmd("bacuri-vms admin enroll-admin installer"))
        a = site.login()
        site.assert_lost(old_id)
        backup_started = time.time()
        cmd(f"{MAINT} backup /safe/current.snap {KEY}")
        replacement = cmd("bacuri-vms admin recover-admin installer")
        assert site.api("GET", "session", headers=a)[0] == 401
        replacement_id = site.enroll(cmd, replacement)
        a2 = site.login()
        site.assert_lost(restored_id)
        audit_before = query(
            cmd, "SELECT action FROM bacuri_access.credential_audit ORDER BY recorded_at"
        ).splitlines()
        assert audit_before[-3:] == ["revoked", "recovery_started", "enrolled"]
        latest(cmd, rollback_frame["end_utc_us"])
        catalog_before_loss = {m["block_id"] for m in blocks(cmd)}
        outage = time.time()
        print("Powering off source, restoring on independent Debian disk", flush=True)
    with boot(standby, archive, kit, safe, "restore") as cmd:
        manifests = cmd("for m in /srv/bacuri-archive/recorder/blocks/*/manifest.json; do cat \"$m\"; echo; done")
        completed = [json.loads(line) for line in manifests.splitlines()]
        known = {m["block_id"] for m in completed}
        assert catalog_before_loss <= known
        last = max(completed, key=lambda m: m["end_utc_us"])
        cmd("systemctl start test-camera")
        cmd(operation("restore", NEW, "current.snap"))
        site.wait()
        site.browser.visit()
        assert (
            site.api("GET", "session", headers=a2)[0] == 401
            and site.api("GET", "session", headers=a)[0] == 401
        )
        assert site.api("POST", "session", site.credentials("installer"))[0] == 403
        viewer = site.viewer()
        site.access(viewer)
        config_rto = time.time() - outage
        assert site.frame(old, viewer) == pixels
        old_rto = time.time() - outage
        fresh = until(
            lambda: next((m for m in blocks(cmd) if m["begin_utc_us"] >= int(outage * 1e6)), None),
            "first new recording",
            LIMITS["new_recording"]["rto_s"],
        )
        site.frame(fresh, viewer)
        rto = time.time() - outage
        gap = max(0, (fresh["begin_utc_us"] - last["end_utc_us"]) / 1e6)
        assert known <= {m["block_id"] for m in blocks(cmd)}
        site.enroll(cmd, cmd("bacuri-vms admin enroll-admin installer"))
        site.login()
        site.assert_lost(restored_id)
        site.assert_lost(replacement_id)
        audit = query(
            cmd, "SELECT action FROM bacuri_access.credential_audit ORDER BY recorded_at"
        ).splitlines()
        assert "restore_reset" in audit and "enrolled" in audit
        rpo = outage - backup_started
        print("Measured RPO/RTO", config_rto, old_rto, rto, gap, rpo, flush=True)
        assert config_rto <= LIMITS["configuration"]["rto_s"] and rpo <= LIMITS["configuration"]["rpo_s"]
        assert (
            old_rto <= LIMITS["completed_archive"]["rto_s"]
            and rto <= LIMITS["new_recording"]["rto_s"]
            and gap <= LIMITS["new_recording"]["rpo_s"]
        )
        site.browser.close()
        publish(
            "operations.json",
            {
                "status": "passed",
                "source_schema": source_schema,
                "target_schema": 21,
                "interrupted_migration_retried": True,
                "rollback_schema": source_schema,
                "independent_root": True,
                "verified_old_blocks": len(known),
                "lost_healthy_blocks": 0,
                "old_frame_sha256": pixels,
                "config_rto_seconds": config_rto,
                "config_rpo_seconds": rpo,
                "old_frame_rto_seconds": old_rto,
                "new_frame_rto_seconds": rto,
                "new_recording_gap_seconds": gap,
                "credential_audit": audit,
                "audit_before_failure": audit_before,
                "recovery_limits": LIMITS,
                "signed_bundles": {
                    p: hashlib.sha256((Path(root) / "bundle/SHA256SUMS").read_bytes()).hexdigest()
                    for p, root in (("previous", "/previous"), ("current", "/report"))
                },
            },
        )
    print("Verified upgrade, interrupted migration, rollback and clean-host recovery", flush=True)


if __name__ == "__main__":
    main()
