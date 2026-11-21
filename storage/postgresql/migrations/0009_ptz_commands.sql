BEGIN;
SET LOCAL search_path = bacuri_inventory, pg_catalog;
CREATE TABLE ptz_commands (
    site_id uuid NOT NULL REFERENCES sites,
    user_id uuid NOT NULL,
    request_key uuid NOT NULL,
    device_id uuid NOT NULL,
    point_id uuid NOT NULL,
    command text NOT NULL CHECK (command IN ('left','right','up','down','stop')),
    started_at timestamptz NOT NULL DEFAULT clock_timestamp(),
    lease_until timestamptz NOT NULL DEFAULT clock_timestamp() + interval '5 seconds',
    status integer,
    failure_code text,
    PRIMARY KEY (site_id,user_id,request_key)
);
CREATE INDEX ptz_last_command ON ptz_commands(site_id,device_id,started_at DESC);
CREATE INDEX ptz_expiry ON ptz_commands(started_at);
INSERT INTO schema_migrations VALUES (9);
COMMIT;
