BEGIN;
SET LOCAL search_path=bacuri_inventory,pg_catalog;
CREATE TABLE credential_audit (
    event_id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
    user_id uuid NOT NULL REFERENCES local_users,
    action text NOT NULL CHECK (action IN ('enrolled','revoked','recovery_started','restore_reset')),
    recorded_at timestamptz NOT NULL DEFAULT clock_timestamp()
);
CREATE FUNCTION audit_credential_binding() RETURNS trigger LANGUAGE plpgsql AS $$
BEGIN
    IF TG_OP='INSERT' THEN
        INSERT INTO bacuri_inventory.credential_audit(user_id,action) VALUES(NEW.user_id,'enrolled');
    ELSE
        INSERT INTO bacuri_inventory.credential_audit(user_id,action) VALUES(OLD.user_id,'revoked');
    END IF;
    RETURN NULL;
END $$;
CREATE TRIGGER credential_binding_change AFTER INSERT OR DELETE ON webauthn_credentials
    FOR EACH ROW EXECUTE FUNCTION audit_credential_binding();
INSERT INTO schema_migrations VALUES(15);
COMMIT;
