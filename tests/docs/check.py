"""Validate the local Markdown links, scenario matrix and decision registry."""

from collections import Counter
from functools import cache
import json
from pathlib import Path
import re
from urllib.parse import unquote, urlsplit

import yaml


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
        used.update(re.findall(r'<a name="([^"]+)"></a>', line))
        heading = re.match(r"^#{1,6}\s+(.+)$", line)
        if heading:
            base = heading_anchor(heading[1])
            name = base
            index = 0
            while name in used:
                index += 1
                name = f"{base}-{index}"
            used.add(name)
    return used


def heading_anchor(title):
    return re.sub(r"[^\w -]", "", title.lower()).replace(" ", "-")


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


def unique_ids(path, rows):
    identifiers = set()
    for number, (key, *_) in rows:
        if not re.fullmatch(r"[a-z][a-z0-9-]*", key) or key in identifiers:
            raise ValueError(f"{path}:{number}: invalid or duplicate ID {key}")
        identifiers.add(key)
    return identifiers


def same_set(label, expected, actual):
    if expected != actual:
        raise ValueError(f"{label}: missing {sorted(expected - actual)}, unexpected {sorted(actual - expected)}")


def check_installations(profile, required):
    setups = table(profile, "Установки", [
        "ID", "Минимальная испытательная конфигурация", "Одновременная нагрузка", "Архив после форматирования"])
    profiles = unique_ids(profile, setups)
    same_set(f"{profile} installations", required, profiles)
    return profiles


def check_devices(profile):
    firmware = json.loads(Path("tests/cameras/qualification.json").read_text())["reference_firmware"]
    devices = table(profile, "Эталонные устройства", [
        "Семейство", "Модель", "Оснащение и что проверить", "Источник"])
    same_set("reference firmware", set(firmware), {row[1] for _, row in devices})
    families = {"Стационарная камера", "Поворотная камера", "Многоканальное устройство"}
    if missing := families - {row[0] for _, row in devices}:
        raise ValueError(f"{profile}: missing device families {sorted(missing)}")
    identities = set()
    for number, (_, model, _, source) in devices:
        if (model in identities or not re.search(r"\d", firmware[model]) or "*" in firmware[model]
                or not re.fullmatch(r"\[[^\]]+\]\(https://[^)]+\)", source)):
            raise ValueError(f"{profile}:{number}: invalid reference device or firmware source")
        identities.add(model)
    return len(identities)


def check_plan_steps(plan, functions):
    headings = {}
    for _, line in lines(plan):
        match = re.fullmatch(r"#### (Шаг (\d+)\.(\d+)\. .+)", line)
        if match:
            headings[heading_anchor(match[1])] = (int(match[2]), int(match[3]))
    intro = plan.read_text().partition("## Функции\n")[2].partition("\n|")[0]
    boundaries = re.findall(r"\[[^\]]+\]\(" + re.escape(plan.name) + r"#([^)]+)\)", intro)
    if len(boundaries) != 2 or any(ref not in headings for ref in boundaries):
        raise ValueError(f"{plan}: expected two linked implementation boundaries")
    first, last = (headings[ref] for ref in boundaries)
    covered = sorted(value for value in headings.values() if first[0] <= value[0] <= last[0])
    if not covered or (first, last) != (covered[0], covered[-1]):
        raise ValueError(f"{plan}: boundaries must cover the complete implementation stages")
    steps = {f"{stage}.{step}" for stage, step in covered}
    same_set(plan, steps, {row[1] for _, row in functions})


