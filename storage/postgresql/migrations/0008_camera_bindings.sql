BEGIN;
SET LOCAL search_path = bacuri_inventory, pg_catalog;
ALTER VIEW camera_state RENAME TO camera_configuration_state;
CREATE VIEW camera_state AS
SELECT c.site_id, c.device_id, c.point_id,
       c.body || jsonb_build_object('binding_revision', b.binding_revision::text)
              || coalesce(v.settings->'onvif', '{}'::jsonb) AS body
FROM camera_configuration_state c
JOIN configuration_versions v ON (v.site_id,v.device_id,v.revision) =
    (c.site_id,c.device_id,(c.body->>'desired_revision')::bigint)
JOIN LATERAL (SELECT binding_revision FROM point_bindings
              WHERE site_id=c.site_id AND point_id=c.point_id ORDER BY binding_revision DESC LIMIT 1) b ON true;
ALTER TABLE media_sessions ADD COLUMN binding_revision bigint CHECK (binding_revision IS NULL OR (binding_revision > 0 AND begin_us IS NOT NULL));
CREATE INDEX blocks_by_binding_time ON bacuri_archive.blocks(site_id,point_id,binding_revision,begin_utc_us) WHERE unavailable_reason IS NULL;
CREATE TABLE camera_transport_audit (
    site_id uuid NOT NULL REFERENCES sites,
    revision bigint NOT NULL,
    legacy_addresses jsonb NOT NULL,
    applied_at timestamptz NOT NULL DEFAULT clock_timestamp(),
    PRIMARY KEY(site_id,revision)
);
-- The caller holds the site row lock when comparing and appending policy revisions.
CREATE FUNCTION record_camera_transport_policy(uuid,jsonb) RETURNS void LANGUAGE sql AS $$
    INSERT INTO bacuri_inventory.camera_transport_audit(site_id,revision,legacy_addresses)
    SELECT $1,coalesce(last.revision,0)+1,$2 FROM (SELECT 1) seed
    LEFT JOIN LATERAL (SELECT revision,legacy_addresses FROM bacuri_inventory.camera_transport_audit
                       WHERE site_id=$1 ORDER BY revision DESC LIMIT 1) last ON true
    WHERE coalesce(last.legacy_addresses,'[]'::jsonb) <> $2;
$$;
INSERT INTO schema_migrations VALUES (8);
COMMIT;
