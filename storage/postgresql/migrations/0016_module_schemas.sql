BEGIN;
SET LOCAL search_path = pg_catalog, pg_temp;

CREATE SCHEMA bacuri_access;
CREATE SCHEMA bacuri_media_sessions;
CREATE SCHEMA bacuri_maps;
CREATE SCHEMA bacuri_meta;

REVOKE ALL ON SCHEMA bacuri_inventory, bacuri_archive, bacuri_events, bacuri_access,
    bacuri_media_sessions, bacuri_maps, bacuri_meta FROM PUBLIC;

DROP VIEW bacuri_inventory.media_rights, bacuri_inventory.visible_plans, bacuri_events.alert_audit_details;
DROP VIEW bacuri_inventory.visible_points;

ALTER TABLE bacuri_inventory.local_users SET SCHEMA bacuri_access;
ALTER TABLE bacuri_inventory.site_roles SET SCHEMA bacuri_access;
ALTER TABLE bacuri_inventory.point_grants SET SCHEMA bacuri_access;
ALTER TABLE bacuri_inventory.browser_sessions SET SCHEMA bacuri_access;
ALTER TABLE bacuri_inventory.login_attempts SET SCHEMA bacuri_access;
ALTER TABLE bacuri_inventory.export_grants SET SCHEMA bacuri_access;
ALTER TABLE bacuri_inventory.webauthn_credentials SET SCHEMA bacuri_access;
ALTER TABLE bacuri_inventory.webauthn_enrollment SET SCHEMA bacuri_access;
ALTER TABLE bacuri_inventory.webauthn_challenges SET SCHEMA bacuri_access;
ALTER TABLE bacuri_inventory.credential_audit SET SCHEMA bacuri_access;
ALTER FUNCTION bacuri_inventory.invalidate_role_sessions() SET SCHEMA bacuri_access;
ALTER FUNCTION bacuri_inventory.audit_credential_binding() SET SCHEMA bacuri_access;

ALTER TABLE bacuri_inventory.media_sessions SET SCHEMA bacuri_media_sessions;

ALTER TABLE bacuri_inventory.map_regions SET SCHEMA bacuri_maps;
ALTER TABLE bacuri_inventory.map_tiles SET SCHEMA bacuri_maps;
ALTER TABLE bacuri_inventory.floor_plans SET SCHEMA bacuri_maps;
ALTER TABLE bacuri_inventory.point_placements SET SCHEMA bacuri_maps;

ALTER TABLE bacuri_inventory.archive_retention SET SCHEMA bacuri_archive;
ALTER TABLE bacuri_inventory.archive_recovery SET SCHEMA bacuri_archive;
ALTER TABLE bacuri_inventory.playback_parts SET SCHEMA bacuri_archive;

ALTER TABLE bacuri_inventory.schema_migrations SET SCHEMA bacuri_meta;

CREATE OR REPLACE FUNCTION bacuri_archive.retention_cutoff(site uuid) RETURNS bigint LANGUAGE sql STABLE AS $$
    SELECT greatest(
        (SELECT end_us FROM bacuri_archive.retired_through WHERE site_id=site),
        (SELECT bacuri_archive.retention_now_us() - days * 86400000000::bigint
         FROM bacuri_archive.archive_retention WHERE site_id=site),
        '-9223372036854775808'::bigint)
$$;

CREATE OR REPLACE FUNCTION bacuri_archive.advance_cutoff(site uuid) RETURNS void LANGUAGE sql AS $$
    INSERT INTO bacuri_archive.retired_through
    SELECT site_id,bacuri_archive.retention_cutoff(site) FROM bacuri_archive.archive_retention WHERE site_id=site
    ON CONFLICT(site_id) DO UPDATE SET end_us=greatest(retired_through.end_us,excluded.end_us)
$$;

CREATE OR REPLACE FUNCTION bacuri_access.invalidate_role_sessions() RETURNS trigger LANGUAGE plpgsql AS $$
BEGIN
    PERFORM user_id FROM bacuri_access.local_users WHERE user_id=coalesce(NEW.user_id,OLD.user_id) FOR UPDATE;
    IF TG_OP <> 'INSERT' THEN
        DELETE FROM bacuri_access.browser_sessions WHERE user_id=OLD.user_id;
        DELETE FROM bacuri_access.webauthn_challenges WHERE user_id=OLD.user_id;
        DELETE FROM bacuri_access.webauthn_enrollment WHERE user_id=OLD.user_id;
    END IF;
    IF TG_OP <> 'DELETE' THEN
        DELETE FROM bacuri_access.browser_sessions WHERE user_id=NEW.user_id;
        DELETE FROM bacuri_access.webauthn_challenges WHERE user_id=NEW.user_id;
        DELETE FROM bacuri_access.webauthn_enrollment WHERE user_id=NEW.user_id;
    END IF;
    RETURN NULL;
END $$;

CREATE OR REPLACE FUNCTION bacuri_access.audit_credential_binding() RETURNS trigger LANGUAGE plpgsql AS $$
BEGIN
    IF TG_OP='INSERT' THEN
        INSERT INTO bacuri_access.credential_audit(user_id,action) VALUES(NEW.user_id,'enrolled');
    ELSE
        INSERT INTO bacuri_access.credential_audit(user_id,action) VALUES(OLD.user_id,'revoked');
    END IF;
    RETURN NULL;
END $$;

INSERT INTO bacuri_meta.schema_migrations VALUES (16);
COMMIT;
