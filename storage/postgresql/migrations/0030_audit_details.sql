BEGIN;
SET LOCAL search_path=pg_catalog,pg_temp;
-- A journal entry names its cause by a code and keeps its values apart, so the client words it in the interface
-- language.
ALTER TABLE bacuri_events.alert_audit ADD COLUMN details jsonb;
INSERT INTO bacuri_meta.schema_migrations VALUES(30);
COMMIT;
