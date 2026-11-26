"""Checks the responses of the Storybook fixture against the OpenAPI contract.

Usage: fixtures.py RESPONSES.json, where the file comes from web/client/tests/fixture-responses.mjs.
"""

import json
import sys
from pathlib import Path

import yaml
from jsonschema import Draft202012Validator, FormatChecker
from referencing import Registry
from referencing.jsonschema import DRAFT202012

API_URI = "https://bacuri.invalid/contracts/v1/openapi.yaml"


def response_schemas(api):
    schemas = {}
    for path, item in api["paths"].items():
        escaped = path.replace("~", "~0").replace("/", "~1")
        for method, operation in item.items():
            if isinstance(operation, dict) and "operationId" in operation and "200" in operation["responses"]:
                schemas[operation["operationId"]] = (
                    f"{API_URI}#/paths/{escaped}/{method}/responses/200/content/application~1json/schema")
    return schemas


def main(responses_path):
    api = yaml.safe_load(Path("contracts/v1/openapi.yaml").read_text(encoding="utf-8"))
    registry = Registry().with_resource(API_URI, DRAFT202012.create_resource(api))
    schemas = response_schemas(api)
    errors = []
    checked = 0
    for response in json.loads(Path(responses_path).read_text(encoding="utf-8")):
        if "failed" in response:
            continue
        validator = Draft202012Validator({"$ref": schemas[response["operation"]]}, registry=registry,
                                         format_checker=FormatChecker())
        checked += 1
        for error in validator.iter_errors(response["value"]):
            errors.append(f"{response['condition']} {response['operation']} {list(error.absolute_path)}: "
                          f"{error.message[:160]}")
    if errors:
        unique = sorted(set(errors))
        sys.exit(f"{len(unique)} fixture responses differ from the contract:\n" + "\n".join(unique[:80]))
    print(f"{checked} fixture responses match the contract")


if __name__ == "__main__":
    main(sys.argv[1])
