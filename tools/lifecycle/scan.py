"""Capture the software actually used by the stand and run the offline policy."""

import argparse
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True

import maintenance
from policy import EXTRA_EVIDENCE, IMAGES, LOCKS, RUNTIMES, SNAPSHOT_VERSION, VENDOR_FILES, digest, package_hashes


HERE = Path(__file__).resolve().parent


def output(*args):
    return subprocess.check_output(args, text=True).strip()


def write(path, data):
    path.write_text(json.dumps(data, sort_keys=True, indent=2) + "\n")


def actual(image, *cmd):
    return output("docker", "run", "--rm", "--network", "none", "--entrypoint", cmd[0], image, *cmd[1:])


def verified_graphs(image, evidence):
    expected = {r.split("%")[0] for r in json.loads(Path("conan.lock").read_text())["requires"]}
    for profile in ("gcc-debug", "gcc-release", "clang-debug"):
        data = json.loads(actual(image, "cat", f"/opt/bacuri/deps/{profile}/graph.json"))
        nodes = data["graph"]["nodes"].values()
        refs = {n["ref"] for n in nodes if n.get("recipe") not in ("Consumer", "Virtual") and n.get("ref")}
        wanted = {r for r in expected if profile != "gcc-release" or not r.startswith("gtest/")}
        if refs != wanted:
            raise ValueError(f"Installed Conan graph differs from lock: {profile} {refs ^ wanted}")
        write(evidence / f"conan-{profile}.json", data)


def npm_tree(image, directory, lock):
    js = """
const fs = require('node:fs'), crypto = require('node:crypto');
process.chdir(process.argv[1]);
const bytes = fs.readFileSync('package-lock.json'), installed = [];
for (const [path, pkg] of Object.entries(JSON.parse(bytes).packages)) {
    if (!path) continue;
    const file = path + '/package.json';
    if (pkg.optional && !fs.existsSync(file)) continue;
    const actual = JSON.parse(fs.readFileSync(file));
    if (actual.version !== pkg.version || actual.name !== (pkg.name || path.split('node_modules/').pop()))
        throw Error('Installed package differs from lock: ' + path);
    installed.push([path, actual.name, actual.version]);
}
console.log(JSON.stringify({sha256:crypto.createHash('sha256').update(bytes).digest('hex'), installed}));
"""
    tree = json.loads(actual(image, "node", "-e", js, directory))
    if tree["sha256"] != digest(Path(lock)):
        raise ValueError(f"Image lock differs from source: {lock}")
    return tree


def compose_targets():
    services = json.loads(output("docker", "compose", "--profile", "*", "config", "--format", "json"))["services"]
    selected = {key: output("docker", "compose", "config", "--images", key) for key in IMAGES if key != "scanner"}
    known_images = {image: key for key, image in selected.items()}
    known_builds = {json.dumps(services[key]["build"], sort_keys=True): key for key in selected if "build" in services[key]}
    aliases = {}
    for name, service in services.items():
        representative = known_images.get(service.get("image")) or known_builds.get(json.dumps(service.get("build"), sort_keys=True))
        if not representative:
            raise ValueError(f"Missing dependency scan for Compose service: {name}")
        aliases[name] = representative
    return selected, aliases


