BEGIN;
SET LOCAL search_path=bacuri_inventory,pg_catalog;
CREATE TABLE webauthn_credentials (
    user_id uuid PRIMARY KEY REFERENCES local_users,
    credential_id text NOT NULL UNIQUE,
    public_key text NOT NULL,
    counter bigint NOT NULL CHECK(counter BETWEEN 0 AND 4294967295)
);
CREATE TABLE webauthn_enrollment (
    user_id uuid PRIMARY KEY REFERENCES local_users,
    code_hash text NOT NULL,
    expires_at timestamptz NOT NULL
);
CREATE TABLE webauthn_challenges (
    challenge text PRIMARY KEY,
    ticket_hash text NOT NULL UNIQUE,
    user_id uuid NOT NULL REFERENCES local_users,
    enrolling boolean NOT NULL,
    expires_at timestamptz NOT NULL
);
CREATE INDEX webauthn_challenge_expiry ON webauthn_challenges(expires_at);
DELETE FROM browser_sessions WHERE user_id IN (SELECT user_id FROM site_roles WHERE role='administrator');
CREATE FUNCTION invalidate_role_sessions() RETURNS trigger LANGUAGE plpgsql AS $$
BEGIN
    PERFORM user_id FROM bacuri_inventory.local_users WHERE user_id=coalesce(NEW.user_id,OLD.user_id) FOR UPDATE;
    IF TG_OP <> 'INSERT' THEN
        DELETE FROM bacuri_inventory.browser_sessions WHERE user_id=OLD.user_id;
        DELETE FROM bacuri_inventory.webauthn_challenges WHERE user_id=OLD.user_id;
        DELETE FROM bacuri_inventory.webauthn_enrollment WHERE user_id=OLD.user_id;
    END IF;
    IF TG_OP <> 'DELETE' THEN
        DELETE FROM bacuri_inventory.browser_sessions WHERE user_id=NEW.user_id;
        DELETE FROM bacuri_inventory.webauthn_challenges WHERE user_id=NEW.user_id;
        DELETE FROM bacuri_inventory.webauthn_enrollment WHERE user_id=NEW.user_id;
    END IF;
    RETURN NULL;
END $$;
CREATE TRIGGER role_session_change AFTER INSERT OR UPDATE OR DELETE ON site_roles
    FOR EACH ROW EXECUTE FUNCTION invalidate_role_sessions();
INSERT INTO schema_migrations VALUES(14);
COMMIT;
