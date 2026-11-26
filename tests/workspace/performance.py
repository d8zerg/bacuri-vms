"""Check cold navigation costs measured in the fixture and integrated workspaces."""
import json
from pathlib import Path
from statistics import median
import sys

evidence = json.loads(Path(sys.argv[1]).read_text())
assert len(evidence) == 8
passed = True
for before, after in zip(evidence[:4], evidence[4:], strict=True):
    assert (before["phase"], after["phase"]) == ("fixture", "integrated")
    assert before["browser"] == after["browser"] and before["route"] == after["route"]
    assert len(before["samples"]) == len(after["samples"]) == 3
    size = max(s["bytes"] for s in after["samples"])
    ms = median(s["ms"] for s in after["samples"])
    size_limit = min(400 * 1024, 1.15 * max(s["bytes"] for s in before["samples"]))
    time_limit = min(2000, 1000 + median(s["ms"] for s in before["samples"]))
    result = f"{after['browser']} {after['route']}: {size}/{size_limit:.0f} bytes, {ms}/{time_limit} ms"
    print(result)
    if size > size_limit or ms > time_limit:
        passed = False
if not passed:
    sys.exit("Workspace performance budget exceeded")
