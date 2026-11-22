BEGIN;
SET LOCAL search_path=pg_catalog,pg_temp;
ALTER TABLE bacuri_maps.point_placements ADD COLUMN calibration jsonb;
CREATE TABLE bacuri_inventory.ptz_telemetry (
    site_id uuid NOT NULL, device_id uuid NOT NULL, revision bigint NOT NULL,
    sample jsonb, observed_at timestamptz, available boolean NOT NULL DEFAULT false,
    next_poll timestamptz NOT NULL DEFAULT now(), rapid_until timestamptz,
    PRIMARY KEY(site_id,device_id),
    FOREIGN KEY(site_id,device_id) REFERENCES bacuri_inventory.devices(site_id,device_id) ON DELETE CASCADE
);
INSERT INTO bacuri_meta.schema_migrations VALUES(18);
COMMIT;
