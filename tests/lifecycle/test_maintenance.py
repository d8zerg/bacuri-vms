"""Binding a backport to the wrong bytes, revision or upstream patch must fail."""

import json
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/lifecycle"))
from maintenance import ONNX, verified_fixes


def receipt(rule):
    return {"installed": "install ok installed\t" + rule["version"], "version": rule["version"],
            "patches": {cve: p["sha256"] for cve, p in rule["fixes"].items()},
            "files": {"/usr/lib/example": "fixture"}, "verified_files": True, "deb_sha256": "fixture"}


class BackportTest(unittest.TestCase):
    def test_changed_package_or_patch(self):
        policies = json.loads((ROOT / "packaging/maintenance/policy.json").read_text())
        original = receipt(policies["xml"])
        for edit in ({"verified_files": False}, {"patches": {}}, {"version": "different"}, {"files": {}}):
            with self.subTest(edit=edit):
                fixes, errors = verified_fixes("postgres", policies, {"packages": {"libxml2": dict(original, **edit)}})
                self.assertFalse(fixes)
                self.assertEqual(errors[0]["reason"], "backport_verification_failed")

    def test_python_frontend_changes_onnx_applicability(self):
        policies = json.loads((ROOT / "packaging/maintenance/policy.json").read_text())
        proof = {"packages": {n: receipt(rule) for rule in policies.values() for n in rule["packages"]},
                 "onnx_version": "1.17.0-3+b1", "onnx_files": ["/usr/lib/libonnx.so.1"], "python_onnx": False}
        key = ("dev", "libonnx1t64", "1.17.0-3+b1", ONNX)
        self.assertIn(key, verified_fixes("dev", policies, proof)[0])
        self.assertNotIn(key, verified_fixes("dev", policies, dict(proof, python_onnx=True))[0])
        proof["onnx_files"].append("/usr/lib/python3/dist-packages/onnx/hub.py")
        self.assertNotIn(key, verified_fixes("dev", policies, proof)[0])
