"""Verify distro backports against installed package bytes and keep vendor detection active."""

import json
from hashlib import file_digest
import subprocess

TARGETS = {"conan": ("xml", "soup"), "dev": ("xml", "xml-dev", "soup"), "quality": ("xml", "xml-dev", "soup"),
           "offline-packager": ("xml", "xml-dev", "soup"), "rtsp-camera": ("xml", "xml-dev", "soup"), "postgres": ("xml",)}
ONNX = "CVE-2026-28500"
ONNX_SOURCE = "https://github.com/onnx/onnx/security/advisories/GHSA-hqmj-h5c6-369m"


def references(packages, policies):
    recorded = {}
    for group, rule in policies.items():
        folder = packages / group
        receipts = json.loads((folder / "provenance.json").read_text())
        if receipts.keys() != set(rule["packages"]):
            raise ValueError(f"Wrong reference package set: {group}")
        for name, receipt in receipts.items():
            deb, = folder.glob(f"{name}_*.deb")
            with deb.open("rb") as payload:
                if file_digest(payload, "sha256").hexdigest() != receipt["deb_sha256"]:
                    raise ValueError(f"Reference package checksum differs: {deb.name}")
        recorded[group] = receipts
    return recorded


def capture(images, policies, reference, run):
    evidence = {}
    for target, groups in TARGETS.items():
        image = images[target]["Id"]
        packages = {}
        for group in groups:
            rule = policies[group]
            receipts = json.loads(run(image, "cat", f"/usr/share/bacuri/maintenance/{group}.json"))
            if receipts != reference[group] or set(receipts) != set(rule["packages"]):
                raise ValueError(f"Wrong backport package set in {target}/{group}")
            for name, receipt in receipts.items():
                installed = run(image, "dpkg-query", "-W", "-f=${Status}\t${Version}", name)
                files = receipt["files"]
                if not files or any(not p.startswith("/usr/") or "\n" in p for p in files):
                    raise ValueError(f"Invalid packaged file list for {name}")
                hashes = "".join(f"{value}  {path}\n" for path, value in files.items())
                verified = subprocess.run(["docker", "run", "--rm", "-i", "--network", "none",
                    "--entrypoint", "sha256sum", image, "--check", "--strict", "--quiet", "-"],
                    input=hashes, text=True).returncode == 0
                packages[name] = {**receipt, "installed": installed, "verified_files": verified}
        entry = {"packages": packages}
        if target != "postgres":
            # The vendor advisory concerns onnx.hub.load in the Python frontend.
            entry["onnx_version"] = run(image, "dpkg-query", "-W", "-f=${Version}", "libonnx1t64")
            entry["onnx_files"] = run(image, "dpkg-query", "-L", "libonnx1t64").splitlines()
            entry["python_onnx"] = run(image, "python3", "-I", "-c",
                "import importlib.util; print(importlib.util.find_spec('onnx') is not None)") == "True"
        evidence[target] = entry
    return evidence


def original_packages(root, policies):
    # Scan original Debian versions as well, even if a scanner omits a locally revised package.
    metadata = root / "var/lib/dpkg"
    metadata.mkdir(parents=True)
    (root / "etc").mkdir()
    (root / "etc/debian_version").write_text("13\n")
    (root / "etc/os-release").write_text('ID=debian\nVERSION_ID="13"\nPRETTY_NAME="Debian GNU/Linux 13"\n')
    records = []
    for rule in policies.values():
        for name in rule["packages"]:
            records.append(f"Package: {name}\nStatus: install ok installed\nArchitecture: amd64\n"
                           f"Source: {rule['source_package']}\nVersion: {rule['upstream_version']}\n"
                           f"Description: Original Debian package for backport verification\n")
    (metadata / "status").write_text("\n".join(records))


def verified_fixes(target, policies, proof, groups=None):
    fixes = {}
    errors = []
    for group in (TARGETS[target] if groups is None else groups):
        rule = policies[group]
        expected = {issue: detail["sha256"] for issue, detail in rule["fixes"].items()}
        for name in rule["packages"]:
            item = proof["packages"].get(name, {})
            version = rule["version"]
            if not (item.get("installed") == "install ok installed\t" + version
                    and item.get("version") == version and item.get("patches") == expected
                    and item.get("files") and item.get("verified_files")):
                errors.append({"reason": "backport_verification_failed", "key": [target, name, version]})
                continue
            for issue, detail in rule["fixes"].items():
                fixes[target, name, version, issue] = {
                    "status": "verified_backport", "source": detail["source"], "sha256": detail["sha256"],
                    "deb_sha256": item["deb_sha256"],
                }
    if target != "postgres" and proof.get("onnx_version") == "1.17.0-3+b1":
        files = proof["onnx_files"]
        native = [p for p in files if "/libonnx" in p and ".so" in p]
        if native and proof["python_onnx"] is False and not any(
                p.endswith((".py", ".pyc", ".zip", ".whl")) or "site-packages" in p
                or "dist-packages" in p for p in files):
            fixes[target, "libonnx1t64", proof["onnx_version"], ONNX] = {
                "status": "vulnerable_code_not_present", "source": ONNX_SOURCE,
                "reason": "Native shared libraries only; the affected Python hub is absent from the image.",
            }
    return fixes, errors
