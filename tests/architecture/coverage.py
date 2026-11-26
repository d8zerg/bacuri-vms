"""A high aggregate cannot hide a failing owner, target or missing source."""
import importlib.util
import json
from pathlib import Path
from tempfile import TemporaryDirectory

root = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("coverage_summary", root / "tools/check-coverage-summary.py")
summary = importlib.util.module_from_spec(spec)
spec.loader.exec_module(summary)
minimum = summary.MINIMUM_PERCENT

for owner, source in {
    "modules/inventory": "modules/inventory/onvif/soap.cpp",
    "application/export": "application/export/export.cpp",
    "services/control": "services/control/main.cpp",
    "services/control/transport": "services/control/transport/https.cpp",
    "storage/postgresql": "storage/postgresql/migrations.cpp",
    "admin": "admin/maintenance.cpp",
}.items():
    assert summary.owner(Path("src") / source) == owner

with TemporaryDirectory() as temp:
    fixture = Path(temp)
    summary.ROOT = fixture
    report = fixture / "cpp"
    report.mkdir()
    build = fixture / "build/gcc-coverage"
    build.mkdir(parents=True)
    names = ["src/modules/maps/a.cpp", "src/modules/maps/b.cpp", "src/modules/access/c.cpp"]
    commands, entries = [], []
    for name in names:
        source = fixture / name
        source.parent.mkdir(parents=True, exist_ok=True)
        source.touch()
        commands.append({"file": str(source), "output": f"CMakeFiles/{source.stem}.dir/{source.name}.o"})
        entries.append({"filename": name, "line_covered": minimum, "line_total": 100})
    (build / "compile_commands.json").write_text(json.dumps(commands))

    def passed(selected=""):
        (report / "summary.json").write_text(json.dumps({"files": entries}))
        return summary.main(fixture, selected)

    assert passed()
    entries[0]["line_covered"] = minimum - 1
    entries[2]["line_covered"] = 100
    assert not passed("maps") and passed("access")
    entries[1]["line_covered"] = 100
    assert not passed() and not passed("maps") and passed("access")
    entries[0]["line_total"] = 10000
    entries[0]["line_covered"] = 100 * minimum - 1
    assert not passed("maps")
    entries.pop()
    assert not passed("access")

    header = fixture / "src/storage/sqlite/sqlite.hpp"
    header.parent.mkdir(parents=True)
    header.touch()
    assert not passed("storage/sqlite")
    entries.append({"filename": "src/storage/sqlite/sqlite.hpp", "line_covered": minimum, "line_total": 100})
    assert passed("storage/sqlite")

    unit_report = fixture / "unit"
    unit_report.mkdir()
    unit_files = []
    for entry in names + ["src/application/export/package.cpp", "src/application/recovery/restore.cpp"]:
        source = fixture / entry
        source.parent.mkdir(parents=True, exist_ok=True)
        source.touch()
        unit_files.append({"filename": entry, "line_total": 50, "line_covered": 0})
    (unit_report / "summary.json").write_text(json.dumps({"files": unit_files}))
    assert summary.main(fixture, "--units")
    unit_files.pop()
    (unit_report / "summary.json").write_text(json.dumps({"files": unit_files}))
    assert not summary.main(fixture, "--units")
