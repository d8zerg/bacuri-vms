BEGIN;
ALTER TABLE bacuri_archive.blocks ADD COLUMN unavailable_reason text
    CHECK (unavailable_reason IN ('missing','corrupt'));
ALTER TABLE bacuri_archive.blocks ADD COLUMN verification_generation bigint NOT NULL DEFAULT 0 CHECK (verification_generation>=0);
CREATE OR REPLACE VIEW bacuri_archive.timeline_intervals AS
SELECT site_id, point_id, binding_revision, begin_utc_us, end_utc_us,
       'recorded'::text AS interval_kind, time_basis, uncertainty_us, NULL::text AS gap_reason
FROM bacuri_archive.blocks WHERE unavailable_reason IS NULL
UNION ALL
SELECT site_id, point_id, binding_revision, begin_utc_us, end_utc_us,
       'gap'::text, NULL::text, NULL::bigint, 'storage_failure'::text
FROM bacuri_archive.blocks WHERE unavailable_reason IS NOT NULL
UNION ALL
SELECT site_id, point_id, binding_revision, begin_utc_us, end_utc_us,
       'gap'::text, NULL::text, NULL::bigint, reason
FROM bacuri_archive.gaps;
-- Deliver the archive provenance even for previously accepted camera assignments.
UPDATE bacuri_inventory.recorder_delivery SET desired_revision=desired_revision+1;
INSERT INTO bacuri_inventory.schema_migrations VALUES (3);
COMMIT;
