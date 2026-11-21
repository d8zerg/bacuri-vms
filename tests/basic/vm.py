"""Boot a disposable Debian guest twice, drive its local console and Chrome on an internal network."""

import base64
import hashlib
import json
import os
import platform
from pathlib import Path
import shutil
import socket
import ssl
import subprocess
import time
from urllib.error import HTTPError
from urllib.request import Request, urlopen

report = Path("/report")
guest = Path("/guest")
origin = "https://bacuri.test:8443"


def ext4(name, size, contents=None):
    disk = Path("/tmp") / (name + ".img")
    with disk.open("wb") as out:
        out.truncate(size * 1024 * 1024)
    args = ["mkfs.ext4", "-q", "-F", "-L", name, "-m", "0"]
    if contents:
        args.extend(["-d", str(contents)])
    subprocess.run([*args, disk], check=True)
    return disk


def seed_guest():
    (guest / "mnt/bundle").mkdir(parents=True, exist_ok=True)
    (guest / "srv/bacuri-archive").mkdir(parents=True, exist_ok=True)
    (guest / "etc/fstab").write_text(
        "LABEL=bacuri-root / ext4 defaults 0 1\nLABEL=bacuri-archive /srv/bacuri-archive ext4 defaults 0 2\nLABEL=bacuri-bundle /mnt/bundle ext4 ro 0 2\n"
    )
    (guest / "etc/systemd/network").mkdir(exist_ok=True, parents=True)
    (guest / "etc/systemd/network/10-guest.network").write_text(
        "[Match]\nName=en* eth*\n[Network]\nAddress=10.0.2.15/24\nGateway=10.0.2.2\nLinkLocalAddressing=no\nIPv6AcceptRA=no\n"
    )
    shutil.copyfile(report / "release-key.gpg", guest / "etc/bacuri-test-key.gpg")
    shutil.copyfile("tests/basic/guest", guest / "usr/local/sbin/basic-test")
    unit = guest / "etc/systemd/system/basic-proof.service"
    unit.write_text(
        "[Unit]\nDescription=Offline installation test console\nAfter=network.target local-fs.target bacuri-core.service bacuri-media.service bacuri-recorder.service\n[Service]\nType=simple\nExecStart=/bin/bash /usr/local/sbin/basic-test\nStandardOutput=journal+console\nStandardError=journal+console\n[Install]\nWantedBy=multi-user.target\n"
    )
    (unit.parent / "multi-user.target.wants" / unit.name).symlink_to("../" + unit.name)
    root = ext4("bacuri-root", 6144, guest)
    archive = ext4("bacuri-archive", 256)
    bundle = report / "bundle"
    size = sum(p.stat().st_size for p in bundle.rglob("*") if p.is_file()) // (1024 * 1024)
    media = ext4("bacuri-bundle", size + 128, bundle)
    return root, archive, media


def request(url, method="GET", data=None, headers=None, trust=None):
    opts = {"Content-Type": "application/json", **(headers or {})}
    req = Request(url, method=method, data=None if data is None else json.dumps(data).encode(), headers=opts)
    try:
        res = urlopen(req, timeout=45, context=trust)
    except HTTPError as err:
        res = err
    with res:
        raw = res.read()
        return res.status, res.headers, json.loads(raw) if raw else None


def until(fn, label, limit=30):
    stop = time.monotonic() + limit
    last = None
    while time.monotonic() < stop:
        try:
            value = fn()
            if value:
                return value
        except OSError as error:
            last = error
        time.sleep(0.15)
    raise RuntimeError("Timed out: " + label) from last


