"""Check storage ownership and public includes in the modular Core."""
import argparse
import ast
import re
import tempfile
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

# Objects that the current executable can access. Retired projections still exist in older databases.
OBJECTS = {
    "inventory": """sites node_acceleration observation_points devices channels stream_profiles point_bindings configuration_versions
        configuration_progress camera_registry recorder_delivery recorder_attempt registration_requests configuration_audit
        camera_templates camera_batch_items camera_configuration_state camera_state camera_transport_audit record_camera_transport_policy ptz_commands ptz_telemetry digital_input_states camera_talk_sessions""",
    "access": """local_users site_roles point_grants browser_sessions login_attempts export_grants webauthn_credentials
        webauthn_enrollment webauthn_challenges credential_audit resource_groups group_points scoped_grants effective_grants privacy_policies identity_providers
        external_identities oidc_requests desktop_codes policy_versions access_audit can_use audit_policy_change invalidate_role_sessions audit_credential_binding""",
    "recordings": """blocks motion_indexes enqueue_index archive_holds held_blocks protect_new_block hold_until gaps retired_through catalog_intervals timeline_intervals retained_blocks archive_retention archive_recovery
        retention_now_us retention_cutoff advance_cutoff playback_parts recording_policies recording_windows point_retention point_cutoff reap_expired""",
    "investigations": "investigations",
    "media_sessions": "media_sessions monitor_layouts",
    "events": """video_rules video_matches observations feed_heads alerts alert_outbox alert_details access_versions touch_alert_scope
        alert_delegations alert_audit protect_alert_audit""",
    "maps": "map_regions map_tiles floor_plans point_placements map_jobs map_sources map_catalog_settings",
    "database": "schema_migrations",
    "retired": "visible_points visible_plans media_rights alert_audit_details",
}
OWNER = {name: owner for owner, names in OBJECTS.items() for name in names.split()}

PUBLIC = {
    "application/control_api/api.hpp",
    "application/control_api/http.hpp",
    "application/control_api/playback_time.hpp",
    "application/control_api/support.hpp",
    "application/export/export.hpp",
    "application/recovery/recovery.hpp",
    "modules/access/admin.hpp",
    "modules/access/configuration.hpp",
    "modules/access/enrollment.hpp",
    "modules/access/oidc.hpp",
    "modules/access/operations.hpp",
    "modules/access/restore.hpp",
    "modules/access/session.hpp",
    "modules/events/alert_work.hpp",
    "modules/events/alerts.hpp",
    "modules/events/configuration.hpp",
    "modules/events/observations.hpp",
    "modules/events/operations.hpp",
    "modules/events/video_rules.hpp",
    "modules/inventory/camera_events.hpp",
    "modules/inventory/configuration.hpp",
    "modules/inventory/delivery.hpp",
    "modules/inventory/operations.hpp",
    "modules/inventory/registry.hpp",
    "modules/inventory/restore.hpp",
    "modules/inventory/telemetry.hpp",
    "modules/investigations/cases.hpp",
    "modules/maps/configuration.hpp",
    "modules/maps/operations.hpp",
    "modules/maps/scene.hpp",
    "modules/media_sessions/media_authority.hpp",
    "modules/media_sessions/operations.hpp",
    "modules/media_sessions/playback.hpp",
    "modules/recordings/archive_catalog.hpp",
    "modules/recordings/archive_recovery.hpp",
    "modules/recordings/configuration.hpp",
    "modules/recordings/holds.hpp",
    "modules/recordings/operations.hpp",
    "modules/recordings/policies.hpp",
    "modules/recordings/recorder.hpp",
    "modules/recordings/search.hpp",
    "modules/recordings/status.hpp",
}

# Shared representations used by HTTP, storage and message adapters.
SHARED_CODECS = {
    "application/control_api/http_json.hpp",
    "application/control_api/playback_time_json.hpp",
    "modules/events/alert_json.hpp",
    "modules/events/video_json.hpp",
    "modules/inventory/orientation_json.hpp",
    "modules/investigations/case_json.hpp",
}

def module(file):
    if file.parts[:2] == ("src", "modules") and len(file.parts) >= 4:
        return file.parts[2]
    if file.parts[:3] == ("src", "storage", "postgresql"):
        return "database"
    return None


