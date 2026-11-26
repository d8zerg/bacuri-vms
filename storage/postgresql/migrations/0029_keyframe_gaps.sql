BEGIN;
SET LOCAL search_path=pg_catalog,pg_temp;
-- A camera that sends no keyframe within the block limits loses the frames until the next keyframe.
ALTER TABLE bacuri_archive.gaps DROP CONSTRAINT gaps_reason_check;
ALTER TABLE bacuri_archive.gaps ADD CONSTRAINT gaps_reason_check
    CHECK (reason IN ('source_offline', 'storage_failure', 'overload', 'keyframe_interval'));
INSERT INTO bacuri_meta.schema_migrations VALUES(29);
COMMIT;
