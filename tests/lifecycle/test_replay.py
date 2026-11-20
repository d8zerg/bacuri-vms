"""Verify the exit status and reasons of the public offline admission command."""

from datetime import date
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools" / "lifecycle"))
import policy
from test_maintenance import ROOT, receipt


class ReplayTest(unittest.TestCase):
    def test_offline_command(self):
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)

            (root / "packages").mkdir()
            (root / "packages/fixture").write_text("fixture")
            for filename in policy.VENDOR_FILES:
                path = root / filename
                path.parent.mkdir(exist_ok=True)
                path.write_text("fixture")

            def write(name, data):
                (root / (name + ".json")).write_text(json.dumps(data))

            def run(expected):
                names = set(policy.IMAGES) | policy.EXTRA_EVIDENCE | {"support", "observations", "feeds", "locks"}
                write("inputs", {"evaluated_on": date.today().isoformat(),
                                 "package_sha256": policy.package_hashes(root),
                                 "vendor_sha256": {n: policy.digest(root / n) for n in policy.VENDOR_FILES},
                                 "sha256": {n: policy.digest(root / (n + ".json")) for n in names}})
                result = subprocess.run([sys.executable, "-B", policy.__file__, root, root / "result.json"],
                                        capture_output=True, text=True)
                self.assertEqual(result.returncode, int(bool(expected)), result.stderr)
                reasons = {i["reason"] for i in json.loads((root / "result.json").read_text())["failures"]}
                self.assertEqual(reasons, expected)

            record = {"source": "https://example.invalid/upstream", "revision": "fixture", "channel": "upstream"}
            observed = {"database": {"UpdatedAt": date.today().isoformat()}, "images": {}, "proofs": {}, "versions": {}}
            observed["go_compilers"] = {"gosu": "1.1", "gitleaks": "1.1"}
            observed["java_database"] = observed["database"]
            observed["tools"] = {name: "1" for name in ("docker", "compose", "task", "trivy")}
            runtime = {"go": dict(record, versions=["1.1"])}
            policies = json.loads((ROOT / "packaging/maintenance/policy.json").read_text())
            observed["maintenance"] = {}

            for target in policy.IMAGES:
                observed["images"][target] = {"layers": ["layer"], "id": "image"}
                system_packages = [{"Name": "system", "Version": "1"}]
                if target in policy.maintenance.TARGETS:
                    installed = {}
                    for group in policy.maintenance.TARGETS[target]:
                        rule = policies[group]
                        for name in rule["packages"]:
                            installed[name] = receipt(rule)
                            system_packages.append({"Name": name, "Version": rule["version"]})
                    observed["maintenance"][target] = {"packages": installed}
                write(target, {"Metadata": {"OS": {"Family": "debian"}, "DiffIDs": ["layer"]},
                               "Results": [{"Class": "os-pkgs", "Target": "Debian", "Packages": system_packages}]})
                if target in policy.RUNTIMES:
                    observed["versions"][target] = "1.1"
                    runtime[policy.RUNTIMES[target]] = dict(record, versions=["1.1"], branches={
                        "1": {"start": date.min.isoformat(), "end": date.max.isoformat()}})
            for name in policy.EXTRA_EVIDENCE:
                write(name, {})
            write("vendor", {"Results": [{"Type": "debian", "Packages": [{"Name": name}
                    for group in policies.values() for name in group["packages"]]}]})
            write("maintenance-policy", policies)
            write("backport-reference", {g: {n: receipt(rule) for n in rule["packages"]}
                                         for g, rule in policies.items()})
            write("host", {"Results": [{"Target": name, "Packages": [{"Name": name, "Version": "1"}]}
                                       for name in ("docker", "compose", "task")]})
            write("observations", observed)
            support = {"observed": date.today().isoformat(), "packages": {"npm:test@1": record}, "runtimes": runtime,
                       "tools": {name: dict(record, version="1") for name in observed["tools"]}}
            feeds = {"database": "pinned", "max_snapshot_age_days": 30, "exceptions": []}
            locks = [{"Target": target, "Type": "npm", "Packages": [{"Name": "test", "Version": "1"}]}
                     for target in policy.LOCKS]
            write("locks", {"Results": locks})
            write("feeds", feeds)
            write("support", support)
            run(set())

            write("support", dict(support, packages={}))
            run({"missing_support"})
            write("support", support)
            feeds["exceptions"] = [dict(target="dev", name="fixture", version="1", issue="TEST-1",
                                        until=date.min.isoformat())]
            write("feeds", feeds)
            run({"invalid_or_expired_exception"})
            feeds["exceptions"] = []
            write("feeds", feeds)
            locks[0]["Vulnerabilities"] = [{"PkgName": "test", "InstalledVersion": "1", "Severity": "CRITICAL",
                                             "VulnerabilityID": "TEST-1"}]
            write("locks", {"Results": locks})
            run({"TEST-1"})
            (root / "packages/fixture").write_text("changed")
            with self.assertRaisesRegex(ValueError, "package export checksum"):
                policy.checked_inputs(root)