def exercise(line, send):
    ready = line().split()
    if len(ready) != 7 or ready[0] != "READY":
        raise RuntimeError(
            "Guest failed before completing both boots: "
            + (" ".join(ready) if ready and ready[0] == "FAILED" else "invalid response")
        )
    _, password, code, site, point, first, second = ready
    trust = ssl.create_default_context(cadata=base64.b64decode(line()))
    tls_pin = line()
    result = {
        "boots": [first, second],
        "external_network": False,
        "tampered_package_rejected_before_dpkg": True,
    }

    def api(method, path, data=None, headers=None):
        return request(origin + "/api/v1/" + path, method, data, {"Origin": origin, **(headers or {})}, trust)

    print("Testing HTTPS at", socket.gethostbyname("bacuri.test"), flush=True)
    until(lambda: api("GET", "session")[0] == 401, "HTTPS after boot")
    password_only = {"username": "installer", "password": password}
    status, headers, _ = api("POST", "session", password_only)
    assert status == 403 and not headers.get("Set-Cookie")
    assert api("GET", f"sites/{site}/cameras")[0] == 401
    assert api("POST", f"sites/{site}/cameras", {})[0] == 401
    chrome = "http://playback-chrome:4444"

    def driver(method, path, body=None):
        status, _, r = request(chrome + path, method, body)
        if status >= 400:
            raise RuntimeError(str(r["value"].get("message", "WebDriver failed"))[:360])
        return r["value"]

    until(lambda: driver("GET", "/status")["ready"], "Chrome")
    session = driver(
        "POST",
        "/session",
        {
            "capabilities": {
                "alwaysMatch": {
                    "browserName": "chrome",
                    "goog:chromeOptions": {"args": ["--ignore-certificate-errors-spki-list=" + tls_pin]},
                }
            }
        },
    )
    prefix = "/session/" + session["sessionId"]

    def call(path, body):
        return driver("POST", prefix + path, body)

    def js(script, *args):
        return call("/execute/sync", {"script": script, "args": args})

    def form(name, value):
        el = until(lambda: js("return document.querySelector('input[name='+arguments[0]+']')", name), name)
        call("/element/" + el["element-6066-11e4-a52e-4f735466cecf"] + "/value", {"text": value})

    def submit():
        js("document.querySelector('form button[type=submit]').click()")

    def logged_in():
        return js("return document.body.innerText.includes('Выйти')")

    try:
        call(
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
        call("/url", {"url": origin + "/cameras"})
        form("username", "installer")
        form("password", password)
        submit()
        form("enrollment", code)
        submit()
        until(logged_in, "browser registration and first signed assertion")

        def authorized():
            return call(
                "/execute/async",
                {
                    "script": "fetch(arguments[0]).then(r=>arguments[1](r.status))",
                    "args": [f"/api/v1/sites/{site}/cameras"],
                },
            )

        assert authorized() == 200
        js("[...document.querySelectorAll('button')].find(b=>b.textContent==='Выйти').click()")
        form("username", "installer")
        form("password", password)
        submit()
        until(logged_in, "returning two-factor login")
        assert authorized() == 200
        status, headers, challenge = api("POST", "session", password_only)
        assert status == 202 and challenge["kind"] == "get" and not headers.get("Set-Cookie")
        signed = call(
            "/execute/async",
            {
                "script": "const [c, done] = arguments; import('/webauthn.js').then(m=>m.factorAnswer(c,'installer')).then(done).catch(()=>done(null))",
                "args": [challenge],
            },
        )
        assert signed
        assert api("POST", "session/webauthn", {**signed, "signature": "AA"})[0] == 403
        assert api("POST", "session/webauthn", signed)[0] == 403
        result["admin"] = {
            "unenrolled_password": 403,
            "password_only_challenge": 202,
            "password_only_cameras": 401,
            "invalid_signature": 403,
            "reused_challenge": 403,
            "webauthn_cameras": 200,
            "returning_login": 200,
            "chrome": session["capabilities"]["browserVersion"],
        }
        (report / "admin.png").write_bytes(base64.b64decode(driver("GET", prefix + "/screenshot")))
    finally:
        driver("DELETE", prefix)
    status, headers, operator = api("POST", "session", {"username": "viewer", "password": password})
    assert status == 201
    cookie = {"Cookie": headers["Set-Cookie"].split(";")[0]}

    def points():
        status, _, rows = api("GET", f"sites/{site}/points", headers=cookie)
        assert status == 200
        return [r["id"] for r in rows]

    assert points() == []
    send("grant")
    assert line() == "OK" and points() == [point]
    send("revoke")
    assert line() == "OK" and points() == []
    assert api("GET", f"sites/{site}/cameras", headers=cookie)[0] == 403
    send("promote")
    assert line() == "OK"
    assert api("GET", "session", headers=cookie)[0] == 401
    assert api("POST", "session", {"username": "viewer", "password": password})[0] == 403
    result["operator"] = {
        "before_grant": [],
        "after_grant": [point],
        "after_revoke": [],
        "administration": 403,
        "promoted_session": 401,
        "promoted_password_only": 403,
    }

    def rows(command):
        send(command)
        out = []
        while (value := line()) != "END":
            out.append(value)
        return out

    installed = {(p.split()[0], p.split()[1]): p.split()[2] for p in rows("packages")}
    assets = {path: sha for sha, path in (p.split(maxsplit=1) for p in rows("assets"))}
    sbom = json.loads((report / "bundle/sbom.json").read_text())
    assert assets == sbom["web_assets"]
    legacy = {path: sha for sha, path in (p.split(maxsplit=1) for p in rows("legacy"))}
    assert legacy == sbom["legacy_web_assets"]
    packages = sbom["debian_packages"]
    missing = [p["name"] for p in packages if installed.get((p["name"], p["architecture"])) != p["version"]]
    if missing:
        raise AssertionError(("package inventory differs", missing))
    result.update(
        verified_packages=len(packages),
        verified_assets=len(assets) + len(legacy),
        package_version=sbom["package_version"],
        signed_manifest_sha256=hashlib.sha256((report / "bundle/SHA256SUMS").read_bytes()).hexdigest(),
    )
    (report / "installed.json").write_text(
        json.dumps(sorted([" ".join([*k, v]) for k, v in installed.items()]), indent=2)
    )
    result["test_tools"] = {
        "qemu": subprocess.check_output(["qemu-system-x86_64", "--version"], text=True).splitlines()[0],
        "python": platform.python_version(),
        "chrome": session["capabilities"]["browserVersion"],
        "e2fsprogs": subprocess.check_output(["dpkg-query", "-W", "-f=${Version}", "e2fsprogs"], text=True),
        "docker_engine": os.environ["BACURI_TEST_ENGINE"],
        "docker_compose": os.environ["BACURI_TEST_COMPOSE"],
        "chromedriver": session["capabilities"]["chrome"]["chromedriverVersion"],
    }
    send("done")
    return result


def publish(name, data):
    path = report / name
    temporary = path.with_suffix(".tmp")
    temporary.write_text(json.dumps(data, indent=2) + "\n")
    os.chown(temporary, report.stat().st_uid, report.stat().st_gid)
    temporary.replace(path)


def main():
    (report / "installation.json").unlink(missing_ok=True)
    root, archive, kit = seed_guest()
    local, remote = socket.socketpair()
    cmd = [
        "qemu-system-x86_64",
        "-accel",
        "kvm",
        "-cpu",
        "host",
        "-m",
        "2304",
        "-smp",
        "2",
        "-display",
        "none",
        "-monitor",
        "none",
        "-kernel",
        str(next((guest / "boot").glob("vmlinuz-*"))),
        "-initrd",
        str(next((guest / "boot").glob("initrd.img-*"))),
        "-append",
        "root=/dev/vda rw console=ttyS0 net.ifnames=0 panic=-1",
        "-serial",
        "stdio",
        "-chardev",
        f"socket,id=proof,fd={remote.fileno()}",
        "-serial",
        "chardev:proof",
        "-netdev",
        "user,id=net,restrict=on,hostfwd=tcp:0.0.0.0:8443-:8443",
        "-device",
        "virtio-net-pci,netdev=net",
    ]
    for path in (root, archive, kit):
        cmd.extend(["-drive", f"file={path},format=raw,if=virtio,readonly={'on' if path==kit else 'off'}"])
    with (report / "guest.log").open("wb") as log, subprocess.Popen(
        cmd, pass_fds=[remote.fileno()], stdout=log, stderr=log
    ) as vm:
        remote.close()
        local.settimeout(480)
        stream = local.makefile("r", encoding="ascii")

        def line():
            s = stream.readline()
            if not s:
                raise RuntimeError("Guest console closed")
            return s.strip()

        try:
            result = exercise(line, lambda s: local.sendall((s + "\n").encode()))
            assert vm.wait(timeout=45) == 0
            result["status"] = "passed"
            build_tools = json.loads((report / "build-tools.json").read_text())
            build_tools.update(test_tools=result["test_tools"], signed_manifest_sha256=result["signed_manifest_sha256"])
            publish("build-tools.json", build_tools)
            publish("installation.json", result)
            print("Verified offline installation, reboot, SBOM, WebAuthn and permission matrix", flush=True)
        finally:
            if vm.poll() is None:
                vm.kill()
            local.close()


if __name__ == "__main__":
    main()
