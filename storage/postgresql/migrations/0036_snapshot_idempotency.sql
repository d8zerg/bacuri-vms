BEGIN;
ALTER TABLE bacuri_access.access_audit
    ADD COLUMN idempotency_key uuid,
    ADD CONSTRAINT access_audit_idempotency UNIQUE (site_id, actor, action, idempotency_key);
UPDATE bacuri_access.access_audit SET idempotency_key=id WHERE action='snapshot.capture';
INSERT INTO bacuri_meta.schema_migrations VALUES (36);
COMMIT;