CPP_TOKEN = re.compile(r'''//[^\n]*|/\*.*?\*/|R"(?P<tag>\w*)\((?P<raw>.*?)\)(?P=tag)"|"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*' ''', re.S | re.X)
def literals(code):
    """Join adjacent C++ literals across whitespace and comments."""
    previous = 0
    pieces = []
    for token in CPP_TOKEN.finditer(code):
        if code[previous:token.start()].strip():
            if pieces:
                yield "".join(pieces)
            pieces = []
        previous = token.end()
        value = token[0]
        if token["raw"] is not None:
            pieces.append(token["raw"])
        elif value.startswith('"'):
            pieces.append(ast.literal_eval(value))
    if pieces:
        yield "".join(pieces)

SQL_TOKEN = re.compile(r'''--[^\n]*|/\*.*?\*/|'(?:''|[^'])*'|"(?:""|[^"])*"|[\w$]+|[^\s]''', re.S)
MODULE_SCHEMAS = {
    "access": "bacuri_access", "inventory": "bacuri_inventory", "events": "bacuri_events",
    "recordings": "bacuri_archive", "media_sessions": "bacuri_media_sessions",
    "maps": "bacuri_maps", "investigations": "bacuri_investigations", "database": "bacuri_meta",
}
SCHEMAS = set(MODULE_SCHEMAS.values())
# Previous packages and their snapshots use the original physical names during migration and rollback.
LEGACY_NAMES = {
    Path("src/storage/postgresql/migrations.hpp"): {"schema_migrations"},
    Path("src/storage/postgresql/migrations.cpp"): {"schema_migrations"},
    Path("src/modules/recordings/archive_recovery.cpp"): {"archive_recovery"},
    Path("src/modules/media_sessions/operations.cpp"): {"media_sessions"},
}
FUNCTION_NAMES = {
    "can_use", "advance_cutoff", "retention_cutoff", "retention_now_us", "audit_credential_binding",
    "invalidate_role_sessions", "record_camera_transport_policy", "touch_alert_scope", "protect_alert_audit",
}
def sql_references(query):
    tokens = [t.lower().strip('"') for t in SQL_TOKEN.findall(query) if not t.startswith(("--", "/*"))]
    using_from = set()
    depth = 0
    for i, name in enumerate(tokens):
        prev = tokens[i - 1] if i else ""
        if name == "(":
            depth += 1
        elif name == ")":
            using_from.discard(depth)
            depth -= 1
        elif name in {"from", "using"}:
            using_from.add(depth)
        elif name in {"where", "group", "order", "limit", "set", "on", "returning", ";"}:
            using_from.discard(depth)
        if name not in OWNER:
            continue
        schema = tokens[i - 2] if i >= 2 and prev == "." else None
        keyword = tokens[i - 3] if schema and i >= 3 else prev
        context = keyword in {"from", "join", "update", "into", "using", "truncate", "table", "only"}
        call = name in FUNCTION_NAMES and tokens[i+1:i+2] == ["("]
        if schema in SCHEMAS or context or call or (keyword == "," and depth in using_from):
            yield schema, name

def violations(owner, query):
    return {name for _, name in sql_references(query) if OWNER[name] != owner}

def placement_errors(file, query):
    errors = []
    for schema, name in sql_references(query):
        expected = MODULE_SCHEMAS.get(OWNER[name])
        if schema == expected:
            continue
        if schema == "bacuri_inventory" and name in LEGACY_NAMES.get(file, set()):
            continue
        # This batch is shared by online and offline restore. Its caller selects the access schema for the snapshot version.
        if schema is None and file == Path("src/modules/access/restore.hpp") and OWNER[name] == "access":
            continue
        errors.append(f"expected {expected}.{name}, found {schema or '<unqualified>'}.{name}")
    return errors


def local_header(root, file, header):
    for directory in (file.parent, Path("src")):
        dest = (root / directory / header).resolve()
        if dest.is_relative_to(root / "src") and dest.is_file():
            return dest.relative_to(root)
    return None


