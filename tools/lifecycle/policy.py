"""Evaluate captured dependency evidence without network or container access."""

import argparse
from datetime import date
from hashlib import file_digest
import json
from pathlib import Path

import maintenance


IMAGES = {"conan": "build", "ui-kit-catalog": "test", "scanner": "test", "dev": "build", "quality": "test", "postgres": "product", "rtsp-camera": "test",
          "offline-packager": "build", "install-vm": "test", "api-types": "build", "ui-kit-check": "build", "prototype-check": "test",
          "playback-chrome": "test", "playback-firefox": "test"}
LOCKS = {"conan.lock", "tests/prototype/package-lock.json", "tools/api-client/package-lock.json",
         "web/client/package-lock.json"}
RUNTIMES = {"postgres": "postgres", "playback-chrome": "chrome", "playback-firefox": "firefox",
            "api-types": "node", "prototype-check": "node", "ui-kit-check": "node"}
DISTROS = {"alpine": "https://security.alpinelinux.org/", "debian": "https://security-tracker.debian.org/tracker/",
           "ubuntu": "https://ubuntu.com/security/notices"}


EXTRA_EVIDENCE = {"backport-reference", "host", "vendor", "maintenance-policy", "conan-gcc-debug", "conan-gcc-release", "conan-clang-debug",
                  "installed-api-types", "installed-ui-kit-check", "installed-prototype-check"}
BUNDLERS = {"offline-packager": "https://www.debian.org/security/", "install-vm": "https://www.debian.org/security/",
            "conan": "https://www.debian.org/security/",
            "ui-kit-catalog": "https://github.com/nginx/docker-nginx-unprivileged",
            "scanner": "https://github.com/aquasecurity/trivy/security", "dev": "https://www.debian.org/security/", "quality": "https://github.com/gitleaks/gitleaks/security",
            "rtsp-camera": "https://www.debian.org/security/", "api-types": "https://github.com/nodejs/docker-node",
            "ui-kit-check": "https://github.com/microsoft/playwright", "prototype-check": "https://github.com/microsoft/playwright",
            "playback-chrome": "https://github.com/SeleniumHQ/docker-selenium",
            "playback-firefox": "https://github.com/SeleniumHQ/docker-selenium",
            "postgres": "https://github.com/docker-library/postgres"}


VENDOR_FILES = ("db/metadata.json", "db/trivy.db", "java-db/metadata.json", "java-db/trivy-java.db")


def digest(path):
    with path.open("rb") as file:
        return file_digest(file, "sha256").hexdigest()


def evaluate(items, exceptions, proofs, today, fixes=None):
    fixes = fixes or {}
    failures = []
    active = {}
    fields = ("target", "name", "version", "issue")
    for waiver in exceptions:
        key = tuple(waiver[field] for field in fields)
        proof = proofs.get(waiver.get("check"))
        if (date.fromisoformat(waiver["until"]) <= today or not all(waiver.get(field)
                for field in ("owner", "risk", "mitigation", "migration", "reason"))
                or not proof or proof["exit_code"] != 0 or key in active):
            failures.append({"reason": "invalid_or_expired_exception", "key": key})
        else:
            active[key] = waiver
    for row in items:
        row["exceptions"] = []
        prefix = (row["target"], row["name"], row["version"])
        issue = row["support_issue"]
        if issue:
            key = (*prefix, issue)
            if issue == "outside_supported_profile" and key in active:
                row["exceptions"].append(active[key])
            else:
                failures.append({"reason": issue, "key": key})
        for advisory in row["advisories"]:
            advisory["applicability"] = "installed_version_matched"
            if advisory.get("status") == "not_affected":
                advisory["applicability"] = "vendor_not_affected"
                continue
            repair = fixes.get((*prefix, advisory["id"]))
            if repair:
                advisory["applicability"] = repair["status"]
                advisory["verification"] = repair
                continue
            if advisory["severity"] != "CRITICAL":
                continue
            key = (*prefix, advisory["id"])
            if key in active:
                row["exceptions"].append(active[key])
                advisory["applicability"] = "verified_mitigation"
            else:
                failures.append({"reason": advisory["id"], "key": key})
    return failures


def package_hashes(folder):
    packages = folder / "packages"
    if not packages.is_dir():
        raise ValueError("Missing maintained package export")
    files = sorted(path for path in packages.rglob("*") if path.is_file())
    if not files:
        raise ValueError("Empty maintained package export")
    return {str(path.relative_to(packages)): digest(path) for path in files}


def recorded_targets(targets, manifest):
    previous_names = {"offline-packager": "basic-packager", "install-vm": "basic-vm"}
    if manifest["sha256"].keys() >= set(previous_names.values()):
        return {previous_names.get(name, name): value for name, value in targets.items()}
    return targets


