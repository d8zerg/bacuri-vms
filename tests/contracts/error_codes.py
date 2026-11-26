"""Checks that every error code the server sends in a Problem response is listed in the OpenAPI contract."""

import re
import sys
from pathlib import Path

import yaml

FAILURE = re.compile(r'Failure\{\s*\d+\s*,\s*"([a-z_]+)"')


def main():
    api = yaml.safe_load(Path("contracts/v1/openapi.yaml").read_text(encoding="utf-8"))
    listed = set(api["components"]["schemas"]["Problem"]["properties"]["code"]["enum"])
    sent = {}
    for source in sorted(Path("src").rglob("*.cpp")):
        for code in FAILURE.findall(source.read_text(encoding="utf-8")):
            sent.setdefault(code, source)
    missing = {code: str(source) for code, source in sent.items() if code not in listed}
    if missing:
        sys.exit("Error codes missing from the Problem schema: "
                 + ", ".join(f"{code} ({source})" for code, source in sorted(missing.items())))
    print(f"{len(sent)} server error codes are listed in the contract")


if __name__ == "__main__":
    main()
