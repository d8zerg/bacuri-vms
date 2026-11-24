"""Check the release policy, Debian ordering and the version reported for a real checkout."""

from pathlib import Path
import subprocess
import sys
import unittest
from tempfile import TemporaryDirectory
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
import product_version as product


class ReleaseVersion(unittest.TestCase):
    def test_clean_tag_and_development_order(self):
        def build(tag, distance=0, dirty=False):
            return product.build_identity("a" * 40, tag, distance, dirty)

        alpha = build("v2.3.4-alpha.1")
        self.assertEqual(alpha["version"], "2.3.4-alpha.1")
        self.assertEqual(alpha["debian"], "2.3.4~alpha.1-1")
        self.assertTrue(build(alpha["tag"], dirty=True)["version"].endswith(".dirty"))
        series = [
            alpha["debian"] + "~qualification", alpha["debian"], build(alpha["tag"], 2)["debian"],
            build(alpha["tag"], 11)["debian"], build("v2.3.4-alpha.2")["debian"],
            build("v2.3.4-beta.1")["debian"], build("v2.3.4-rc.1")["debian"], build("v2.3.4")["debian"],
            build("v2.3.4", 1)["debian"], build("v2.3.5-alpha.1")["debian"], build("v2.3.5")["debian"],
        ]
        for old, new in zip(series, series[1:]):
            subprocess.run(["dpkg", "--compare-versions", old, "lt", new], check=True)

    def test_commit_impact(self):
        for message, expected in [
            ("fix(core): restore state", 1), ("feat(api): expose status", 2),
            ("refactor(api)!: remove old names", 3), ("fix(api): change encoding\n\nBREAKING CHANGE: use UTF-8", 3),
            ("docs(ui): clarify login", 0), ("build(release): derive version from Git", 0),
        ]:
            self.assertEqual(product.commit_impact(message), expected)
        with self.assertRaises(ValueError):
            product.commit_impact("miscellaneous edits")

    def test_next_tag(self):
        stable = [(4, "v1.2.3")]
        self.assertEqual([product.next_tag(stable, n) for n in range(4)], [None, "v1.2.4", "v1.3.0", "v2.0.0"])
        train = [(1, "v1.3.0-beta.2"), *stable]
        self.assertEqual(product.next_tag(train, 2), "v1.3.0-beta.3")
        self.assertEqual(product.next_tag(train, 3), "v2.0.0-alpha.1")
        self.assertEqual(product.next_tag([(1, "v0.3.1-alpha.1")], 1), "v0.3.1-alpha.2")

    def test_release_gate(self):
        data = product.build_identity("a" * 40, "v1.2.4-alpha.1", 0, False)
        with patch.object(product, "history", return_value=(data["commit"], [(0, data["tag"]), (1, "v1.2.3")])), \
                patch.object(product, "impact_since", return_value=2):
            with self.assertRaisesRegex(ValueError, "below"):
                product.check_release(data)
        for change in [dict(dirty=True), dict(distance=1)]:
            with self.assertRaisesRegex(ValueError, "clean checkout"):
                product.check_release({**data, **change})

    def test_tagged_checkout(self):
        _, releases = product.history()
        tag = releases[0][1]
        with TemporaryDirectory() as tmp:
            clone = Path(tmp) / "checkout"
            subprocess.run(["git", "clone", "-q", "--no-local", "--branch", tag, "-c", "advice.detachedHead=false",
                            str(product.ROOT), clone], check=True)
            with patch.object(product, "ROOT", clone):
                data = product.current()
                self.assertEqual(data["version"], tag[1:])
                self.assertFalse(data["dirty"])
                product.check_release(data)
                (clone / "local-change").touch()
                self.assertTrue(product.current()["dirty"])

    def test_reject_checkout_without_release_history(self):
        with patch.object(product, "git", side_effect=[str(product.ROOT), "true"]):
            with self.assertRaisesRegex(ValueError, "complete Bacuri checkout"):
                product.history()
        with patch.object(product, "git", side_effect=[str(product.ROOT), "false", "v1.0.0 commit", "deadbeef"]):
            with self.assertRaisesRegex(ValueError, "No annotated"):
                product.history()


if __name__ == "__main__":
    unittest.main()