def checked_inputs(folder):
    manifest = json.loads((folder / "inputs.json").read_text())
    required = {"feeds", "support", "observations", "locks"} | set(recorded_targets(IMAGES, manifest)) | EXTRA_EVIDENCE
    if set(manifest["sha256"]) != required:
        raise ValueError("Incomplete dependency snapshot")
    for name, expected in manifest["sha256"].items():
        if digest(folder / (name + ".json")) != expected:
            raise ValueError(f"Snapshot checksum differs: {name}")
    if set(manifest["vendor_sha256"]) != set(VENDOR_FILES):
        raise ValueError("Incomplete advisory database snapshot")
    for name, expected in manifest["vendor_sha256"].items():
        if digest(folder / name) != expected:
            raise ValueError(f"Advisory database checksum differs: {name}")
    if manifest.get("package_sha256") != package_hashes(folder):
        raise ValueError("Maintained package export checksum differs")
    return manifest


def advisory(finding, target):
    return {"id": finding["VulnerabilityID"], "severity": finding["Severity"], "fix": finding.get("FixedVersion"),
            "source": finding.get("DataSource", {}).get("URL") or finding.get("PrimaryURL"),
            "status": finding.get("Status", "affected"), "target": target}


def package_version(target, group, pkg, observed, support):
    if pkg.get("Version"):
        return pkg["Version"]
    if target == "host" and pkg.get("Relationship") == "root":
        return observed["tools"].get(group["Target"])
    if target == "postgres" and pkg["Name"] == "github.com/tianon/gosu":
        return observed["gosu"]
    if target == "quality" and pkg["Name"] == "github.com/zricethezav/gitleaks/v8":
        return observed["gitleaks"]
    if target == "scanner" and pkg["Name"] == "github.com/aquasecurity/trivy":
        return observed["tools"]["trivy"]
    if pkg["Name"] == "github.com/a8m/envsubst" and target in observed["envsubst"]:
        release = support["envsubst"]
        if observed["envsubst"][target] == release["sha256"]:
            return release["version"]
    return None


def reconcile_backports(items, vendor, policies, reference, observed, targets):
    groups = [g for g in vendor["Results"] if g.get("Type") == "debian"]
    expected = {name for policy in policies.values() for name in policy["packages"]}
    if len(groups) != 1 or {p["Name"] for p in groups[0]["Packages"]} != expected:
        raise ValueError("Incomplete original Debian inventory for locally maintained packages")
    indexed = {(item["target"], item["name"], item["version"]): item for item in items}
    fixes, failures = {}, []
    for target, scope in targets.items():
        proof = observed["maintenance"][target]
        for group in scope:
            for name in policies[group]["packages"]:
                installed = proof["packages"].get(name, {})
                expected = reference[group][name]
                if any(installed.get(k) != expected.get(k) for k in ("files", "patches", "deb_sha256", "version")):
                    raise ValueError(f"Reference and installation disagree: {target}/{name}")
        proofs, errors = maintenance.verified_fixes(target, policies, proof, scope)
        fixes.update(proofs)
        failures.extend(errors)
        for group in scope:
            rule = policies[group]
            for name in rule["packages"]:
                item = indexed.get((target, name, rule["version"]))
                if not item:
                    failures.append({"reason": "missing_installed_backport", "key": [target, name, rule["version"]]})
                    continue
                item["upstream_version"] = rule["upstream_version"]
                found = {a["id"]: a for a in item["advisories"]}
                for v in groups[0].get("Vulnerabilities", []):
                    if v["PkgName"] == name and v["InstalledVersion"] == rule["upstream_version"]:
                        previous = found.get(v["VulnerabilityID"])
                        if not previous or v["Severity"] == "CRITICAL":
                            found[v["VulnerabilityID"]] = advisory(v, "original Debian package")
                item["advisories"] = list(found.values())
    return fixes, failures


