#!/usr/bin/env python3
"""Check the parity register of the desktop client.

The register must cover every public operation, every web client section and every
console action of the requirement register, and it must name the contract revision
it was reconciled with. With --accept the current contract fingerprints are written
into the register after a reconciliation.
"""

from __future__ import annotations

import argparse
import hashlib
import re
import sys
from collections import Counter
from pathlib import Path

import yaml

ROOT = Path(__file__).resolve().parent.parent
REGISTER = ROOT / "desktop/parity.yaml"
OPENAPI = ROOT / "contracts/v1/openapi.yaml"
REQUIREMENTS = ROOT / "docs/release-register.md"
ROUTES = ROOT / "web/client/src/app/Workspace.tsx"

SCENARIO_FIELDS = (
    "function", "area", "role", "precondition", "action", "result", "failure",
    "steps", "platform", "operations", "test", "material",
)
ROLES = {"operator", "administrator", "service"}
INSTALLS = {"compact", "site"}
STATUSES = {"open", "ready", "accepted"}
STEP = re.compile(r"^\d+\.\d+$")
PLATFORM_STEP = re.compile(r"^(?:(?:analytics|manager) )?\d+\.\d+$")


def fingerprint(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def openapi_operations() -> tuple[str, set[str]]:
    spec = yaml.safe_load(OPENAPI.read_text(encoding="utf-8"))
    operations = {
        operation["operationId"]
        for methods in spec["paths"].values()
        for operation in methods.values()
        if isinstance(operation, dict) and "operationId" in operation
    }
    return str(spec["info"]["version"]), operations


def table_rows(text: str, heading: str) -> list[list[str]]:
    section = text.split(heading, 1)[1]
    section = re.split(r"\n## ", section, maxsplit=1)[0]
    rows = []
    for line in section.splitlines():
        if not line.startswith("| ") or set(line.replace("|", "").strip()) <= {"-", " "}:
            continue
        rows.append([cell.strip() for cell in line.strip().strip("|").split("|")])
    return rows[1:]


def requirement_register() -> tuple[set[str], dict[str, dict[str, set[str]]]]:
    text = REQUIREMENTS.read_text(encoding="utf-8")
    functions = {row[0] for row in table_rows(text, "\n## Functions\n")}
    sources: dict[str, dict[str, set[str]]] = {}
    for function, interface, items in table_rows(text, "\n## Source operations\n"):
        names = {item.strip() for item in items.split(",") if item.strip()}
        sources.setdefault(interface, {}).setdefault(function, set()).update(names)
    functions.update(function for interfaces in sources.values() for function in interfaces)
    return functions, sources


def web_routes() -> set[str]:
    text = ROUTES.read_text(encoding="utf-8")
    sections = text.split("const sections = [", 1)[1].split("] satisfies", 1)[0]
    pages = {"/" + p.stem for p in (ROOT / "web/client").glob("*.html") if p.stem != "index"}
    return set(re.findall(r'path: "(/[a-z-]+)"', sections)) | pages


def accept(register_text: str, contracts: dict[str, str]) -> str:
    for name in contracts:
        pattern = re.compile(rf"^(\s+{re.escape(name)}: )[0-9a-f]{{64}}$", re.MULTILINE)
        register_text, count = pattern.subn(lambda match: match.group(1) + fingerprint(ROOT / name), register_text)
        if count != 1:
            raise SystemExit(f"Cannot update the fingerprint of {name}")
    return register_text


def check(register: dict) -> list[str]:
    errors: list[str] = []
    version, operations = openapi_operations()
    functions, sources = requirement_register()
    routes = web_routes()

    reference = register["reference"]
    if str(reference["api_version"]) != version:
        errors.append(f"reference: API version {reference['api_version']} differs from the contract version {version}")
    for name, digest in reference["contracts"].items():
        actual = fingerprint(ROOT / name)
        if digest != actual:
            errors.append(f"reference: {name} changed since the reconciliation, review the register and run with --accept")

    declared = register["functions"]
    for name in sorted(functions - declared.keys()):
        errors.append(f"functions: {name} of the requirement register has no owner and criterion")
    for name in sorted(declared.keys() - functions):
        errors.append(f"functions: {name} is not a function of the requirement register")
    for name, function in declared.items():
        if not STEP.match(str(function.get("owner", ""))) or not function.get("criterion"):
            errors.append(f"functions: {name} needs an owner step and a parity criterion")

    defaults = register.get("defaults", {})
    areas = register["areas"]
    requests = register["contract_requests"]
    scenarios = register["scenarios"]
    used: Counter[str] = Counter()
    covered_routes: set[str] = set()
    covered_functions: set[str] = set()
    for name, scenario in scenarios.items():
        where = f"scenario {name}"
        for field in SCENARIO_FIELDS:
            if field not in scenario:
                errors.append(f"{where}: missing {field}")
        if scenario.get("function") not in declared:
            errors.append(f"{where}: unknown function {scenario.get('function')}")
        covered_functions.add(scenario.get("function"))
        if scenario.get("area") not in areas:
            errors.append(f"{where}: unknown area {scenario.get('area')}")
        if scenario.get("role") not in ROLES:
            errors.append(f"{where}: unknown role {scenario.get('role')}")
        for step in scenario.get("steps", []):
            if not STEP.match(str(step)):
                errors.append(f"{where}: client step {step} is not a step number")
        if not scenario.get("steps"):
            errors.append(f"{where}: no client step")
        for step in scenario.get("platform", []):
            if not PLATFORM_STEP.match(str(step)):
                errors.append(f"{where}: platform step {step} is not a step number")
        for operation in scenario.get("operations", []):
            if operation not in operations:
                errors.append(f"{where}: unknown operation {operation}")
            used[operation] += 1
        route = scenario.get("web")
        if route is not None:
            if route not in routes:
                errors.append(f"{where}: unknown web route {route}")
            covered_routes.add(route)
        if scenario.get("os", defaults.get("os")) != ["linux"]:
            errors.append(f"{where}: the current desktop release requires Linux as its only target")
        if not set(scenario.get("install", defaults.get("install", []))) <= INSTALLS:
            errors.append(f"{where}: unknown installation type")
        if scenario.get("status", defaults.get("status")) not in STATUSES:
            errors.append(f"{where}: unknown status")

    for operation in sorted(operations - used.keys()):
        errors.append(f"operations: {operation} is not used by any scenario")
    for function, names in sources.get("REST", {}).items():
        for operation in sorted(names - operations):
            errors.append(f"requirement register: {function} names {operation}, which is not in the contract")
    for route in sorted(routes - covered_routes):
        errors.append(f"web: section {route} has no scenario")
    for function in sorted(declared.keys() - covered_functions):
        errors.append(f"functions: {function} has no scenario")

    console = register["console"]
    for interface in ("Core", "Maintenance"):
        for function, names in sources.get(interface, {}).items():
            for action in sorted(names):
                target = console["equivalents"].get(action)
                if target is None and action not in console["node_procedures"]:
                    errors.append(f"console: {action} of {function} has no API equivalent and is not a node procedure")
                elif target is not None and target not in used:
                    errors.append(f"console: {action} maps to {target}, which no scenario uses")

    for name, extension in register["extensions"].items():
        for scenario in extension["scenarios"]:
            if scenario not in scenarios:
                errors.append(f"extension {name}: unknown scenario {scenario}")
        for request in extension["requires"]:
            if request not in requests:
                errors.append(f"extension {name}: unknown contract request {request}")
        for field in ("capability", "older_server"):
            if not extension.get(field):
                errors.append(f"extension {name}: missing {field}")
    for name, request in requests.items():
        if not request.get("need") or not request.get("platform"):
            errors.append(f"contract request {name}: needs a description and platform steps")
    return errors


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--accept", action="store_true", help="record the current contract fingerprints")
    arguments = parser.parse_args()

    text = REGISTER.read_text(encoding="utf-8")
    register = yaml.safe_load(text)
    if arguments.accept:
        text = accept(text, register["reference"]["contracts"])
        REGISTER.write_text(text, encoding="utf-8")
        register = yaml.safe_load(text)

    errors = check(register)
    for error in errors:
        print(error, file=sys.stderr)
    if errors:
        return 1
    scenarios = register["scenarios"]
    statuses = Counter(scenario.get("status", register["defaults"]["status"]) for scenario in scenarios.values())
    print(
        f"Desktop parity: {len(register['functions'])} functions, {len(scenarios)} scenarios, "
        f"{len(register['extensions'])} extensions, {len(register['contract_requests'])} contract requests, "
        + ", ".join(f"{count} {status}" for status, count in sorted(statuses.items()))
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