def include_error(file, header):
    if header == "archive_sql.hpp":
        return module(file) != "recordings" and file != Path("src/archive/block_journal.cpp")
    dest = local_header(ROOT, file, header)
    if dest is None:
        return False
    target = dest.relative_to("src")
    caller = file.relative_to("src")
    if caller.as_posix() in PUBLIC and target.name.endswith("_json.hpp"):
        return True
    # Service implementation headers belong to exactly one executable.
    if target.parts[0] == "services":
        return caller.parts[:2] != target.parts[:2]
    if target.parts[0] == "admin":
        return caller.parts[0] != "admin"
    if target.parts[0] not in {"modules", "application"}:
        return False
    if caller.parts[:2] == target.parts[:2]:
        return False
    if caller.parts[:2] == ("application", "control_api"):
        return True
    if caller.parts[0] in {"domain", "platform", "archive", "recording", "ingest", "media", "observability", "storage"}:
        return True
    if caller.parts[0] == "services" and caller.parts[1] != "control":
        return True
    if caller.parts[0] == "modules" and target.parts[0] == "application" and target.parts[1] != "control_api":
        return True
    if target.name == "restore.hpp":
        return caller.as_posix() not in {"admin/maintenance.cpp", "application/recovery/configuration_backup.cpp"}
    if target.as_posix() in SHARED_CODECS:
        return file.suffix != ".cpp" and caller.as_posix() not in JSON_EDGE_HEADERS
    return target.as_posix() not in PUBLIC


# The export verifier is distributed to third parties, so it must build with libsodium and JSON only.
VERIFIER = Path("src/archive/verify_export.cpp")
VERIFIER_FORBIDDEN = re.compile(r"^(sqlite3\.h|glib|gio/|gst/|archive/volume\.hpp|platform/(settings|secrets)\.hpp|storage/|services/|modules/|application/)")


def verifier_errors(root, entry=VERIFIER):
    seen, pending, errors = set(), [entry], []
    while pending:
        file = pending.pop()
        if file in seen:
            continue
        seen.add(file)
        code = (root / file).read_text()
        for header in re.findall(r'^\s*#\s*include\s*["<]([^">]+)[">]', code, re.M):
            source = local_header(root, file, header)
            name = source.relative_to("src").as_posix() if source else header
            if VERIFIER_FORBIDDEN.match(name):
                errors.append(f"{file}: the export verifier includes {header}")
            if source is not None:
                pending.append(source)
                if source.suffix == ".hpp" and (root / source.with_suffix(".cpp")).is_file():
                    pending.append(source.with_suffix(".cpp"))
    return errors


# JSON belongs to the HTTP adapter and explicit format codecs. All other owner headers,
# including newly added private headers, use C++ types for arguments, results and fields.
JSON_TYPE = re.compile(r"\b(?:Json|json|ordered_json|basic_json)\b|NLOHMANN_DEFINE_TYPE")
JSON_EDGE_HEADERS = {
    "application/control_api/http_json.hpp",
    "application/control_api/playback_time_json.hpp",
    "archive/analysis_json.hpp", "archive/manifest_json.hpp",
    "archive/trusted_key_json.hpp",
    "modules/events/alert_json.hpp", "modules/events/video_json.hpp",
    "modules/inventory/device_configuration_json.hpp", "modules/inventory/device_json.hpp",
    "modules/inventory/onvif/ticket_json.hpp", "modules/inventory/orientation_json.hpp",
    "modules/inventory/registered_camera_json.hpp", "modules/inventory/selection_json.hpp",
    "modules/investigations/case_json.hpp",
    "modules/maps/georeference_json.hpp", "modules/maps/orientation_json.hpp",
    "modules/media_sessions/playback_json.hpp", "modules/media_sessions/export_scope_json.hpp",
}

def owner_header(file):
    return (file.suffix in {".hpp", ".h", ".hh", ".hxx", ".inl"} and
            file.parts[:2] in {("src", "modules"), ("src", "application"), ("src", "archive")} and
            file.relative_to("src").as_posix() not in JSON_EDGE_HEADERS)

def exposes_json(file, code):
    return owner_header(file) and bool(JSON_TYPE.search(CPP_TOKEN.sub("", code)))

