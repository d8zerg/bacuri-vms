"""Scan reachable history, the index and the files eligible for the next commit."""

import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def git(*args):
    return subprocess.check_output(["git", *args])


def scan(report, name, *command):
    subprocess.run(
        ["gitleaks", *command, "--no-banner", "--no-color", "--redact=100",
         "--report-path", str(report / f"{name}.json")],
        check=True,
    )


def main(report):
    if git("rev-parse", "--is-shallow-repository").strip() != b"false":
        raise SystemExit("Gitleaks requires a complete Git history")
    scan(report, "history", "git", "--log-opts=--all HEAD", ".")
    scan(report, "index", "git", "--pre-commit", "--staged", ".")
    with tempfile.TemporaryDirectory() as directory:
        snapshot = Path(directory)
        for name in git("ls-files", "--cached", "--others", "--exclude-standard", "--deduplicate", "-z").split(b"\0"):
            if not name:
                continue
            source = Path(name.decode(errors="surrogateescape"))
            if not source.is_symlink() and not source.is_file():
                continue
            target = snapshot / source
            target.parent.mkdir(parents=True, exist_ok=True)
            if source.is_symlink():
                target.write_bytes(os.fsencode(os.readlink(source)))
            else:
                shutil.copyfile(source, target)
        scan(report, "worktree", "dir", directory)


if __name__ == "__main__":
    main(Path(sys.argv[1]))
