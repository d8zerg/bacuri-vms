"""Collect attributable upstream maintenance evidence for the locked source packages."""

from concurrent.futures import ThreadPoolExecutor
from datetime import timezone, datetime
from hashlib import sha256
import json
from pathlib import Path
import re
import subprocess
from urllib.parse import quote
from urllib.request import Request, urlopen


ROOT = Path(__file__).resolve().parent


def fetch(url):
    req = Request(url, headers={"User-Agent": "bacuri-vms-dependency-review"})
    with urlopen(req, timeout=45) as response:
        body = response.read()
    return body, {"source": url, "revision": sha256(body).hexdigest()}


def npm_record(pair):
    name, version = pair
    url = "https://registry.npmjs.org/" + quote(name, safe="") + "/" + quote(version, safe="")
    raw, record = fetch(url)
    info = json.loads(raw)
    if (info["name"], info["version"]) != pair:
        raise ValueError(f"Registry version mismatch: {pair}")
    # A project correction channel is distinct from a guaranteed LTS branch.
    channel = info.get("bugs") or info.get("repository") or info.get("homepage")
    if isinstance(channel, dict):
        channel = channel.get("url")
    owners = info.get("maintainers", [])
    if not channel and owners:
        channel = "https://www.npmjs.com/~" + quote(owners[0]["name"], safe="")
    record.update(channel=channel, deprecated=info.get("deprecated") or None)
    return f"npm:{name}@{version}", record


def collect():
    packages = set()
    paths = subprocess.check_output(["git", "ls-files", "--cached", "--others", "--exclude-standard",
                                     ":(glob)**/package-lock.json"], text=True).splitlines()
    for lock in map(Path, paths):
        if not lock.is_file():
            continue
        for path, item in json.loads(lock.read_text())["packages"].items():
            if path:
                packages.add((item.get("name") or path.split("node_modules/")[-1], item["version"]))
    with ThreadPoolExecutor(max_workers=10) as pool:
        evidence = dict(pool.map(npm_record, sorted(packages)))
    projects = {"boost": ("boostorg/boost", "boost-"), "gtest": ("google/googletest", "v"),
                "nlohmann_json": ("nlohmann/json", "v")}
    for ref in json.loads(Path("conan.lock").read_text())["requires"]:
        name, version = ref.split("#")[0].split("/")
        repo, prefix = projects[name]
        _, record = fetch(f"https://api.github.com/repos/{repo}/git/ref/tags/{prefix}{version}")
        record["channel"] = f"https://github.com/{repo}/security"
        evidence[f"conan:{name}@{version}"] = record

    channels = {}
    for name, url in {
        "go": "https://go.dev/dl/?mode=json",
        "node": "https://raw.githubusercontent.com/nodejs/Release/main/schedule.json",
        "chrome": "https://googlechromelabs.github.io/chrome-for-testing/last-known-good-versions.json",
        "firefox": "https://product-details.mozilla.org/1.0/firefox_versions.json",
        "postgres": "https://www.postgresql.org/support/versioning/",
    }.items():
        raw, record = fetch(url)
        if name == "go":
            record["versions"] = [row["version"].removeprefix("go") for row in json.loads(raw) if row["stable"]]
        elif name == "node":
            record["branches"] = {v.lstrip("v"): {"start": row["lts"], "end": row["end"]}
                                  for v, row in json.loads(raw).items() if row.get("lts")}
        elif name == "chrome":
            record["versions"] = [json.loads(raw)["channels"]["Stable"]["version"]]
        elif name == "firefox":
            data = json.loads(raw)
            record["versions"] = [data[k].removesuffix("esr") for k in ("LATEST_FIREFOX_VERSION", "FIREFOX_ESR")]
        else:
            cells = re.findall(r"<td[^>]*>\s*([\d.]+)\s*</td>\s*<td[^>]*>\s*([\d.]+)\s*</td>\s*<td[^>]*>\s*Yes\s*</td>", raw.decode(), re.I)
            record["versions"] = [minor for major, minor in cells]
            if not record["versions"]:
                raise ValueError("PostgreSQL support table is empty")
        channels[name] = record
    # The Selenium image ships a renamed Go module without an embedded release version.
    raw, envsubst = fetch("https://api.github.com/repos/ndviet/envsubst/releases/tags/v1.5.6")
    release = json.loads(raw)
    envsubst["version"] = release["tag_name"].lstrip("v")
    envsubst["sha256"] = next(a["digest"].split(":")[1] for a in release["assets"]
                              if a["name"] == "envsubst-Linux-x86_64")
    tools = {}
    for name, repo, command in (
        ("docker", "moby/moby", ["docker", "version", "--format", "{{.Server.Version}}"]),
        ("compose", "docker/compose", ["docker", "compose", "version", "--short"]),
        ("task", "go-task/task", ["task", "--version"]),
        ("trivy", "aquasecurity/trivy", None),
    ):
        if command:
            version = re.search(r"\d+\.\d+\.\d+", subprocess.check_output(command, text=True))[0]
        else:
            version = json.loads((ROOT / "feeds.json").read_text())["scanner"].split(":")[1].split("@")[0]
        if name == "docker":
            raw, record = fetch(f"https://docs.docker.com/engine/release-notes/{version.split('.')[0]}/")
            if version.encode() not in raw:
                raise ValueError(f"No Docker release notes for {version}")
        else:
            _, record = fetch(f"https://api.github.com/repos/{repo}/git/ref/tags/v{version}")
        tools[name] = dict(record, version=version, channel=f"https://github.com/{repo}/security")
    return {"tools": tools, "envsubst": envsubst, "observed": datetime.now(timezone.utc).date().isoformat(),
            "packages": evidence, "runtimes": channels}


if __name__ == "__main__":
    result = collect()
    feeds_file = ROOT / "feeds.json"
    feeds = json.loads(feeds_file.read_text())
    for name, schema in (("database", "2"), ("java_database", "1")):
        repository = feeds[name].split("@")[0]
        descriptor = json.loads(subprocess.check_output(
            ["docker", "buildx", "imagetools", "inspect", repository + ":" + schema, "--format",
             "{{json .Manifest}}"], text=True))
        digest = descriptor["digest"]
        if not re.fullmatch("sha256:[0-9a-f]{64}", digest):
            raise ValueError(f"Invalid database digest from {repository}")
        feeds[name] = repository + "@" + digest
    for path, data in ((feeds_file, feeds), (ROOT / "support.json", result)):
        temporary = path.with_suffix(".tmp")
        temporary.write_text(json.dumps(data, indent=2, sort_keys=path.name == "support.json") + "\n")
        temporary.replace(path)