def json_include_chain(root, entry):
    pending = [(entry, [entry.as_posix()])]
    seen = {entry}
    while pending:
        file, chain = pending.pop()
        code = (root / file).read_text()
        for header in re.findall(r'^\s*#\s*include\s*["<]([^">]+)[">]', code, re.M):
            if header.startswith("nlohmann/"):
                return [*chain, header]
            target = local_header(root, file, header)
            if target is None or target in seen:
                continue
            path = target.relative_to("src").as_posix()
            if path in JSON_EDGE_HEADERS or target.name.endswith("_json.hpp"):
                return [*chain, target.as_posix()]
            seen.add(target)
            pending.append((target, [*chain, target.as_posix()]))
    return []

def check_source(file, code):
    owner = module(file)
    errors = ["JSON type outside a serialization boundary"] if exposes_json(file, code) else []
    for header in re.findall(r'^\s*#\s*include\s*["<]([^">]+)[">]', code, re.M):
        if include_error(file, header):
            errors.append(f"private include {header}")
    for query in literals(code):
        errors.extend(placement_errors(file, query))
        errors.extend(f"{owner} accesses {OWNER[name]}.{name}" for name in sorted(violations(owner, query)))
        if re.search(r"\b(?:from|join|into|truncate)\s*$", query, re.I) and file != Path("src/storage/postgresql/configuration_snapshot.cpp"):
            errors.append("dynamic relation outside the configuration snapshot adapter")
    # The backup adapter builds SQL only from a section declared by its storage owner.
    for section in re.finditer(r'\.section\(\s*([^,\)]+)', code):
        relation = section[1].strip().strip('"')
        schema, _, name = relation.partition(".")
        if OWNER.get(name) != owner or MODULE_SCHEMAS.get(owner) != schema:
            errors.append(f"invalid snapshot section {relation}")
    return [f"{file}: {issue}" for issue in errors]

SQL_NAME = r'(?:"[a-z_][\w$]*"|[a-z_][\w$]*)'
OBJECT_NAME = rf'(?:{SQL_NAME}\s*\.\s*)?({SQL_NAME})'
DDL = re.compile(rf'\bCREATE\s+(?:OR\s+REPLACE\s+)?(?:MATERIALIZED\s+)?(?:TABLE|VIEW|FUNCTION)\s+'
                 rf'(?:IF\s+NOT\s+EXISTS\s+)?{OBJECT_NAME}|'
                 rf'\bALTER\s+(?:TABLE|VIEW)\s+[\w."]+\s+RENAME\s+TO\s+({SQL_NAME})', re.I)


def check_storage():
    errors = []
    known = set()
    migrations = sorted((ROOT / "storage/postgresql/migrations").glob("*.sql"))
    registered = re.findall(r'"(\d{4}_[a-z0-9_]+\.sql)"', (ROOT / "src/storage/postgresql/migrations.cpp").read_text())
    if registered != [file.name for file in migrations]:
        errors.append("the runtime migration order differs from the PostgreSQL migrations")
    for file in migrations:
        sql = file.read_text()
        for first, renamed in DDL.findall(sql):
            known.add((first or renamed).strip('"').lower())
        # Immutable migrations define the retired views. No runtime module can read them.
        historical = {
            "0004_playback.sql": {"media_rights", "visible_points"},
            "0007_recovery.sql": {"media_rights"},
            "0010_maps.sql": {"visible_plans"},
            "0011_alert_work.sql": {"alert_audit_details"},
        }
        views = rf"CREATE\s+(?:OR\s+REPLACE\s+)?(?:MATERIALIZED\s+)?VIEW\s+{OBJECT_NAME}\s+AS\s+(.*?);"
        functions = rf"CREATE\s+(?:OR\s+REPLACE\s+)?FUNCTION\s+{OBJECT_NAME}\s*\([^;]*?\bAS\s+\$\$(.*?)\$\$"
        for pattern in (views, functions):
            for name, body in re.findall(pattern, sql, re.I | re.S):
                name = name.strip('"').lower()
                if name in historical.get(file.name, set()):
                    continue
                if int(file.name.split("_")[0]) >= 16:
                    errors.extend(f"{file.name}: {name}: {issue}" for issue in placement_errors(file, body))
                for other in violations(OWNER.get(name), body):
                    errors.append(f"{file.name}: {name} accesses {other}")
    errors.extend(f"missing storage owner: {name}" for name in sorted(known - OWNER.keys()))
    errors.extend(f"unknown storage object: {name}" for name in sorted(OWNER.keys() - known))
    for file in (ROOT / "storage/postgresql").glob("*.sql"):
        errors.extend(placement_errors(file, file.read_text()))
        errors.extend(f"{file.name}: archive query accesses {name}" for name in violations("recordings", file.read_text()))
    return errors

