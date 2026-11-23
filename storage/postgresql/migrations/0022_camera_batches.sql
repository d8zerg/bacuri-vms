BEGIN;
CREATE TABLE bacuri_inventory.camera_batch_items (
    site_id uuid NOT NULL REFERENCES bacuri_inventory.sites,
    user_id uuid NOT NULL,
    batch_id uuid NOT NULL,
    item_id uuid NOT NULL,
    fingerprint text NOT NULL,
    sealed_plan text NOT NULL,
    receipt jsonb NOT NULL,
    created_at timestamptz NOT NULL DEFAULT clock_timestamp(),
    PRIMARY KEY (site_id,user_id,batch_id,item_id)
);
CREATE TABLE bacuri_inventory.camera_templates (
    site_id uuid NOT NULL REFERENCES bacuri_inventory.sites,
    template_id uuid NOT NULL,
    revision bigint NOT NULL CHECK (revision > 0),
    user_id uuid NOT NULL,
    settings jsonb NOT NULL,
    PRIMARY KEY (site_id, template_id, revision)
);
INSERT INTO bacuri_meta.schema_migrations VALUES (22);
COMMIT;