def capture(report, offline_from):
    snapshot = report / "snapshot"
    snapshot.mkdir()
    for name in ("feeds", "support"):
        shutil.copyfile(HERE / (name + ".json"), snapshot / (name + ".json"))
    feeds = json.loads((snapshot / "feeds.json").read_text())
    shutil.copyfile("packaging/maintenance/policy.json", snapshot / "maintenance-policy.json")
    policies = json.loads((snapshot / "maintenance-policy.json").read_text())
    scanner = feeds["scanner"]
    selected, aliases = compose_targets()
    if not offline_from and subprocess.run(["docker", "image", "inspect", scanner],
                                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode:
        subprocess.run(["docker", "pull", scanner], check=True)
    selected["scanner"] = scanner
    selected = {key: json.loads(output("docker", "image", "inspect", tag))[0] for key, tag in selected.items()}
    observed = {"images": {}, "versions": {}, "proofs": {}, "compose_services": aliases}
    observed["tools"] = {
        "docker": output("docker", "version", "--format", "{{.Server.Version}}"),
        "compose": output("docker", "compose", "version", "--short"),
        "task": re.search(r"\d+\.\d+\.\d+", output("task", "--version"))[0],
        "trivy": re.search(r"\d+\.\d+\.\d+", actual(scanner, "trivy", "--version"))[0],
    }
    packages = snapshot / "packages"
    if offline_from:
        hashes = json.loads((offline_from / "inputs.json").read_text())["package_sha256"]
        if hashes != package_hashes(offline_from):
            raise ValueError("Offline maintained package export differs")
        shutil.copytree(offline_from / "packages", packages)
    else:
        subprocess.run(["docker", "build", "--target", "maintained-packages", "--output",
                        f"type=local,dest={packages}", "."], check=True)
    reference = maintenance.references(packages, policies)
    write(snapshot / "backport-reference.json", reference)
    observed["maintenance"] = maintenance.capture(selected, policies, reference, actual)
    build_info = (packages / "tools/go-buildinfo.txt").read_text()
    observed["go_compilers"] = {}
    for target, name in (("quality", "gitleaks"), ("postgres", "gosu")):
        compiler = re.search(rf"^/out/{name}: go(\d+\.\d+\.\d+)$", build_info, re.M)
        installed = actual(selected[target]["Id"], "sha256sum", "/usr/local/bin/" + name).split()[0]
        if not compiler or installed != digest(packages / "tools" / name):
            raise ValueError(f"Go build provenance differs from installed executable: {name}")
        observed["go_compilers"][name] = compiler[1]
    observed["gosu"] = actual(selected["postgres"]["Id"], "gosu", "--version").split()[0]
    observed["gitleaks"] = actual(selected["quality"]["Id"], "gitleaks", "version")
    observed["envsubst"] = {s: actual(selected[s]["Id"], "sha256sum", "/usr/local/bin/envsubst").split()[0]
                           for s in ("playback-chrome", "playback-firefox")}
    verified_graphs(selected["dev"]["Id"], snapshot)
    for target, directory, lock in (
        ("api-types", "/opt/bacuri-api-tools", "tools/api-client/package-lock.json"),
        ("ui-kit-check", "/workspace/web/client", "web/client/package-lock.json"),
    ):
        write(snapshot / f"installed-{target}.json", npm_tree(selected[target]["Id"], directory, lock))
    for target, name in RUNTIMES.items():
        command = "google-chrome" if name == "chrome" else name
        result = actual(selected[target]["Id"], command, "--version")
        match = re.search(r"\b(?:v)?(\d+\.\d+(?:\.\d+)*)", result)
        if not match:
            raise ValueError(f"No {name} version: {result}")
        observed["versions"][target] = match[1]

    with tempfile.TemporaryDirectory(prefix=".trivy-", dir=report.parent) as tmp:
        work = Path(tmp)
        mounted = ["docker", "run", "--rm", "--user", f"{os.getuid()}:{os.getgid()}",
                   "-v", f"{work}:/cache", "-v", f"{snapshot}:/snapshot"]
        if offline_from:
            previous = json.loads((offline_from / "feeds.json").read_text())
            for key in ("scanner", "database", "java_database"):
                if previous[key] != feeds[key]:
                    raise ValueError(f"Different pinned {key} in offline snapshot")
            hashes = json.loads((offline_from / "inputs.json").read_text())["vendor_sha256"]
            for filename in VENDOR_FILES:
                src = offline_from / filename
                if digest(src) != hashes[filename]:
                    raise ValueError(f"Offline database checksum differs: {filename}")
                dest = work / filename
                dest.parent.mkdir(exist_ok=True)
                shutil.copyfile(src, dest)
        else:
            for only, repo, pin in (("--download-db-only", "--db-repository", feeds["database"]),
                                    ("--download-java-db-only", "--java-db-repository", feeds["java_database"])):
                subprocess.run([*mounted, scanner, "--cache-dir", "/cache", "image", "--quiet", only, repo, pin],
                               check=True)
        observed["database"] = json.loads((work / "db/metadata.json").read_text())
        observed["java_database"] = json.loads((work / "java-db/metadata.json").read_text())
        # Only inputs used by the source build enter this scan. Local and generated locks are excluded.
        locks = work / "source"
        candidates = output("git", "ls-files", "--cached", "--others", "--exclude-standard",
                            "conan.lock", ":(glob)**/package-lock.json").splitlines()
        paths = {path for path in candidates if Path(path).is_file()}
        if paths != LOCKS:
            raise ValueError(f"Unreviewed or missing dependency manifests: {paths ^ LOCKS}")
        for path in paths:
            dest = locks / path
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(path, dest)
        offline = [*mounted, "--network", "none", scanner, "--cache-dir", "/cache", "--timeout", "15m"]
        flags = ["--quiet", "--scanners", "vuln", "--list-all-pkgs", "--skip-db-update",
                 "--skip-java-db-update", "--offline-scan", "--format", "json"]
        subprocess.run([*offline, "fs", *flags, "--include-dev-deps", "-o", "/snapshot/locks.json",
                        "/cache/source"], check=True)
        maintenance.original_packages(work / "vendor", policies)
        subprocess.run([*offline, "rootfs", *flags, "--pkg-types", "os", "-o", "/snapshot/vendor.json",
                        "/cache/vendor"], check=True)
        host = work / "host"
        host.mkdir()
        plugins = json.loads(output("docker", "info", "--format", "{{json .ClientInfo.Plugins}}"))
        compose = next(p["Path"] for p in plugins if p["Name"] == "compose")
        for tool, binary in (("docker", shutil.which("dockerd")), ("compose", compose), ("task", shutil.which("task"))):
            shutil.copy2(binary, host / tool)
        subprocess.run([*offline, "rootfs", *flags, "-o", "/snapshot/host.json", "/cache/host"], check=True)
        for target, image in selected.items():
            print(f"Reviewing {target} {image['Id']}", flush=True)
            observed["images"][target] = {"id": image["Id"], "layers": image["RootFS"]["Layers"]}
            subprocess.run(["docker", "image", "save", "-o", str(work / "image.tar"), image["Id"]], check=True)
            subprocess.run([*offline, "image", *flags, "-o", f"/snapshot/{target}.json",
                            "--input", "/cache/image.tar"], check=True)
            (work / "image.tar").unlink()
        for directory in ("db", "java-db"):
            (work / directory).rename(snapshot / directory)

    write(snapshot / "observations.json", observed)
    for waiver in feeds["exceptions"]:
        check = waiver["check"]
        if not re.fullmatch(r"[a-z0-9_-]+", check):
            raise ValueError(f"Invalid verification name: {check}")
        if check not in observed["proofs"]:
            script = Path("tests/lifecycle/mitigations") / (check + ".py")
            with (snapshot / (check + ".log")).open("w") as logfile:
                code = subprocess.run([sys.executable, script, str(snapshot)], stdout=logfile,
                                      stderr=subprocess.STDOUT).returncode
            observed["proofs"][check] = {"exit_code": code, "sha256": digest(script)}
    write(snapshot / "observations.json", observed)
    names = {"feeds", "support", "locks", "observations"} | set(IMAGES) | EXTRA_EVIDENCE
    write(snapshot / "inputs.json", {"version": SNAPSHOT_VERSION, "evaluated_on": datetime.now(timezone.utc).date().isoformat(),
                                     "package_sha256": package_hashes(snapshot),
                                     "vendor_sha256": {f: digest(snapshot / f) for f in VENDOR_FILES},
                                     "sha256": {n: digest(snapshot / (n + ".json")) for n in sorted(names)}})
    return snapshot


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--offline-from", type=Path)
    args = parser.parse_args()
    report = Path(output("sh", "tools/quality-report-dir", "dependencies")).resolve()
    print(f"Dependency evidence: {report}", flush=True)
    subprocess.run([sys.executable, "-B", "-m", "unittest", "discover", "-s", "tests/lifecycle"], check=True)
    snapshot = capture(report, args.offline_from)
    result = report / "result.json"
    decision = subprocess.run([sys.executable, HERE / "policy.py", snapshot, result])
    if result.is_file():
        write(report / "blocking.json", json.loads(result.read_text())["failures"])
    return decision.returncode


if __name__ == "__main__":
    sys.exit(main())
