from copy import deepcopy
import json
from pathlib import Path

from jsonschema import Draft202012Validator


schema = json.loads(Path("storage/archive/block-manifest-v1.schema.json").read_text())
fixture = json.loads(Path("tests/schema/block-manifest-v1.json").read_text())
Draft202012Validator.check_schema(schema)
validator = Draft202012Validator(schema)
validator.validate(fixture)

reception = deepcopy(fixture)
reception["time"].update(basis="reception", clock_epoch_id=None, uncertainty_us=None)
reception["begin_utc_us"] = reception["time"]["received_at_utc_us"]
validator.validate(reception)

incorrect_version = deepcopy(fixture)
incorrect_version["manifest_version"] = 2
incomplete_time = deepcopy(fixture)
incomplete_time["time"]["uncertainty_us"] = None
unbounded_time = deepcopy(reception)
unbounded_time["end_utc_us"] = 9223372036854775808
invalid_media = deepcopy(fixture)
invalid_media["media"]["file"] = "../media.mp4"
invalid_track = deepcopy(fixture)
invalid_track["media"]["tracks"][0]["time_scale"] = 0

for invalid in (incorrect_version, incomplete_time, unbounded_time, invalid_media, invalid_track):
    if validator.is_valid(invalid):
        raise RuntimeError("An invalid archive block manifest passed validation")

print("Archive block manifest v1 validation passed")
