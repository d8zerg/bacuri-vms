"""Validate the local Markdown links, scenario matrix and decision registry."""

from collections import Counter
from functools import cache
from pathlib import Path
import re
from urllib.parse import unquote, urlsplit


@cache
def lines(path):
    result = []
    fence = None
    for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        marker = re.match(r"^ {0,3}(`{3,}|~{3,})", line)
        if fence:
            if line.strip().startswith(fence) and not line.strip().strip(fence[0]):
                fence = None
        elif marker:
            fence = marker[1]
        else:
            result.append((number, line))
    return result


@cache
def anchors(path):
    used = set()
    for _, line in lines(path):
        heading = re.match(r"^#{1,6}\s+(.+)$", line)
        if heading:
            base = re.sub(r"[^\w -]", "", heading[1].lower()).replace(" ", "-")
            name = base
            index = 0
            while name in used:
                index += 1
                name = f"{base}-{index}"
            used.add(name)
    return used


def table(path, section, columns):
    active = False
    rows = []
    for number, line in lines(path):
        if line.startswith("## "):
            active = line == f"## {section}"
        elif active and line.startswith("|"):
            cells = [value.strip() for value in re.split(r"(?<!\\)\|", line.strip().strip("|"))]
            if len(cells) != len(columns) or not all(cells):
                raise ValueError(f"{path}:{number}: incomplete row in {section}")
            rows.append((number, cells))
    if len(rows) < 3 or rows[0][1] != columns or not all(
        re.fullmatch(r":?-{3,}:?", cell) for cell in rows[1][1]
    ):
        raise ValueError(f"{path}: missing or invalid table in {section}")
    return rows[2:]


def check():
    docs = Path("docs")
    files = [Path("README.md"), Path("CHANGELOG.md"), *sorted(docs.rglob("*.md"))]
    count = 0
    for path in files:
        for number, line in lines(path):
            for raw in re.findall(r"\[[^\]]*\]\(([^)]+)\)", line):
                url = urlsplit(raw)
                if url.scheme or url.netloc:
                    continue
                target = path.parent / unquote(url.path) if url.path else path
                if not target.exists() or (
                    url.fragment and target.suffix == ".md" and unquote(url.fragment) not in anchors(target)
                ):
                    raise ValueError(f"{path}:{number}: unresolved link {raw}")
                count += 1

    plan = docs / "devplan.md"
    steps = Counter(re.findall(r"^#### Шаг (\d+\.\d+)\.", plan.read_text(), re.M))
    if not steps or max(steps.values()) != 1:
        raise ValueError(f"{plan}: missing or duplicate step identifiers")
    profile = docs / "test-profile.md"
    scenarios = table(profile, "Матрица сценариев", ["Сценарий", "Входы", "Условие прохождения", "Шаг"])
    decisions = table(profile, "Реестр проектных решений", ["Решение", "Проверяемое свойство", "Шаги проверки"])
    for number, cells in scenarios + decisions:
        for entry in cells[-1].removeprefix("Только ").split(","):
            bounds = entry.strip().split("-")
            if len(bounds) > 2 or any(step not in steps for step in bounds):
                raise ValueError(f"{profile}:{number}: invalid step reference {entry}")
            if len(bounds) == 2:
                start, end = (tuple(map(int, step.split("."))) for step in bounds)
                if end < start:
                    raise ValueError(f"{profile}:{number}: reversed step range {entry}")

    registered = []
    for number, cells in decisions:
        link = re.fullmatch(r"\[ADR-(\d+)\.[^\]]+\]\((adr/[^)]+\.md)\)", cells[0])
        if not link or not link[2].startswith(f"adr/{link[1]}-"):
            raise ValueError(f"{profile}:{number}: invalid ADR reference {cells[0]}")
        path = docs / link[2]
        if not path.read_text().startswith(f"# ADR-{link[1]}."):
            raise ValueError(f"{path}: title differs from registry")
        registered.append(path)
    if sorted(registered) != sorted((docs / "adr").glob("*.md")):
        raise ValueError(f"{profile}: registry differs from ADR files")
    print(f"{len(files)} documents, {count} local links, {len(steps)} steps, "
          f"{len(scenarios)} scenarios and {len(decisions)} decisions checked")


if __name__ == "__main__":
    try:
        check()
    except (ValueError, OSError) as error:
        raise SystemExit(str(error))
