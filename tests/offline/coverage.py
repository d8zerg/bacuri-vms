"""Measure the administrative programs on disposable Debian root and archive disks."""
import base64
import io
import shlex
import shutil
import subprocess
import tarfile
from pathlib import Path

from operations import boot
from vm import configure_guest, ext4, guest, report

build = Path("build/gcc-coverage")
binaries = ("bacuri-install", "bacuri-maintain", "bacuri-vms")
keyring = "/etc/bacuri-test-key.gpg"
kit = "/mnt/bundle"
snapshot = "/safe/site.snap"
key = "/safe/recovery.key"
psql = "runuser -u postgres -- psql -XqAt -v ON_ERROR_STOP=1 -d bacuri_vms"
point = "10000000-0000-4000-8000-000000000099"


def measured(program, *args):
    strip = len(build.resolve().parts) - 1
    return f"GCOV_PREFIX=/safe/gcov GCOV_PREFIX_STRIP={strip} " + shlex.join([f"/opt/coverage/{program}", *args])


def main():
    configure_guest("tests/offline/operations-guest", report / "release-key.gpg")
    (guest / "safe").mkdir()
    with (guest / "etc/fstab").open("a") as f:
        f.write("LABEL=bacuri-safe /safe ext4 defaults 0 2\n")
    overlay = guest / "opt/coverage"
    overlay.mkdir(parents=True)
    for name in binaries:
        shutil.copy2(build / name, overlay / name)
    root = ext4("bacuri-root", 6144, guest)
    fresh_root = root.with_suffix(".fresh.img")
    # Both boots use the same archive, bundle and backup, and separate system disks.
    subprocess.run(["cp", "--sparse=always", "--reflink=auto", root, fresh_root], check=True)
    archive = ext4("bacuri-archive", 256)
    safe = ext4("bacuri-safe", 128)
    package = report / "bundle"
    size = sum(p.stat().st_size for p in package.rglob("*") if p.is_file()) // 1048576
    bundle = ext4("bacuri-bundle", size + 128, package)
    with boot(root, archive, bundle, safe, "coverage-install") as cmd:
        cmd(measured("bacuri-install"), fails=True)
        cmd(measured("bacuri-install", keyring, kit))
        cmd("openssl rand 32 >" + key)
        cmd("openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:P-256 -noenc -days 2 "
            "-subj /CN=bacuri.test -addext subjectAltName=DNS:bacuri.test -keyout /root/tls.key -out /root/tls.crt")
        setup = measured("bacuri-vms", "setup", "bacuri.test", "installer", "/root/tls.crt", "/root/tls.key", "10.0.2.0/24")
        cmd("openssl rand -hex 24 | " + setup)
        assert cmd(measured("bacuri-vms", "--version")).startswith("bacuri.vms ")
        cmd(measured("bacuri-vms", "setup", "invalid", "installer", "/no-cert", "/no-key", "10.0.2.0/24"), fails=True)
        site = cmd("cat /etc/bacuri-vms/site-id")
        cmd(psql + f''' -c "INSERT INTO bacuri_inventory.observation_points VALUES('{site}','{point}','Coverage point')"''')
        cmd(measured("bacuri-vms", "admin", "migrate"))
        cmd(measured("bacuri-maintain", "backup", snapshot, key))
        cmd(measured("bacuri-maintain", "backup", snapshot, key), fails=True)
        cmd("test -s " + snapshot)
        cmd(measured("bacuri-maintain", "diagnose", "/safe/diagnostics"))
        cmd("test -s /safe/diagnostics/events.jsonl && test -s /safe/diagnostics/traces.json")
        cmd(measured("bacuri-maintain", "sample"))
        assert "200 OK" in cmd("printf 'GET /metrics HTTP/1.0\\r\\n\\r\\n' | " + measured("bacuri-maintain", "scrape"))
        cmd(psql + f''' -c "DELETE FROM bacuri_inventory.observation_points WHERE point_id='{point}'"''')
        cmd(measured("bacuri-maintain", "rollback", keyring, kit, snapshot, key))
        assert cmd(psql + f''' -c "SELECT name FROM bacuri_inventory.observation_points WHERE point_id='{point}'"''') == "Coverage point"
    with boot(fresh_root, archive, bundle, safe, "coverage-restore") as cmd:
        # The maintenance executable needs the package's runtime libraries on the clean host.
        cmd(measured("bacuri-install", keyring, kit))
        cmd(measured("bacuri-maintain", "restore", keyring, kit, snapshot, key))
        assert cmd("cat /etc/bacuri-vms/site-id") == site
        assert cmd(psql + f''' -c "SELECT name FROM bacuri_inventory.observation_points WHERE point_id='{point}'"''') == "Coverage point"
        encoded = cmd("tar -C /safe/gcov -czf - . | base64 -w0")
    counts = report / "counters"
    counts.mkdir()
    with tarfile.open(fileobj=io.BytesIO(base64.b64decode(encoded)), mode="r:gz") as data:
        for member in data:
            path = Path(member.name)
            if member.isfile() and path.suffix == ".gcda":
                if path.is_absolute() or ".." in path.parts or not (build / path.with_suffix(".gcno")).is_file():
                    raise ValueError(f"Unexpected GCC counter {path}")
                dest = counts / path
                dest.parent.mkdir(parents=True, exist_ok=True)
                dest.write_bytes(data.extractfile(member).read())
    print("Verified installation, backup, rollback and clean-host restore with GCC coverage", flush=True)


if __name__ == "__main__":
    main()
