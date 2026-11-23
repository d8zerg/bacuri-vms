BEGIN;
CREATE TABLE bacuri_archive.recording_policies (
    site_id uuid NOT NULL, id uuid NOT NULL, scope text NOT NULL CHECK(scope IN ('point','group')),
    target uuid NOT NULL, priority integer NOT NULL CHECK(priority BETWEEN 0 AND 100),
    rule jsonb NOT NULL, revision bigint NOT NULL DEFAULT 1, changed_us bigint NOT NULL,
    PRIMARY KEY(site_id,id), UNIQUE(site_id,scope,target)
);
CREATE TABLE bacuri_archive.recording_windows (
    site_id uuid NOT NULL, point_id uuid NOT NULL, binding bigint NOT NULL, policy_id uuid NOT NULL,
    revision bigint NOT NULL, kind text NOT NULL CHECK(kind IN ('manual','event')),
    begin_us bigint NOT NULL, end_us bigint NOT NULL, CHECK(end_us>=begin_us),
    FOREIGN KEY(site_id,policy_id) REFERENCES bacuri_archive.recording_policies(site_id,id) ON DELETE CASCADE
);
CREATE INDEX recording_windows_by_end ON bacuri_archive.recording_windows(site_id,end_us);
CREATE TABLE bacuri_archive.point_retention (
    site_id uuid NOT NULL, point_id uuid NOT NULL, days integer, cutoff_us bigint NOT NULL,
    PRIMARY KEY(site_id,point_id)
);
CREATE INDEX blocks_by_point_end ON bacuri_archive.blocks(site_id,point_id,end_utc_us);
CREATE INDEX gaps_by_point_end ON bacuri_archive.gaps(site_id,point_id,end_utc_us);
CREATE FUNCTION bacuri_archive.reap_expired(s uuid, cutoff bigint) RETURNS void LANGUAGE sql AS $$
 DELETE FROM bacuri_archive.blocks WHERE ctid IN (
    SELECT id FROM (
        (SELECT ctid AS id FROM bacuri_archive.blocks WHERE site_id=s AND end_utc_us<=cutoff ORDER BY end_utc_us LIMIT 512)
        UNION ALL
        (SELECT expired.id FROM bacuri_archive.point_retention r CROSS JOIN LATERAL (
            SELECT b.ctid AS id FROM bacuri_archive.blocks b WHERE b.site_id=r.site_id AND b.point_id=r.point_id
                AND b.end_utc_us<=r.cutoff_us ORDER BY b.end_utc_us LIMIT 512
         ) expired WHERE r.site_id=s AND r.cutoff_us>cutoff LIMIT 512)
    ) pending LIMIT 512
 );
 DELETE FROM bacuri_archive.gaps WHERE ctid IN (
    SELECT id FROM (
        (SELECT ctid AS id FROM bacuri_archive.gaps WHERE site_id=s AND end_utc_us<=cutoff ORDER BY end_utc_us LIMIT 512)
        UNION ALL
        (SELECT expired.id FROM bacuri_archive.point_retention r CROSS JOIN LATERAL (
            SELECT b.ctid AS id FROM bacuri_archive.gaps b WHERE b.site_id=r.site_id AND b.point_id=r.point_id
                AND b.end_utc_us<=r.cutoff_us ORDER BY b.end_utc_us LIMIT 512
         ) expired WHERE r.site_id=s AND r.cutoff_us>cutoff LIMIT 512)
    ) pending LIMIT 512
 );
$$;
CREATE FUNCTION bacuri_archive.point_cutoff(s uuid,p uuid) RETURNS bigint LANGUAGE sql STABLE AS $$
 SELECT greatest(bacuri_archive.retention_cutoff(s),
    (SELECT greatest(cutoff_us,bacuri_archive.retention_now_us()-days*86400000000::bigint)
     FROM bacuri_archive.point_retention WHERE site_id=s AND point_id=p))
$$;
CREATE OR REPLACE VIEW bacuri_archive.retained_blocks AS SELECT * FROM bacuri_archive.blocks
 WHERE end_utc_us>bacuri_archive.point_cutoff(site_id,point_id);
CREATE OR REPLACE VIEW bacuri_archive.timeline_intervals AS
 SELECT site_id,point_id,binding_revision,begin_utc_us,end_utc_us,
 CASE WHEN unavailable_reason IS NULL THEN 'recorded' ELSE 'gap' END AS interval_kind,
 CASE WHEN unavailable_reason IS NULL THEN time_basis END AS time_basis,
 CASE WHEN unavailable_reason IS NULL THEN uncertainty_us END AS uncertainty_us,
 CASE WHEN unavailable_reason IS NOT NULL THEN 'storage_failure' END AS gap_reason FROM bacuri_archive.retained_blocks
 UNION ALL SELECT site_id,point_id,binding_revision,begin_utc_us,end_utc_us,'gap',NULL,NULL,reason FROM bacuri_archive.gaps
 WHERE end_utc_us>bacuri_archive.point_cutoff(site_id,point_id);
ALTER TABLE bacuri_access.scoped_grants DROP CONSTRAINT scoped_grants_action_check;
ALTER TABLE bacuri_access.scoped_grants ADD CHECK(action IN
 ('view','audio','audio_talk','audio_record','record','ptz','configure','protect','export'));
ALTER TABLE bacuri_archive.archive_retention ADD COLUMN revision bigint NOT NULL DEFAULT 1;
ALTER TABLE bacuri_events.alert_audit DROP CONSTRAINT alert_audit_action_check;
ALTER TABLE bacuri_events.alert_audit ADD CHECK(action IN
 ('claim','record_result','close','delegate','revoke','export','camera_control','recording_policy','manual_recording','recording_storage'));
INSERT INTO bacuri_meta.schema_migrations VALUES (26);
COMMIT;
