"""Check integer line counts by product, source module and compiled production target."""
import json
from collections import defaultdict
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
MINIMUM_PERCENT = json.loads((ROOT / "tests/coverage-policy.json").read_text())["minimum_line_percent"]
SOURCE_SUFFIXES = {".cpp", ".cc", ".cxx"}


def check(label, files, expected, *, enforce_minimum=True):
    missing = expected - files.keys()
    for path in sorted(missing):
        print(f"{label}: missing coverage for {path}")
    covered = sum(item["line_covered"] for item in files.values())
    total = sum(item["line_total"] for item in files.values())
    valid = total > 0 and not missing and all(0 <= f["line_covered"] <= f["line_total"] for f in files.values())
    counts = f"{label}: {covered}/{total} lines ({100 * covered / total if total else 0:.2f}%)"
    if not enforce_minimum:
        print(counts if valid else f"{counts}: incomplete measurement")
        return valid
    passed = valid and 100 * covered >= MINIMUM_PERCENT * total
    print(f"{counts}, minimum {MINIMUM_PERCENT}%: {'PASS' if passed else 'FAIL'}")
    return passed


def owner(path):
    # Each owner keeps its own denominator, including the control transport and SQL adapters.
    directory = path.relative_to("src").parent
    if directory.parts[0] == "services":
        return directory.as_posix()
    if directory.parts[0] in {"application", "modules", "storage"}:
        return "/".join(directory.parts[:2])
    return directory.parts[0]


def main(report, selected=""):
    units = selected == "--units"
    summary = json.loads((report / ("unit" if units else "cpp") / "summary.json").read_text())
    files = {Path(f["filename"]): f for f in summary["files"]}
    expected = {p.relative_to(ROOT) for p in (ROOT / "src").rglob("*") if p.suffix in SOURCE_SUFFIXES}
    if units:
        owners = {owner(path) for path in expected if path.parts[1] == "modules"}
        owners.update({"application/export", "application/recovery"})
        passed = True
        for name in sorted(owners):
            owned = {p: f for p, f in files.items() if owner(p) == name}
            passed = check(f"Unit tests {name}", owned, {p for p in expected if owner(p) == name},
                           enforce_minimum=False) and passed
        return passed
    targets = defaultdict(set)
    commands = json.loads((ROOT / "build/gcc-coverage/compile_commands.json").read_text())
    for command in commands:
        source = Path(command["file"]).resolve()
        if source.is_relative_to(ROOT / "src"):
            target = re.search(r"(?:^|/)CMakeFiles/([^/]+)\.dir/", command["output"])
            if not target:
                raise ValueError(f"Unknown CMake target for {source}")
            targets[target[1]].add(source.relative_to(ROOT))
    missing_targets = expected - set().union(*targets.values())
    if missing_targets:
        raise ValueError(f"Production sources missing from compilation database: {sorted(missing_targets)}")

    passed = True if selected else check("C++", files, expected)
    owners = {owner(p) for p in files.keys() | expected}
    owners.update(owner(p.relative_to(ROOT)) for p in (ROOT / "src").rglob("*.hpp"))
    if selected and selected not in owners:
        matches = owners & {f"modules/{selected}", f"application/{selected}"}
        if len(matches) != 1:
            raise ValueError(f"Unknown or ambiguous coverage module {selected}")
        selected = matches.pop()
    for name in ([selected] if selected else sorted(owners)):
        owned = {p: f for p, f in files.items() if owner(p) == name}
        passed = check(f"Module {name}", owned, {p for p in expected if owner(p) == name}) and passed
    for name, sources in sorted(targets.items()):
        if selected and not any(owner(p) == selected for p in sources):
            continue
        passed = check(f"Target {name}", {p: files[p] for p in sources if p in files}, sources) and passed
    return passed


if __name__ == "__main__":
    sys.exit(0 if main(Path(sys.argv[1]), *sys.argv[2:]) else 1)
