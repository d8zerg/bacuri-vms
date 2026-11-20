"""Negative admission cases use the same evaluator as the offline report."""

from datetime import date, timedelta
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools" / "lifecycle"))
from policy import evaluate


class AdmissionTest(unittest.TestCase):
    def setUp(self):
        self.today = date.today()
        self.item = dict(target="dev", name="fixture", version="1", support_issue=None, advisories=[])
        self.waiver = dict(target="dev", name="fixture", version="1", issue="TEST-1",
                           until=(self.today + timedelta(days=1)).isoformat(), check="isolation",
                           owner="test", risk="transfer", mitigation="isolated transport",
                           migration="replace dependency", reason="test profile")
        self.proof = {"isolation": {"exit_code": 0}}

    def gate(self, waivers=(), proofs=None):
        return evaluate([self.item], waivers, proofs or {}, self.today)

    def test_missing_support(self):
        self.item["support_issue"] = "missing_support"
        self.assertEqual(self.gate()[0]["reason"], "missing_support")

    def test_expired_exception(self):
        self.waiver["until"] = self.today.isoformat()
        self.assertEqual(self.gate([self.waiver], self.proof)[0]["reason"], "invalid_or_expired_exception")

    def test_unmitigated_critical(self):
        self.item["advisories"] = [dict(id="TEST-1", severity="CRITICAL")]
        self.assertEqual(self.gate()[0]["reason"], "TEST-1")
        self.assertTrue(self.gate([self.waiver]))
        self.assertTrue(self.gate([self.waiver], {"isolation": {"exit_code": 1}}))
        self.assertFalse(self.gate([self.waiver], self.proof))
        self.waiver["version"] = "2"
        self.assertTrue(self.gate([self.waiver], self.proof))
