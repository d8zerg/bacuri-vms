BEGIN;
SET LOCAL search_path = bacuri_inventory,pg_catalog;
CREATE TABLE archive_retention (
    site_id uuid PRIMARY KEY REFERENCES sites,
    days integer NOT NULL CHECK (days BETWEEN 1 AND 36500),
    reserve_percent integer NOT NULL CHECK (reserve_percent BETWEEN 1 AND 90)
);
CREATE TABLE bacuri_archive.retired_through (
    site_id uuid PRIMARY KEY REFERENCES sites,
    end_us bigint NOT NULL
);
CREATE FUNCTION bacuri_archive.retention_now_us() RETURNS bigint LANGUAGE sql STABLE AS $$
    SELECT (extract(epoch FROM statement_timestamp()) * 1000000)::bigint
$$;
CREATE FUNCTION bacuri_archive.retention_cutoff(site uuid) RETURNS bigint LANGUAGE sql STABLE AS $$
    SELECT greatest(
        (SELECT end_us FROM bacuri_archive.retired_through WHERE site_id=site),
        (SELECT bacuri_archive.retention_now_us() - days * 86400000000::bigint
         FROM bacuri_inventory.archive_retention WHERE site_id=site),
        '-9223372036854775808'::bigint)
$$;
CREATE FUNCTION bacuri_archive.advance_cutoff(site uuid) RETURNS void LANGUAGE sql AS $$
    INSERT INTO bacuri_archive.retired_through
    SELECT site_id,bacuri_archive.retention_cutoff(site) FROM bacuri_inventory.archive_retention WHERE site_id=site
    ON CONFLICT(site_id) DO UPDATE SET end_us=greatest(retired_through.end_us,excluded.end_us)
$$;
CREATE VIEW bacuri_archive.retained_blocks AS
SELECT * FROM bacuri_archive.blocks WHERE end_utc_us > bacuri_archive.retention_cutoff(site_id);
CREATE OR REPLACE VIEW bacuri_archive.timeline_intervals AS
SELECT site_id,point_id,binding_revision,begin_utc_us,end_utc_us,
       CASE WHEN unavailable_reason IS NULL THEN 'recorded' ELSE 'gap' END AS interval_kind,
       CASE WHEN unavailable_reason IS NULL THEN time_basis END AS time_basis,
       CASE WHEN unavailable_reason IS NULL THEN uncertainty_us END AS uncertainty_us,
       CASE WHEN unavailable_reason IS NOT NULL THEN 'storage_failure' END AS gap_reason
FROM bacuri_archive.retained_blocks
UNION ALL SELECT site_id,point_id,binding_revision,begin_utc_us,end_utc_us,'gap',NULL,NULL,reason
FROM bacuri_archive.gaps WHERE end_utc_us > bacuri_archive.retention_cutoff(site_id);
DO $$
DECLARE block_fk name;
BEGIN
    SELECT conname INTO STRICT block_fk FROM pg_constraint
    WHERE conrelid='playback_parts'::regclass AND confrelid='bacuri_archive.blocks'::regclass;
    EXECUTE format('ALTER TABLE playback_parts DROP CONSTRAINT %I',block_fk);
END $$;
ALTER TABLE playback_parts ADD FOREIGN KEY(site_id,block_id,recording_epoch_id,owner_generation)
    REFERENCES bacuri_archive.blocks ON DELETE CASCADE;
CREATE INDEX playback_parts_by_block ON playback_parts(site_id,block_id,recording_epoch_id,owner_generation);
CREATE INDEX blocks_by_end ON bacuri_archive.blocks(site_id,end_utc_us);
CREATE INDEX gaps_by_end ON bacuri_archive.gaps(site_id,end_utc_us);
INSERT INTO schema_migrations VALUES (12);
COMMIT;