def check_examples():
    if duplicates := PUBLIC & JSON_EDGE_HEADERS:
        raise AssertionError(f"Public module API listed as a JSON adapter: {sorted(duplicates)}")
    if missing := [name for name in JSON_EDGE_HEADERS if not (ROOT / "src" / name).is_file()]:
        raise AssertionError(f"Unknown JSON adapter headers: {sorted(missing)}")
    examples = [
        ("SELECT * FROM retained_blocks", {"retained_blocks"}),
        ("SELECT * FROM visible_points", {"visible_points"}),
        ("SELECT retention_cutoff($1)", {"retention_cutoff"}),
        ('SELECT * FROM camera_registry r JOIN "bacuri_archive"."retained_blocks" b ON true', {"retained_blocks"}),
        ("UPDATE bacuri_events.alerts SET work_state='new'", {"alerts"}),
        ("INSERT INTO alert_audit SELECT * FROM media_sessions", {"alert_audit", "media_sessions"}),
        ("SELECT * FROM camera_registry, local_users", {"local_users"}),
        ("DELETE FROM point_grants USING site_roles", {"point_grants", "site_roles"}),
        ("SELECT 'FROM local_users' FROM camera_registry", set()),
    ]
    for sql, expected in examples:
        if violations("inventory", sql) != expected:
            raise AssertionError(sql)
    joined, = literals('"SELECT * " /* separator */ R"sql(FROM )sql" "\\"local_users\\""')
    if violations("events", joined) != {"local_users"}:
        raise AssertionError(joined)
    for path in ("src/modules/maps/model.hpp", "src/application/export/results.hpp", "src/archive/example.hpp"):
        for declaration in ("Json get();", "void set(const nlohmann::json&);", "using Payload = nlohmann::ordered_json;",
                            "std::optional<Json> metadata;", "NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Data, value)"):
            if not exposes_json(Path(path), declaration):
                raise AssertionError(f"{path} accepted {declaration}")
        if exposes_json(Path(path), '// Json format\nstruct Example { std::string name; };'):
            raise AssertionError("JSON mentioned only in a comment")
    with tempfile.TemporaryDirectory() as sample:
        root = Path(sample)
        entry = Path("src/modules/maps/model.hpp")
        (root / entry.parent).mkdir(parents=True)
        (root / entry).write_text('#include "detail.hpp"')
        detail = root / entry.parent / "detail.hpp"
        for header in ("nlohmann/json.hpp", "nlohmann/json_fwd.hpp"):
            detail.write_text(f'#include "model.hpp"\n#include <{header}>')
            assert json_include_chain(root, entry) == [entry.as_posix(), "src/modules/maps/detail.hpp", header]
        detail.write_text('#include "model.hpp"')
        assert not json_include_chain(root, entry)
    caller = Path("src/modules/events/operations.cpp")
    allowed = '#include "modules/inventory/operations.hpp"\n db.exec("SELECT * FROM bacuri_events.alerts");'
    if check_source(caller, allowed):
        raise AssertionError("owner query and public include")
    for path, header in [
        ("src/archive/volume.cpp", "application/control_api/api.hpp"),
        ("src/modules/events/operations.hpp", "modules/events/alert_json.hpp"),
        ("src/modules/maps/operations.hpp", "modules/inventory/orientation_json.hpp"),
        ("src/services/media/live.cpp", "modules/access/operations.hpp"),
        ("src/modules/maps/scene.cpp", "../../services/control/transport/routes.hpp"),
        ("src/modules/inventory/registry.cpp", "application/export/export.hpp"),
        ("src/storage/postgresql/database.cpp", "modules/access/operations.hpp"),
        ("src/application/control_api/api.cpp", "modules/maps/operations.hpp"),
        ("src/services/control/main.cpp", "admin/maintenance.hpp"),
    ]:
        if not include_error(Path(path), header):
            raise AssertionError(f"{path} accepted a forbidden include: {header}")
    prohibited = [
        '#include "../inventory/onvif.hpp"',
        'db.exec("SELECT count(*) FROM " "local_users");',
        'db.exec(R"sql(SELECT * FROM bacuri_events.alerts JOIN visible_points USING(point_id))sql");',
        'db.exec("DELETE FROM " + table);',
        'snapshot.section("site_roles");',
        'db.exec("SELECT * FROM bacuri_maps.alerts");',
        'db.exec("SELECT * FROM alerts");',
        'db.exec("SELECT * FROM other_schema.alerts");',
        'snapshot.section("bacuri_maps.alerts");',
    ]
    for source in prohibited:
        if not check_source(caller, source):
            raise AssertionError(f"undetected boundary violation: {source}")


