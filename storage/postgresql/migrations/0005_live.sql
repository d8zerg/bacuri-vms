BEGIN;
SET LOCAL search_path = bacuri_inventory, pg_catalog;
ALTER TABLE media_sessions ALTER COLUMN begin_us DROP NOT NULL, ALTER COLUMN end_us DROP NOT NULL,
    ADD CONSTRAINT complete_media_interval CHECK ((begin_us IS NULL) = (end_us IS NULL)),
    ADD COLUMN live_negotiated boolean NOT NULL DEFAULT false;
INSERT INTO schema_migrations VALUES (5);
COMMIT;
