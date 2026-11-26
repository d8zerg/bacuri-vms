BEGIN;
CREATE SCHEMA bacuri_investigations;
REVOKE ALL ON SCHEMA bacuri_investigations FROM PUBLIC;
ALTER TABLE bacuri_archive.investigations SET SCHEMA bacuri_investigations;
INSERT INTO bacuri_meta.schema_migrations VALUES(31);
COMMIT;