def check_catalog(database):
    query = """
        SELECT n.nspname,c.relname FROM pg_catalog.pg_class c
        JOIN pg_catalog.pg_namespace n ON n.oid=c.relnamespace
        WHERE c.relkind IN ('r','p','v','m','S','f')
        AND n.nspname !~ '^pg_' AND n.nspname <> 'information_schema'
        UNION ALL
        SELECT nspname,'<schema>' FROM pg_catalog.pg_namespace WHERE starts_with(nspname,'bacuri_')
        UNION ALL
        SELECT n.nspname,p.proname FROM pg_catalog.pg_proc p
        JOIN pg_catalog.pg_namespace n ON n.oid=p.pronamespace
        WHERE n.nspname !~ '^pg_' AND n.nspname <> 'information_schema'
    """
    rows = subprocess.check_output(
        ["psql", "-XAt", "-v", "ON_ERROR_STOP=1", "-F", ".", "-d", database, "-c", query], text=True
    )
    actual = set(rows.splitlines())
    expected = {f"{MODULE_SCHEMAS[owner]}.{name}" for name, owner in OWNER.items() if owner != "retired"}
    expected.update(f"{schema}.<schema>" for schema in SCHEMAS)
    return ([f"missing object: {name}" for name in sorted(expected - actual)] +
            [f"unexpected object: {name}" for name in sorted(actual - expected)])


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--database", help="verify a disposable, migrated PostgreSQL database")
    args = parser.parse_args()
    check_examples()
    issues = check_storage()
    with tempfile.TemporaryDirectory() as sample:
        (Path(sample) / "src/archive").mkdir(parents=True)
        (Path(sample) / VERIFIER).write_text('#include "verify.hpp"\n')
        (Path(sample) / "src/archive/verify.hpp").write_text("")
        adapter = Path(sample) / "src/storage/sqlite/sqlite.hpp"
        adapter.parent.mkdir(parents=True)
        adapter.touch()
        for header in ("<sqlite3.h>", '"../storage/sqlite/sqlite.hpp"'):
            (Path(sample) / "src/archive/verify.cpp").write_text(f"#include {header}\n")
            if not verifier_errors(Path(sample)):
                raise AssertionError(f"undetected export verifier dependency: {header}")
    issues.extend(verifier_errors(ROOT))
    for file in sorted((ROOT / "src").rglob("*")):
        if file.suffix in {".cpp", ".hpp", ".cc", ".cxx", ".hh", ".hxx", ".h", ".inl"}:
            relative, code = file.relative_to(ROOT), file.read_text()
            if owner_header(relative) and (chain := json_include_chain(ROOT, relative)):
                issues.append("JSON dependency in owner header: " + " -> ".join(chain))
            issues.extend(check_source(relative, code))
        elif file.suffix == ".sql":
            relative = file.relative_to(ROOT)
            issues.extend(f"{relative}: {module(relative)} accesses {name}"
                          for name in violations(module(relative), file.read_text()))
    if args.database:
        issues.extend(check_catalog(args.database))
    if issues:
        sys.exit("\n".join(issues))
    print("Core module boundaries passed")
