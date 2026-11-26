"""Apply the line threshold to each product using the original integer counters."""

import json
from pathlib import Path
import sys

MINIMUM_PERCENT = 75


def check(label, covered, total, expected, measured):
    missing = expected - measured
    for path in sorted(missing):
        print(f"{label}: missing coverage for {path}")
    hundredths = covered * 10000 // total if total else 0
    print(f"{label}: {covered}/{total} lines ({hundredths // 100}.{hundredths % 100:02}%), minimum {MINIMUM_PERCENT}%")
    valid = bool(expected) and not missing and 0 <= covered <= total and total > 0
    return valid and 100 * covered >= MINIMUM_PERCENT * total


def main(report):
    cpp = json.loads((report / "cpp/summary.json").read_text())
    cpp_files = {path for path in Path("src").rglob("*") if path.suffix in {".cpp", ".cc", ".cxx"}}
    return check(
        "C++", cpp["line_covered"], cpp["line_total"], cpp_files,
        {Path(entry["filename"]) for entry in cpp["files"]},
    )


if __name__ == "__main__":
    sys.exit(0 if main(Path(sys.argv[1])) else 1)