def check_release_coverage(register, profiles, *, includes_plan=False):
    columns = ["ID", "Роль", "Профили", "Доступно", "Требуемая операция сервера", "Действие клиента"]
    if includes_plan:
        columns.insert(1, "Шаг")
    functions = table(register, "Функции", columns)
    if includes_plan:
        check_plan_steps(register, functions)
    requirements = unique_ids(register, functions)
    concept = table(register, "Требования концепта", ["Основание", "Функции реестра"])
    concepts = set()
    for number, (source, ids) in concept:
        if not re.fullmatch(r"\[[^\]]+\]\(concept\.md#[^)]+\)", source):
            raise ValueError(f"{register}:{number}: missing concept reference")
        concepts.update(ids.split(", "))
    same_set("concept coverage", requirements, concepts)
    vectors = table(register, "Тестовые векторы", [
        "ID", "Исходные данные", "Действие и ожидаемый результат", "Внесённый отказ и ожидаемый результат"])
    same_set(register, requirements, unique_ids(register, vectors))

    profile_index = columns.index("Профили")
    for number, row in functions:
        assigned = [item.strip() for item in row[profile_index].split(",")]
        if set(assigned) - profiles or len(set(assigned)) != len(assigned):
            raise ValueError(f"{register}:{number}: invalid installation profile")
    return requirements


def check_release_operations(register, requirements):
    operations = table(register, "Исходные операции", ["ID", "Интерфейс", "Операции"])
    surfaces = {}
    for number, (key, interface, names) in operations:
        if key not in requirements:
            raise ValueError(f"{register}:{number}: unknown requirement {key}")
        known = surfaces.setdefault(interface, set())
        for name in names.split(", "):
            if not name or name in known:
                raise ValueError(f"{register}:{number}: duplicate or empty operation {name}")
            known.add(name)
    same_set(register, {"REST", "Core", "Веб", "Страница", "Обслуживание"}, set(surfaces))
    api = yaml.safe_load(Path("contracts/v1/openapi.yaml").read_text())
    operation_ids = {op["operationId"] for item in api["paths"].values() for method, op in item.items()
                     if method in {"get", "put", "post", "delete", "patch", "head", "options", "trace"}}
    same_set("REST", operation_ids, surfaces["REST"])
    for interface, name in (("Core", "main"), ("Обслуживание", "maintenance")):
        source = (Path("src/core") / f"{name}.cpp").read_text()
        same_set(interface, set(re.findall(r'\baction\s*==\s*"([^"]+)"', source)), surfaces[interface])
    router = Path("web/client/src/app/Workspace.tsx").read_text()
    routes = re.findall(r'"(/[\w/-]*)"', router.partition("const routes: RouteObject[]")[2])
    same_set("web workspace", set(routes) - {"/"}, surfaces["Веб"])
    server = Path("src/control/server.cpp").read_text()
    pages = set(re.findall(r'path == "(/[\w-]+/)"', server))
    same_set("standalone web pages", pages, surfaces["Страница"])
    return len(operation_ids)


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
    scenarios = table(profile, "Матрица сценариев", ["Сценарий", "Входы", "Условие прохождения"])
    decisions = table(profile, "Реестр проектных решений", ["Решение", "Проверяемое свойство"])
    scenario_names = [row[0] for _, row in scenarios]
    if len(set(scenario_names)) != len(scenario_names):
        raise ValueError(f"{profile}: duplicate scenario")

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
    register = docs / "release-register.md"
    profile = docs / "release-profile.md"
    profiles = check_installations(profile, {"compact", "site"})
    devices = check_devices(profile)
    requirements = check_release_coverage(register, profiles)
    manager = docs / "devplan-manager.md"
    distributed = check_installations(manager, {"cluster", "federation"})
    manager_requirements = check_release_coverage(manager, {"site"} | distributed, includes_plan=True)
    rest = check_release_operations(register, requirements | manager_requirements)
    print(f"Manager: {len(manager_requirements)} requirements and test vectors")
    print(f"VMS: {len(requirements)} release requirements and test vectors, "
          f"{rest} REST operations, {len(profiles)} profiles and {devices} reference devices checked")
    print(f"{len(files)} documents, {count} local links, {len(steps)} steps, "
          f"{len(scenarios)} scenarios and {len(decisions)} decisions checked")


if __name__ == "__main__":
    try:
        check()
    except (ValueError, OSError) as error:
        raise SystemExit(str(error))
