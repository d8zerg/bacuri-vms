"""Verify that the access link line rejects the map operation used by the probe."""
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile


def check(command):
    output = command.index("-o") + 1
    stamp = Path(command[output])
    stamp.unlink(missing_ok=True)
    with tempfile.TemporaryDirectory() as work:
        command[output] = str(Path(work) / "probe")
        link = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                              text=True, env={**os.environ, "LC_ALL": "C"})
    if link.returncode == 0:
        sys.exit("Access unexpectedly linked the map placement lookup")
    diagnostic = re.search(r"undefined (?:reference to|symbol:)[^\n]*point_on_plan[^\n]*", link.stdout)
    if not diagnostic:
        sys.exit(link.stdout or f"Linker exited with status {link.returncode}")
    stamp.write_text(diagnostic[0] + "\n")


if __name__ == "__main__":
    check(sys.argv[1:])
