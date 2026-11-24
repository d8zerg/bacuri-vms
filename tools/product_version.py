"""Resolve Bacuri release and build identities from annotated tags on the release branch."""

import argparse
import json
import os
from pathlib import Path
import re
import subprocess
from tempfile import NamedTemporaryFile


ROOT = Path(__file__).resolve().parents[1]
NUMBER = r"(0|[1-9][0-9]*)"
TAG = re.compile(rf"v{NUMBER}\.{NUMBER}\.{NUMBER}(?:-(alpha|beta|rc)\.{NUMBER})?")
HEADER = re.compile(r"(feat|fix|refactor|docs|test|build|ci|chore)\(([^()\s]+)\)(!)?: \S.*")


def git(*args):
    return subprocess.check_output(
        ["git", "-c", f"safe.directory={ROOT}", *args], cwd=ROOT, text=True,
        env={**os.environ, "GIT_OPTIONAL_LOCKS": "0"},
    ).strip()


def tag_key(tag):
    major, minor, patch, channel, serial = TAG.fullmatch(tag).groups()
    return (int(major), int(minor), int(patch), ("alpha", "beta", "rc", None).index(channel), int(serial or 0))


def history():
    if Path(git("rev-parse", "--show-toplevel")) != ROOT or git("rev-parse", "--is-shallow-repository") == "true":
        raise ValueError("Expected a complete Bacuri checkout with its annotated release tags")
    by_commit = {}
    for line in git("for-each-ref", "--format=%(refname:strip=2) %(objecttype) %(*objectname)", "refs/tags/v*").splitlines():
        name, kind, *peeled = line.split()
        if kind == "tag" and TAG.fullmatch(name):
            by_commit.setdefault(peeled[0], []).append(name)
    commits = git("rev-list", "--first-parent", "HEAD").splitlines()
    releases = [(distance, tag) for distance, commit in enumerate(commits)
                for tag in sorted(by_commit.get(commit, ()), key=tag_key, reverse=True)]
    if not releases:
        raise ValueError("No annotated Bacuri release tag on the first-parent history; fetch the release tags")
    return commits[0], releases


def build_identity(commit, tag, distance, dirty):
    major, minor, patch, channel, _ = TAG.fullmatch(tag).groups()
    version = tag[1:]
    if distance or dirty:
        if not channel:
            version = f"{major}.{minor}.{int(patch) + 1}-0"
        version += f".dev.{distance}+g{commit[:12]}"
        if dirty:
            version += ".dirty"
    return dict(version=version, debian=version.replace("-", "~", 1) + "-1",
                tag=tag, commit=commit, distance=distance, dirty=dirty)


def current():
    commit, releases = history()
    distance, tag = releases[0]
    return build_identity(commit, tag, distance, bool(git("status", "--porcelain", "--untracked-files=normal")))


def commit_impact(message):
    subject = message.splitlines()[0]
    match = HEADER.fullmatch(subject)
    if not match:
        raise ValueError(f"Expected a Conventional Commit with component scope: {subject}")
    paragraphs = message.split("\n\n")[1:]
    if match[3] or any(re.match(r"(?:BREAKING CHANGE|[\w-]+)(?:: | #)", paragraph)
                       and re.search(r"^BREAKING[ -]CHANGE: \S", paragraph, re.MULTILINE)
                       for paragraph in paragraphs):
        return 3
    return {"feat": 2, "fix": 1}.get(match[1], 0)


def impact_since(tag):
    messages = git("log", "--no-merges", "--format=%B%x00", f"{tag}..HEAD")
    return max((commit_impact(m.strip()) for m in messages.split("\0") if m.strip()), default=0)


def next_tag(releases, impact):
    if not impact:
        return None
    latest = releases[0][1]
    # While qualifying a release, measure compatibility against the same published baseline.
    base = next((name for _, name in releases if "-" not in name), releases[-1][1])
    major, minor, patch = tag_key(base)[:3]
    current_core = tag_key(latest)[:3]
    if impact == 3:
        required = (major + 1, 0, 0)
    elif impact == 2:
        required = (major, minor + 1, 0)
    else:
        required = current_core if "-" in base else (major, minor, patch + 1)
    core = max(required, current_core)
    result = "v" + ".".join(map(str, core))
    if "-" in latest:
        if core == current_core:
            channel, serial = latest.split("-")[1].split(".")
            return f"{result}-{channel}.{int(serial) + 1}"
        return result + "-alpha.1"
    return result


def write_changed(path, content):
    if path.exists() and path.read_text() == content:
        return
    with NamedTemporaryFile(mode="w", dir=path.parent, delete=False) as file:
        file.write(content)
    os.replace(file.name, path)


def check_release(data):
    if data["distance"] or data["dirty"]:
        raise ValueError("Release signing requires a clean checkout at an annotated release tag")
    _, releases = history()
    previous = [entry for entry in releases if entry[0] > 0]
    if previous:
        floor = next_tag(previous, impact_since(previous[0][1]))
        current_key = tag_key(data["tag"])
        if current_key <= tag_key(previous[0][1]) or (floor and current_key[:3] < tag_key(floor)[:3]):
            raise ValueError("Release tag is below the version required by the previous release and its commits")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("show", "next", "generate"))
    parser.add_argument("directory", nargs="?", type=Path)
    args = parser.parse_args()
    if args.command == "next":
        _, releases = history()
        print(next_tag(releases, impact_since(releases[0][1])) or "No release-triggering commits since the tag")
        return
    data = current()
    if args.command == "show":
        print(data["version"])
        return
    if args.directory is None:
        parser.error("generate requires a build directory")
    args.directory.mkdir(parents=True, exist_ok=True)
    write_changed(args.directory / "product-version.json", json.dumps(data, indent=2) + "\n")
    write_changed(args.directory / "product_version.hpp",
                  '#pragma once\n#define BACURI_VMS_VERSION "' + data["version"] + '"\n'
                  '#define BACURI_DEBIAN_VERSION "' + data["debian"] + '"\n')


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        raise SystemExit(str(error))
