import base64
import sys
from pathlib import Path

import yaml
from google.protobuf import json_format, symbol_database
from jsonschema import Draft202012Validator, FormatChecker
from openapi_spec_validator import validate
from referencing import Registry
from referencing.jsonschema import DRAFT202012

API_URI = "https://bacuri.invalid/contracts/v1/openapi.yaml"
ALERTS_URI = "https://bacuri.invalid/contracts/v1/alerts.schema.yaml"
FIXTURES = Path("tests/contracts")


def read_yaml(path):
    return yaml.safe_load(Path(path).read_text(encoding="utf-8"))


def validator(registry, ref):
    return Draft202012Validator(
        {"$ref": ref}, registry=registry, format_checker=FormatChecker()
    )


def check_rest(api, registry):
    resolver = registry.resolver(API_URI)
    operations = {}
    for path, path_item in api["paths"].items():
        escaped_path = path.replace("~", "~0").replace("/", "~1")
        for method, operation in path_item.items():
            if isinstance(operation, dict) and "operationId" in operation:
                operations[operation["operationId"]] = f"#/paths/{escaped_path}/{method}"

    def payload_validator(pointer):
        definition = resolver.lookup(pointer).contents
        pointer = definition.get("$ref", pointer)
        content = resolver.lookup(pointer).contents["content"]
        media_type = next(kind for kind in ("application/json", "application/problem+json") if kind in content)
        escaped_media_type = media_type.replace("/", "~1")
        return validator(registry, f"{API_URI}{pointer}/content/{escaped_media_type}/schema")

    for name, fixture in read_yaml(FIXTURES / "rest.yaml").items():
        operation = operations[fixture["operation"]]
        status = fixture["status"]
        try:
            if "request" in fixture:
                payload_validator(f"{operation}/requestBody").validate(fixture["request"])
            if status == 204:
                resource = resolver.lookup(f"{operation}/responses/{status}").contents
                assert "content" not in resource and "response" not in fixture
                continue
            response = fixture["response"]
            payload_validator(f"{operation}/responses/{status}").validate(response)
            if status >= 400 and response["status"] != status:
                raise ValueError("problem status does not match the HTTP status")
        except Exception as error:
            raise ValueError(f"REST example {name}: {error}") from error


def check_alerts(registry):
    directions = {"client": "Subscribe", "server": "ServerMessage"}
    for name, fixture in read_yaml(FIXTURES / "alerts.yaml").items():
        schema = directions[fixture["direction"]]
        try:
            validator(registry, f"{ALERTS_URI}#/$defs/{schema}").validate(fixture["message"])
        except Exception as error:
            raise ValueError(f"WebSocket example {name}: {error}") from error


def check_rejected_examples(registry):
    rest = read_yaml(FIXTURES / "rest.yaml")
    timeline = rest["archive_timeline"]["response"]["items"]
    exact_time, _, estimated_time, *_ = timeline
    alerts = read_yaml(FIXTURES / "alerts.yaml")
    interval = rest["open_archive_session"]["request"]["interval"]
    invalid = [
        (f"{API_URI}#/components/schemas/ArchiveEntry", {**estimated_time, "uncertainty_us": "0"}),
        (f"{API_URI}#/components/schemas/ArchiveEntry", {
            field: value for field, value in exact_time.items() if field != "uncertainty_us"
        }),
        (f"{API_URI}#/components/schemas/MediaRequest", {"mode": "live", "interval": interval}),
        (f"{API_URI}#/components/schemas/Camera", {**rest["register_camera"]["response"], "credentials": None}),
        (f"{API_URI}#/components/schemas/CameraStreamUrl", "rtsp://user:password@camera.invalid/main"),
        (f"{ALERTS_URI}#/$defs/ServerMessage", alerts["subscribe"]["message"]),
        (f"{ALERTS_URI}#/$defs/Subscribe", {"type": "subscribe"}),
    ]
    for schema, payload in invalid:
        if validator(registry, schema).is_valid(payload):
            raise ValueError(f"{schema} accepted malformed payload: {payload}")


def check_grpc(generated):
    sys.path.insert(0, generated)
    import control_pb2

    methods = {
        f"{service.name}.{method.name}": method
        for service in control_pb2.DESCRIPTOR.services_by_name.values()
        for method in service.methods
    }
    symbols = symbol_database.Default()
    for name, fixture in read_yaml(FIXTURES / "grpc.yaml").items():
        method = methods.pop(fixture["rpc"])
        for side, message in (("request", method.input_type), ("response", method.output_type)):
            fields = {
                field: base64.b64encode(Path(value["base64_file"]).read_bytes()).decode("ascii")
                if isinstance(value, dict) and "base64_file" in value else value
                for field, value in fixture[side].items()
            }
            try:
                json_format.ParseDict(fields, symbols.GetSymbol(message.full_name)())
            except json_format.ParseError as error:
                raise ValueError(f"gRPC example {name} {side}: {error}") from error
    if methods:
        raise ValueError(f"Missing gRPC examples: {', '.join(methods)}")


def main():
    api = read_yaml("contracts/v1/openapi.yaml")
    alerts = read_yaml("contracts/v1/alerts.schema.yaml")
    validate(api)
    Draft202012Validator.check_schema(alerts)
    registry = Registry().with_resources([
        (API_URI, DRAFT202012.create_resource(api)),
        (ALERTS_URI, DRAFT202012.create_resource(alerts)),
    ])
    check_rest(api, registry)
    check_alerts(registry)
    check_rejected_examples(registry)
    check_grpc(sys.argv[1])
    print("Validated OpenAPI, WebSocket and Protobuf contracts and examples")


if __name__ == "__main__":
    main()
