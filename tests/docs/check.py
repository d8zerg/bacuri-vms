"""Validate the local Markdown links, scenario matrix and decision registry."""

from functools import cache
import json
from pathlib import Path
import os
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


# Section, column and value names of the structured documents. An English source links to its Russian version,
# a document without that link is still written in Russian.
LABELS = {
    "en": {
        "installations": "Installations",
        "installation_columns": ["ID", "Minimum test configuration", "Concurrent load", "Archive after formatting"],
        "devices": "Reference devices",
        "device_columns": ["Family", "Model", "Equipment and what to check", "Source"],
        "families": {"Fixed camera", "PTZ camera", "Multi-channel device"},
        "functions": "Functions",
        "function_columns": ["ID", "Role", "Profiles", "Available", "Required server operation", "Client action"],
        "vectors": "Test vectors",
        "vector_columns": ["ID", "Input data", "Action and expected result", "Injected failure and expected result"],
        "operations": "Source operations",
        "operation_columns": ["ID", "Interface", "Operations"],
        "interfaces": {"REST": "REST", "Core": "Core", "Web": "Web", "Maintenance": "Maintenance"},
        "scenarios": "Scenario matrix",
        "scenario_columns": ["Scenario", "Inputs", "Pass condition"],
        "decisions": "Design decision registry",
        "decision_columns": ["Decision", "Verified property"],
    },
    "ru": {
        "installations": "Установки",
        "installation_columns": ["ID", "Минимальная испытательная конфигурация", "Одновременная нагрузка",
                                 "Архив после форматирования"],
        "devices": "Эталонные устройства",
        "device_columns": ["Семейство", "Модель", "Оснащение и что проверить", "Источник"],
        "families": {"Стационарная камера", "Поворотная камера", "Многоканальное устройство"},
        "functions": "Функции",
        "function_columns": ["ID", "Роль", "Профили", "Доступно", "Требуемая операция сервера", "Действие клиента"],
        "vectors": "Тестовые векторы",
        "vector_columns": ["ID", "Исходные данные", "Действие и ожидаемый результат",
                           "Внесённый отказ и ожидаемый результат"],
        "operations": "Исходные операции",
        "operation_columns": ["ID", "Интерфейс", "Операции"],
        "interfaces": {"REST": "REST", "Core": "Core", "Web": "Веб", "Maintenance": "Обслуживание"},
        "scenarios": "Матрица сценариев",
        "scenario_columns": ["Сценарий", "Входы", "Условие прохождения"],
        "decisions": "Реестр проектных решений",
        "decision_columns": ["Решение", "Проверяемое свойство"],
    },
}


def labels(path):
    return LABELS["en" if "[Русская версия](" in path.read_text() else "ru"]


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
    names = labels(profile)
    setups = table(profile, names["installations"], names["installation_columns"])
    profiles = unique_ids(profile, setups)
    same_set(f"{profile} installations", required, profiles)
    return profiles


def check_devices(profile):
    firmware = json.loads(Path("tests/cameras/qualification.json").read_text())["reference_firmware"]
    names = labels(profile)
    devices = table(profile, names["devices"], names["device_columns"])
    same_set("reference firmware", set(firmware), {row[1] for _, row in devices})
    if missing := names["families"] - {row[0] for _, row in devices}:
        raise ValueError(f"{profile}: missing device families {sorted(missing)}")
    identities = set()
    for number, (_, model, _, source) in devices:
        if (model in identities or not re.search(r"\d", firmware[model]) or "*" in firmware[model]
                or not re.fullmatch(r"\[[^\]]+\]\(https://[^)]+\)", source)):
            raise ValueError(f"{profile}:{number}: invalid reference device or firmware source")
        identities.add(model)
    return len(identities)


def check_release_coverage(register, profiles):
    names = labels(register)
    functions = table(register, names["functions"], names["function_columns"])
    requirements = unique_ids(register, functions)
    vectors = table(register, names["vectors"], names["vector_columns"])
    same_set(register, requirements, unique_ids(register, vectors))

    profile_index = 2
    for number, row in functions:
        assigned = [item.strip() for item in row[profile_index].split(",")]
        if set(assigned) - profiles or len(set(assigned)) != len(assigned):
            raise ValueError(f"{register}:{number}: invalid installation profile")
    return requirements


