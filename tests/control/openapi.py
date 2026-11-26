"""Checks the HTTP responses of the end-to-end scenarios against the OpenAPI contract."""

import re
from pathlib import Path

import yaml
from jsonschema import Draft202012Validator, FormatChecker
from referencing import Registry
from referencing.jsonschema import DRAFT202012

API_URI = "https://bacuri.invalid/contracts/v1/openapi.yaml"
SPECIFICATION = Path(__file__).resolve().parents[2] / "contracts/v1/openapi.yaml"
PARAMETER = re.compile(r"\{[^}]+\}")


def pointer(*parts):
    return "/".join(part.replace("~", "~0").replace("/", "~1") for part in parts)


class Contract:
    def __init__(self, path=SPECIFICATION):
        self.api = yaml.safe_load(path.read_text(encoding="utf-8"))
        catalog = yaml.safe_load(path.with_name("errors.yaml").read_text(encoding="utf-8"))
        self.status_by_code = {code: entry["http"] for code, entry in catalog["errors"].items()
                               if entry.get("surface", "problem") == "problem"}
        self.base = self.api["servers"][0]["url"].rstrip("/")
        self.registry = Registry().with_resource(API_URI, DRAFT202012.create_resource(self.api))
        self.resolver = self.registry.resolver(API_URI)
        self.templates = [
            (re.compile(PARAMETER.sub("[^/]+", re.escape(template).replace(r"\{", "{").replace(r"\}", "}")) + "$"),
             template)
            for template in self.api["paths"]
        ]
        self.validators = {}

    def template(self, path):
        matches = [template for pattern, template in self.templates if pattern.match(path)]
        if not matches:
            return None
        # A literal segment is more specific than a parameter, as in /alerts/stream against /alerts/{alertId}.
        return max(matches, key=lambda template: len(PARAMETER.sub("", template)))

    def check(self, method, path, status, content_type, body):
        """Raises AssertionError when the response is not described by the contract."""
        path = path.split("?", 1)[0]
        if not path.startswith(self.base + "/"):
            raise AssertionError(f"{method} {path}: outside the API base {self.base}")
        template = self.template(path[len(self.base):])
        if template is None:
            raise AssertionError(f"{method} {path}: path is not in the contract")
        operation = self.api["paths"][template].get(method.lower())
        if operation is None:
            raise AssertionError(f"{method} {template}: method is not in the contract")
        response_key = str(status) if str(status) in operation["responses"] else "default"
        if response_key not in operation["responses"]:
            raise AssertionError(f"{method} {template}: status {status} is not in the contract")
        location = "#/" + pointer("paths", template, method.lower(), "responses", response_key)
        response = self.resolver.lookup(location).contents
        if "$ref" in response:
            location = response["$ref"]
            response = self.resolver.lookup(location).contents
        content = response.get("content")
        if not content:
            if body is not None:
                raise AssertionError(f"{method} {template} {status}: the contract declares no body")
            return
        media = (content_type or "").split(";", 1)[0].strip()
        if media not in content:
            raise AssertionError(f"{method} {template} {status}: media type {media!r} is not in the contract")
        schema = location + "/" + pointer("content", media, "schema")
        if schema not in self.validators:
            self.validators[schema] = Draft202012Validator(
                {"$ref": API_URI + schema}, registry=self.registry, format_checker=FormatChecker())
        errors = sorted(self.validators[schema].iter_errors(body), key=lambda error: list(error.absolute_path))
        if errors:
            details = "; ".join(f"{list(error.absolute_path)}: {error.message}" for error in errors[:5])
            raise AssertionError(f"{method} {template} {status}: response differs from the contract: {details}")
        if media == "application/problem+json" and (body["status"] != status or
                self.status_by_code.get(body["code"]) != status):
            raise AssertionError(f"{method} {template}: HTTP {status} disagrees with Problem {body['code']}")
