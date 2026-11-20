"""Reset counters and discard notes for translation units removed from the CMake build."""
import json
from pathlib import Path

build = Path("build/gcc-coverage").resolve()
commands = json.loads((build / "compile_commands.json").read_text())
notes = {(Path(entry["directory"]) / entry["output"]).with_suffix(".gcno") for entry in commands}
if not notes or not all(path.is_relative_to(build) for path in notes):
    raise ValueError("Invalid coverage compile database")

for path in build.rglob("*.gcno"):
    if path not in notes:
        path.unlink()
for path in build.rglob("*.gcda"):
    path.unlink()
