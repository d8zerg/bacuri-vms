BEGIN;
SET LOCAL search_path = bacuri_inventory,pg_catalog;
CREATE TABLE export_grants (
    user_id uuid NOT NULL REFERENCES local_users,
    site_id uuid NOT NULL,
    point_id uuid NOT NULL,
    begin_us bigint NOT NULL,
    end_us bigint NOT NULL CHECK (end_us > begin_us),
    grant_id uuid NOT NULL UNIQUE DEFAULT gen_random_uuid(),
    PRIMARY KEY(user_id,site_id,point_id),
    FOREIGN KEY(site_id,point_id) REFERENCES observation_points
);
ALTER TABLE bacuri_events.alert_audit DROP CONSTRAINT alert_audit_action_check,
    ADD COLUMN export_details jsonb,
    ADD CHECK ((action = 'export') = (export_details IS NOT NULL)),
    ADD CHECK (action IN ('claim','record_result','close','delegate','revoke','export'));
INSERT INTO schema_migrations VALUES (13);
COMMIT;
