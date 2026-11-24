"""Validate qualification inputs before running the ONVIF series."""
import csv
import hashlib
import json
import math
from pathlib import Path

from series import FEATURES

PROFILE = Path("tests/cameras/qualification.json")
MATRIX = Path("docs/asvs.csv")


def need(condition, message):
    if not condition:
        raise ValueError(message)


def fields(row, names):
    need(isinstance(row, dict) and set(row) == set(names.split()), f"expected fields: {names}")


def positive(row, names):
    for name in names.split():
        n = row[name]
        need(type(n) in (int, float) and math.isfinite(n) and n > 0, f"invalid {name}")


def validate(p):
    fields(p, "node load archive latency recovery role_minutes emulator reference_firmware hardware")
    fields(p["node"], "cpu logical_cpus ram_gib os filesystem network_mbps")
    positive(p["node"], "logical_cpus ram_gib network_mbps")
    for key in ("cpu", "os", "filesystem"):
        need(isinstance(p["node"][key], str) and p["node"][key].strip(), f"missing {key}")
    load = p["load"]
    counts = "cameras live_viewers archive_viewers gop_seconds events_per_second warmup_minutes measure_minutes samples_per_metric"
    fields(load, counts + " codec main_mbps sub_mbps")
    positive(load, counts + " sub_mbps")
    for key in ("cameras", "live_viewers", "archive_viewers", "samples_per_metric"):
        need(type(load[key]) is int, f"expected an integer for {key}")
    need(load["codec"] == "H264" and load["main_mbps"] == [2, 4], "unsupported media load")
    archive = p["archive"]
    fields(archive, "days volume_tb reserved_percent")
    positive(archive, "days volume_tb reserved_percent")
    payload = load["cameras"] * max(load["main_mbps"]) * 1e6 * archive["days"] * 86400 / 8
    need(payload <= archive["volume_tb"] * 1e12 * (1 - archive["reserved_percent"] / 100), "archive too small")
    budgets = "live_p95_ms alarm_p95_ms cold_search_p95_ms warm_search_p95_ms archive_frame_p95_ms seek_error_ms revoke_max_ms"
    fields(p["latency"], budgets + " additional_gaps")
    positive(p["latency"], budgets)
    need(type(p["latency"]["additional_gaps"]) is int and p["latency"]["additional_gaps"] == 0, "extra gaps forbidden")
    fields(p["recovery"], "configuration completed_archive new_recording")
    for name, values in p["recovery"].items():
        fields(values, "rpo_s rto_s")
        positive(values, "rto_s")
        if name == "completed_archive":
            need(type(values["rpo_s"]) is int and values["rpo_s"] == 0, "completed archive loss forbidden")
        else:
            positive(values, "rpo_s")
    roles = "first_recording alarm correct_address explain_gap replace investigate install upgrade restore"
    fields(p["role_minutes"], roles)
    positive(p["role_minutes"], roles)
    identities = set()
    need(isinstance(p["hardware"], list), "hardware inventory must be an array")
    for camera in [p["emulator"], *p["hardware"]]:
        fields(camera, "manufacturer model firmware control media trust legacy_exception features")
        identity = tuple(camera[field] for field in ("manufacturer", "model", "firmware"))
        need(all(isinstance(s, str) and s.strip() for s in (*identity, camera["trust"])), "missing camera identity or trust")
        need(identity not in identities, "duplicate model and firmware")
        identities.add(identity)
        need(camera["control"] == "https" and camera["media"] == "rtsps" and camera["legacy_exception"] is False,
             "the current profile requires encrypted control and interleaved media")
        features = camera["features"]
        need(isinstance(features, list) and all(isinstance(f, str) for f in features)
             and len(features) == len(set(features)) and
             set(FEATURES) - {"ptz", "motion"} <= set(features) <= set(FEATURES), "invalid feature list")
    need(set(p["emulator"]["features"]) == set(FEATURES), "incomplete emulator")


def validate_asvs():
    with MATRIX.open() as file:
        reader = csv.DictReader(file)
        need(reader.fieldnames == "id level control scope rationale analysis_or_test result".split(), "invalid ASVS columns")
        rows = list(reader)
    keys = []
    for row in rows:
        need(all(isinstance(v, str) and v.strip() for v in row.values()), "incomplete ASVS row")
        need(row["scope"] in ("applicable", "excluded"), "invalid ASVS applicability")
        need(row["result"] in (("pending", "tested") if row["scope"] == "applicable" else ("scope-review",)), "invalid ASVS evidence status")
        need(Path(row["analysis_or_test"]).is_file(), "missing evidence target")
        keys.append(f"{row['id']} {row['level']}\n")
    # SHA-256 of the sorted requirement identifiers and levels in the OWASP v5.0.0 CSV.
    need(hashlib.sha256("".join(sorted(keys)).encode()).hexdigest() ==
         "07a860db1100480274b8aa570b5f20462cf4a58c6f52e5a4239efa4d7bd630bd", "ASVS identifiers or levels differ from upstream")


def load_profile():
    p = json.loads(PROFILE.read_bytes())
    validate(p)
    validate_asvs()
    return p
