BEGIN;

CREATE FUNCTION bacuri_archive.enqueue_index() RETURNS trigger LANGUAGE plpgsql AS $$
BEGIN
    INSERT INTO bacuri_archive.motion_indexes(site_id,block_id,recording_epoch_id,owner_generation,retry_at)
    VALUES(NEW.site_id,NEW.block_id,NEW.recording_epoch_id,NEW.owner_generation,clock_timestamp());
    RETURN NEW;
END $$;
CREATE TRIGGER enqueue_motion_index AFTER INSERT ON bacuri_archive.blocks
    FOR EACH ROW EXECUTE FUNCTION bacuri_archive.enqueue_index();

DROP INDEX bacuri_archive.motion_index_retry;
INSERT INTO bacuri_archive.motion_indexes(site_id,block_id,recording_epoch_id,owner_generation,retry_at)
SELECT site_id,block_id,recording_epoch_id,owner_generation,statement_timestamp() FROM bacuri_archive.blocks
ON CONFLICT(site_id,block_id,recording_epoch_id,owner_generation) DO NOTHING;
CREATE INDEX motion_index_ready ON bacuri_archive.motion_indexes(site_id,retry_at,block_id) WHERE samples IS NULL;

INSERT INTO bacuri_meta.schema_migrations VALUES(28);
COMMIT;
