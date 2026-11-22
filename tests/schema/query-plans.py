"""Compare production access paths on a populated, disposable PostgreSQL database."""
import json
from pathlib import Path
from statistics import median
import subprocess
from uuid import uuid4

SITE = "'10000000-0000-4000-8000-000000000001'"
POINT = "'20000000-0000-4000-8000-000000000001'"
MIGRATIONS = Path("storage/postgresql/migrations")
FIXTURES = Path("tests/schema")
QUERIES = {
    "pending sources": f"""SELECT id,kind,url FROM bacuri_maps.map_sources WHERE site_id={SITE} AND state='pending'
                           ORDER BY touched LIMIT 1""",
    "runnable jobs": f"""SELECT j.id,j.site_id,j.source_id,j.mode,s.url,e FROM bacuri_maps.map_jobs j
        JOIN bacuri_maps.map_sources s ON s.id=j.source_id CROSS JOIN LATERAL jsonb_array_elements(s.entries) e
        WHERE j.site_id={SITE} AND e->>'id'=j.region_id AND j.state IN ('queued','downloading','retrying','installing')
        AND j.next_attempt<=now() ORDER BY j.next_attempt,j.created LIMIT 1""",
    "source references": "SELECT id FROM bacuri_maps.map_jobs WHERE source_id=md5('18461')::uuid",
    "plan markers": f"""SELECT point_id,x,y FROM bacuri_maps.point_placements WHERE site_id={SITE}
                        AND plan_id=md5('248')::uuid ORDER BY point_id""",
}


def check(database):
    psql = ["psql", "-XAtq", "-v", "ON_ERROR_STOP=1", "-d", database]

    def sql(query):
        return subprocess.check_output(psql, text=True, input=query).strip()

    def measure(query, setup=""):
        result = sql(setup + query)
        plans = [json.loads(sql(setup + "EXPLAIN (ANALYZE, BUFFERS, FORMAT JSON) " + query))[0] for _ in range(3)]
        pages = [p["Plan"]["Shared Hit Blocks"] + p["Plan"]["Shared Read Blocks"] for p in plans]
        return result, median(pages), median(p["Execution Time"] for p in plans)

    def sample(archive, legacy):
        setup = "PREPARE timeline(uuid,uuid,bigint,bigint,jsonb,text) AS " + archive.read_text() + ";\n"
        result = {name: measure(query) for name, query in QUERIES.items()}
        join = " JOIN bacuri_events.alerts a USING(site_id,position,point_id)" if legacy else ""
        state = "a.work_state" if legacy else "d.work_state"
        result["alert feed"] = measure(f"""SELECT d.alert_id,d.point_id,d.observed_at_utc_us,d.type,d.time_basis,
            d.uncertainty_us,d.position,{state} FROM bacuri_events.alert_details d {join}
            WHERE site_id={SITE} ORDER BY d.position DESC LIMIT 100""")
        pending = '[{"a":49995,"b":50005},{"a":50009,"b":50012}]'
        result["archive timeline"] = measure(f"""EXECUTE timeline({SITE},{POINT},49990,51000,'{pending}','1')""", setup)
        result["archive full page"] = measure(f"""EXECUTE timeline({SITE},{POINT},49990,99999,\'[]\',\'\')""", setup)
        return result

    print("PostgreSQL", sql("SHOW server_version"), flush=True)
    migrations = sorted(MIGRATIONS.glob("*.sql"))
    previous = [f for f in migrations if int(f.name[:4]) < 20]
    for path in previous:
        sql(path.read_text())
    subprocess.run(psql + ["-v", "manifest=" + (FIXTURES / "block-manifest-v1.json").read_text(),
                           "-f", str(FIXTURES / "performance-data.sql")], check=True)
    before = sample(FIXTURES / "search-archive-v19.sql", legacy=True)
    for path in migrations[len(previous):]:
        sql(path.read_text())
    after = sample(Path("storage/postgresql/search-archive.sql"), legacy=False)
    for name, (result, pages, ms) in after.items():
        old_result, old_pages, old_ms = before[name]
        assert old_result and old_result == result, name
        print(f"{name}: {old_pages} -> {pages} shared buffer accesses, {old_ms:.3f} -> {ms:.3f} ms", flush=True)
        if name == "alert feed":
            # This projection removes a join; the underlying indexed lookup still reads the same observations.
            assert pages <= old_pages * 1.1, (name, old_pages, pages)
        else:
            assert pages < old_pages, (name, old_pages, pages)

    # A longer policy cannot bring retired intervals back or create another tuple for the same boundary.
    sql(f"""INSERT INTO bacuri_archive.archive_retention VALUES ({SITE},1,20);
            UPDATE bacuri_archive.retired_through SET end_us=9223372036854775807 WHERE site_id={SITE}""")
    read_boundary = f"SELECT end_us,ctid FROM bacuri_archive.retired_through WHERE site_id={SITE}"
    saved = sql(read_boundary)
    sql(f"SELECT bacuri_archive.advance_cutoff({SITE})")
    assert saved == sql(read_boundary)
    sql(f"DELETE FROM bacuri_archive.retired_through WHERE site_id={SITE}; SELECT bacuri_archive.advance_cutoff({SITE})")
    assert sql(f"SELECT end_us>50007 FROM bacuri_archive.retired_through WHERE site_id={SITE}") == "t"


if __name__ == "__main__":
    database = "bacuri_plans_" + uuid4().hex
    subprocess.run(["createdb", "--template=template0", database], check=True)
    try:
        check(database)
    finally:
        subprocess.run(["dropdb", database], check=True)
