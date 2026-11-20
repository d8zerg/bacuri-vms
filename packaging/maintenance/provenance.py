"""Bind the files in locally rebuilt Debian packages to the applied vendor patches."""

from hashlib import file_digest
import json
from pathlib import Path
import subprocess
import tempfile


def sha(path):
    with path.open("rb") as stream:
        return file_digest(stream, "sha256").hexdigest()


for group, fixes in {"xml": ["CVE-2026-6653"], "soup": ["CVE-2026-2369", "CVE-2026-102560"]}.items():
    folder = Path("/out") / group
    records = {}
    for deb in sorted(folder.glob("*.deb")):
        def field(key):
            return subprocess.check_output(["dpkg-deb", "-f", deb, key], text=True).strip()

        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            subprocess.run(["dpkg-deb", "-x", deb, root], check=True)
            files = {"/" + str(p.relative_to(root)): sha(p) for p in sorted(root.rglob("*"))
                     if p.is_file() and not p.is_symlink()}
        records[field("Package")] = {"version": field("Version"), "deb_sha256": sha(deb), "files": files,
                                    "patches": {cve: sha(Path("/patches") / (cve + ".patch")) for cve in fixes}}
    (folder / "provenance.json").write_text(json.dumps(records, sort_keys=True) + "\n")
