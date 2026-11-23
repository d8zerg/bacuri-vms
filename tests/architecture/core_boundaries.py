"""Check storage ownership and public includes in the modular Core."""
import argparse
import ast
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

# Objects that the current executable can access. Retired projections still exist in older databases.
OBJECTS = {
    "inventory": """sites observation_points devices channels stream_profiles point_bindings configuration_versions
        configuration_progress camera_registry recorder_delivery recorder_attempt registration_requests configuration_audit
        camera_templates camera_batch_items camera_configuration_state camera_state camera_transport_audit record_camera_transport_policy ptz_commands ptz_telemetry digital_input_states camera_talk_sessions""",
    "access": """local_users site_roles point_grants browser_sessions login_attempts export_grants webauthn_credentials
        webauthn_enrollment webauthn_challenges credential_audit resource_groups group_points scoped_grants effective_grants privacy_policies identity_providers
        external_identities oidc_requests policy_versions access_audit can_use audit_policy_change invalidate_role_sessions audit_credential_binding""",
    "recordings": """blocks gaps retired_through catalog_intervals timeline_intervals retained_blocks archive_retention archive_recovery
        retention_now_us retention_cutoff advance_cutoff playback_parts recording_policies recording_windows point_retention point_cutoff reap_expired""",
    "media_sessions": "media_sessions monitor_layouts",
    "events": """observations feed_heads alerts alert_outbox alert_details access_versions touch_alert_scope
        alert_delegations alert_audit protect_alert_audit""",
    "maps": "map_regions map_tiles floor_plans point_placements map_jobs map_sources map_catalog_settings",
    "database": "schema_migrations",
    "retired": "visible_points visible_plans media_rights alert_audit_details",
}
OWNER = {name: owner for owner, names in OBJECTS.items() for name in names.split()}

# Existing entry points in src/control. New module files live under their owner's directory.
FILES = {
    "inventory": "registry onvif camera_events delivery",
    "access": "session webauthn",
    "recordings": "archive_catalog archive_recovery",
    "media_sessions": "playback media_authority",
    "events": "observations alerts alert_work observation_source",
    "maps": "maps map_import",
    "export": "export",
    "transport": "server https alert_socket",
    "database": "database",
    "recovery": "configuration_backup recovery configuration_snapshot",
    "shared": "api http support playback_time",
}
ENTRY_OWNER = {name: owner for owner, names in FILES.items() for name in names.split()}
PUBLIC = set("""
    api.hpp http.hpp support.hpp playback_time.hpp database.hpp recovery.hpp configuration_snapshot.hpp
    archive_catalog.hpp maps.hpp alerts.hpp alert_work.hpp observations.hpp delivery.hpp media_authority.hpp
    playback.hpp export.hpp access/enrollment.hpp inventory/camera_events.hpp access/session.hpp inventory/registry.hpp
    access/operations.hpp access/admin.hpp access/oidc.hpp inventory/operations.hpp inventory/telemetry.hpp events/operations.hpp recordings/operations.hpp media_sessions/operations.hpp
    access/configuration.hpp inventory/configuration.hpp recordings/policies.hpp recordings/configuration.hpp maps/configuration.hpp
    access/restore.hpp inventory/restore.hpp
""".split())

def module(file):
    if file.parts[:2] == ("src", "control"):
        path = file.relative_to("src/control")
        return path.parts[0] if len(path.parts) > 1 else ENTRY_OWNER.get(path.stem, "unassigned")
    return "composition" if file.parts[:2] == ("src", "core") else "external"

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
    "maps": "bacuri_maps", "database": "bacuri_meta",
}
SCHEMAS = set(MODULE_SCHEMAS.values())
# Previous packages and their snapshots use the original physical names during migration and rollback.
LEGACY_NAMES = {
    Path("src/control/database.hpp"): {"schema_migrations"},
    Path("src/control/database.cpp"): {"schema_migrations"},
    Path("src/control/archive_recovery.cpp"): {"archive_recovery"},
    Path("src/control/media_sessions/operations.cpp"): {"media_sessions"},
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
        if schema is None and file == Path("src/control/access/restore.hpp") and OWNER[name] == "access":
            continue
        errors.append(f"expected {expected}.{name}, found {schema or '<unqualified>'}.{name}")
    return errors


def include_error(file, header):
    src = ROOT / "src"
    candidates = [(ROOT / file.parent / header).resolve(), (src / header).resolve()]
    dest = next((p for p in candidates if p.is_file()), candidates[-1])
    if header == "archive_sql.hpp":
        return module(file) != "recordings" and file != Path("src/archive/volume.cpp")
    if not dest.is_relative_to(src / "control"):
        return False
    relative = dest.relative_to(src / "control").as_posix()
    if relative.endswith("/restore.hpp") and file not in {
        Path("src/core/maintenance.cpp"), Path("src/control/configuration_backup.cpp")
    }:
        return module(file) != module(dest.relative_to(ROOT))
    return relative not in PUBLIC and module(file) != module(dest.relative_to(ROOT))

def check_source(file, code):
    owner = module(file)
    errors = []
    for header in re.findall(r'^\s*#\s*include\s*["<]([^">]+)[">]', code, re.M):
        if include_error(file, header):
            errors.append(f"private include {header}")
    if owner == "unassigned":
        errors.append("assign the entry point to a Core module")
    for query in literals(code):
        errors.extend(placement_errors(file, query))
        errors.extend(f"{owner} accesses {OWNER[name]}.{name}" for name in sorted(violations(owner, query)))
        if re.search(r"\b(?:from|join|into|truncate)\s*$", query, re.I) and file != Path("src/control/configuration_snapshot.cpp"):
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
    registered = re.findall(r'"(\d{4}_[a-z0-9_]+\.sql)"', (ROOT / "src/control/database.cpp").read_text())
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
    caller = Path("src/control/events/operations.cpp")
    allowed = '#include "control/inventory/operations.hpp"\n db.exec("SELECT * FROM bacuri_events.alerts");'
    if check_source(caller, allowed):
        raise AssertionError("owner query and public include")
    prohibited = [
        '#include "../onvif.hpp"',
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
    for file in sorted((ROOT / "src").rglob("*")):
        if file.suffix in {".cpp", ".hpp", ".cc", ".cxx", ".hh", ".hxx", ".h", ".inl"}:
            issues.extend(check_source(file.relative_to(ROOT), file.read_text()))
        elif file.suffix == ".sql":
            relative = file.relative_to(ROOT)
            issues.extend(f"{relative}: {module(relative)} accesses {name}"
                          for name in violations(module(relative), file.read_text()))
    if args.database:
        issues.extend(check_catalog(args.database))
    if issues:
        sys.exit("\n".join(issues))
    print("Core module boundaries passed")