def check_release_operations(register, requirements):
    vocabulary = labels(register)
    kinds = vocabulary["interfaces"]
    operations = table(register, vocabulary["operations"], vocabulary["operation_columns"])
    surfaces = {}
    for number, (key, interface, names) in operations:
        if key not in requirements:
            raise ValueError(f"{register}:{number}: unknown requirement {key}")
        known = surfaces.setdefault(interface, set())
        for name in names.split(", "):
            if not name or name in known:
                raise ValueError(f"{register}:{number}: duplicate or empty operation {name}")
            known.add(name)
    same_set(register, set(kinds.values()), set(surfaces))
    api = yaml.safe_load(Path("contracts/v1/openapi.yaml").read_text())
    operation_ids = {op["operationId"] for item in api["paths"].values() for method, op in item.items()
                     if method in {"get", "put", "post", "delete", "patch", "head", "options", "trace"}}
    same_set("REST", operation_ids, surfaces[kinds["REST"]])
    for key, name in (("Core", "main"), ("Maintenance", "maintenance")):
        source = (Path("src/core") / f"{name}.cpp").read_text()
        same_set(key, set(re.findall(r'\baction\s*==\s*"([^"]+)"', source)), surfaces[kinds[key]])
    router = Path("web/client/src/app/Workspace.tsx").read_text()
    routes = re.findall(r'"(/[\w/-]*)"', router.partition("const routes: RouteObject[]")[2])
    same_set("web workspace", set(routes) - {"/"}, surfaces[kinds["Web"]])
    return len(operation_ids)


def check_translations(sources):
    # The English document is the source. The Russian version of a root document has the .ru suffix,
    # the Russian version of docs/NAME.md is docs/ru/NAME.md. Each version links to the other.
    for source in sources:
        russian = source.with_suffix(".ru.md") if source.parent == Path(".") else Path("docs/ru") / source.relative_to("docs")
        if not russian.is_file():
            raise ValueError(f"{source}: missing Russian version {russian}")
        for document, other in ((source, russian), (russian, source)):
            link = os.path.relpath(other, document.parent)
            if f"]({link})" not in document.read_text():
                raise ValueError(f"{document}: missing link to {link}")
    # Every Russian document under docs/ru/ has its English source.
    for russian in Path("docs/ru").rglob("*.md"):
        if not (Path("docs") / russian.relative_to("docs/ru")).is_file():
            raise ValueError(f"{russian}: missing English version")


def check():
    docs = Path("docs")
    # The concept and the development plans are working notes of the author outside the published documentation.
    notes = {"concept.md", "devplan.md", "devplan-desktop.md", "devplan-manager.md", "review-plan.md"}
    files = [path for path in [*sorted(Path(".").glob("*.md")), *sorted(docs.rglob("*.md"))]
             if not (path.parent == docs and path.name in notes)]
    # Every document with a Russian version under docs/ru/ is checked as a pair.
    check_translations([Path("README.md"),
                        *(docs / russian.relative_to(docs / "ru") for russian in sorted((docs / "ru").rglob("*.md")))])
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

    profile = docs / "test-profile.md"
    names = labels(profile)
    scenarios = table(profile, names["scenarios"], names["scenario_columns"])
    decisions = table(profile, names["decisions"], names["decision_columns"])
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
    # Maps and maintenance have source operations without a release function of their own.
    rest = check_release_operations(register, requirements | {"maps", "operations"})
    print(f"VMS: {len(requirements)} release requirements and test vectors, "
          f"{rest} REST operations, {len(profiles)} profiles and {devices} reference devices checked")
    print(f"{len(files)} documents, {count} local links, "
          f"{len(scenarios)} scenarios and {len(decisions)} decisions checked")


if __name__ == "__main__":
    try:
        check()
    except (ValueError, OSError) as error:
        raise SystemExit(str(error))