def report(folder):
    manifest = checked_inputs(folder)
    today = date.fromisoformat(manifest["evaluated_on"])
    bundlers = recorded_targets(BUNDLERS, manifest)

    def read(name):
        return json.loads((folder / (name + ".json")).read_text())

    feeds, support, observed = read("feeds"), read("support"), read("observations")
    failures = []
    for kind, timestamp in (("advisories", observed["database"]["UpdatedAt"][:10]),
                            ("java_advisories", observed["java_database"]["UpdatedAt"][:10]),
                            ("maintenance", support["observed"])):
        age = (today - date.fromisoformat(timestamp)).days
        if not 0 <= age <= feeds["max_snapshot_age_days"]:
            failures.append({"reason": "snapshot_age", "kind": kind, "days": age})
    items = []

    def append(target, name, version, role, origin, revision, issue, advisories):
        items.append(dict(target=target, name=name, version=version, role=role, source=origin,
                          revision=revision, support_issue=issue, advisories=advisories))

    def packages(scan, target, role, source=None, unsupported=False):
        for group in scan["Results"]:
            scope = target if group.get("Class") == "os-pkgs" else target + "/" + group["Target"]
            vulns = {}
            for v in group.get("Vulnerabilities", []):
                key = (v["PkgName"], v["InstalledVersion"])
                vulns.setdefault(key, []).append(advisory(v, group["Target"]))
            for pkg in group.get("Packages", []):
                name = pkg["Name"]
                version = package_version(target, group, pkg, observed, support)
                origin, revision = source, feeds["database"]
                if target == "host":
                    receipt = support["tools"].get(group["Target"], {})
                    origin, revision = receipt.get("source"), receipt.get("revision")
                elif target != "locks" and group.get("Class") != "os-pkgs":
                    origin, revision = bundlers[target], observed["images"][target]["id"]
                issue = "ended_support" if unsupported else None
                if not version:
                    version, issue = "unversioned", "missing_version"
                component_role = role
                if target == "locks":
                    key = f"{group['Type']}:{name}@{version}"
                    receipt = support["packages"].get(key, {})
                    origin, revision = receipt.get("source"), receipt.get("revision")
                    if not origin or not receipt.get("channel") or not revision:
                        issue = "missing_support"
                    elif receipt.get("deprecated"):
                        issue = "ended_support"
                    component_role = "test" if name == "gtest" or group["Target"].startswith("tests/") else (
                        "build" if pkg.get("Dev") or group["Target"].startswith("tools/") else "product")
                # Debian epochs and revisions are separate fields in the package inventory.
                if pkg.get("Epoch"):
                    version = f"{pkg['Epoch']}:{version}"
                if pkg.get("Release"):
                    version += "-" + pkg["Release"]
                advisories = vulns.pop((name, version), [])
                append(group["Target"] if target == "locks" else scope, name, version, component_role,
                       origin, revision, issue, advisories)
            # Preserve findings even for analyzers with a distinct package identity or version format.
            for (name, version), advisories in vulns.items():
                append(scope, name, version, role, source or feeds["database"], feeds["database"],
                       None, advisories)

    locks = read("locks")
    if {g["Target"] for g in locks["Results"] if g.get("Packages")} != LOCKS:
        raise ValueError("Missing or unreviewed dependency lock")
    packages(locks, "locks", "product")
    for target, role in recorded_targets(IMAGES, manifest).items():
        scan = read(target)
        meta = scan["Metadata"]
        distro = meta["OS"]
        image = observed["images"][target]
        if (not meta["DiffIDs"] or meta["DiffIDs"] != image["layers"] or not any(
                g.get("Class") == "os-pkgs" and g.get("Packages") for g in scan["Results"])):
            raise ValueError(f"Image inventory mismatch: {target}")
        source = DISTROS.get(distro["Family"])
        if not source:
            failures.append({"reason": "missing_distribution_support", "target": target})
        packages(scan, target, role, source, distro.get("EOSL", False))
        if target in RUNTIMES:
            name = RUNTIMES[target]
            value = observed["versions"][target]
            record = support["runtimes"][name]
            if name == "node":
                branch = record["branches"].get(value.split(".")[0])
                supported = branch and date.fromisoformat(branch["start"]) <= today < date.fromisoformat(branch["end"])
            else:
                supported = value in record["versions"]
            append(target, name, value, role, record["source"], record["revision"],
                   None if supported else "outside_supported_profile", [])
    go = support["runtimes"]["go"]
    for name, version in observed["go_compilers"].items():
        append("maintained-tools", name, version, "build", go["source"], go["revision"],
               None if version in go["versions"] else "outside_supported_profile", [])
    host = read("host")
    if {g["Target"] for g in host["Results"] if g.get("Packages")} != {"docker", "compose", "task"}:
        raise ValueError("Host tool inventory is incomplete")
    packages(host, "host", "build")
    for name in ("docker", "compose", "task", "trivy"):
        version = observed["tools"][name]
        receipt = support["tools"].get(name, {})
        append("orchestration", name, version, "test", receipt.get("source"), receipt.get("revision"),
               None if receipt.get("version") == version and receipt.get("channel") else "missing_support", [])
    fixes, issues = reconcile_backports(items, read("vendor"), read("maintenance-policy"),
                                       read("backport-reference"), observed, recorded_targets(maintenance.TARGETS, manifest))
    failures.extend(issues)
    failures += evaluate(items, feeds["exceptions"], observed["proofs"], today, fixes)
    return {"as_of": today.isoformat(), "input_sha256": manifest["sha256"], "components": items,
            "failures": failures}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("snapshot", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    result = report(args.snapshot)
    args.output.write_text(json.dumps(result, ensure_ascii=False, separators=(",", ":")) + "\n")
    print(f"Dependency review: {len(result['components'])} component records, {len(result['failures'])} blocking findings")
    return int(bool(result["failures"]))


if __name__ == "__main__":
    raise SystemExit(main())
