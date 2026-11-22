BEGIN;
SET LOCAL search_path=pg_catalog,pg_temp;
SET LOCAL statement_timeout=0;

CREATE INDEX map_sources_pending ON bacuri_maps.map_sources(site_id,touched) WHERE state='pending';
CREATE INDEX map_jobs_runnable ON bacuri_maps.map_jobs(site_id,next_attempt,created)
    WHERE state IN ('queued','downloading','retrying','installing');
CREATE INDEX map_jobs_by_source ON bacuri_maps.map_jobs(source_id);
CREATE INDEX placements_by_plan ON bacuri_maps.point_placements(site_id,plan_id,point_id) WHERE plan_id IS NOT NULL;

CREATE OR REPLACE VIEW bacuri_events.alert_details AS
SELECT a.site_id,a.position,a.alert_id,a.source_id,a.observation_id,a.point_id,
       o.binding_revision,o.type,o.observed_at_utc_us,o.time_basis,o.uncertainty_us,a.work_state
FROM bacuri_events.alerts a JOIN bacuri_events.observations o USING(site_id,source_id,observation_id);

CREATE VIEW bacuri_archive.catalog_intervals AS
SELECT site_id,point_id,binding_revision,begin_utc_us,end_utc_us,
       CASE WHEN unavailable_reason IS NULL THEN 'recorded' ELSE 'gap' END AS interval_kind,
       CASE WHEN unavailable_reason IS NULL THEN time_basis END AS time_basis,
       CASE WHEN unavailable_reason IS NULL THEN uncertainty_us END AS uncertainty_us,
       CASE WHEN unavailable_reason IS NOT NULL THEN 'storage_failure' END AS gap_reason
FROM bacuri_archive.blocks
UNION ALL SELECT site_id,point_id,binding_revision,begin_utc_us,end_utc_us,'gap',NULL,NULL,reason
FROM bacuri_archive.gaps;

CREATE OR REPLACE VIEW bacuri_archive.timeline_intervals AS
SELECT * FROM bacuri_archive.catalog_intervals WHERE end_utc_us > bacuri_archive.retention_cutoff(site_id);

CREATE OR REPLACE FUNCTION bacuri_archive.advance_cutoff(site uuid) RETURNS void LANGUAGE sql AS $$
    INSERT INTO bacuri_archive.retired_through AS retired
    SELECT site_id,bacuri_archive.retention_now_us()-days*86400000000::bigint
    FROM bacuri_archive.archive_retention WHERE site_id=site
    ON CONFLICT(site_id) DO UPDATE SET end_us=excluded.end_us WHERE retired.end_us<excluded.end_us
$$;

INSERT INTO bacuri_meta.schema_migrations VALUES(20);
COMMIT;
