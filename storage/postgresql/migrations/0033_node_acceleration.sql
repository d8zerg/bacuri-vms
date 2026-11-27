BEGIN;
SET LOCAL search_path=pg_catalog,pg_temp;
CREATE TABLE bacuri_inventory.node_acceleration (
    site_id uuid NOT NULL REFERENCES bacuri_inventory.sites(site_id),
    node_id uuid NOT NULL,
    mode text NOT NULL DEFAULT 'cpu_only' CHECK(mode IN ('cpu_only','manual','auto')),
    gpu_tasks text[] NOT NULL DEFAULT '{}' CHECK(gpu_tasks <@ ARRAY[
        'live_mask','live_profile','live_analysis','export','archive_analysis','keyframe','thumbnail']::text[]),
    revision bigint NOT NULL DEFAULT 0 CHECK(revision >= 0),
    PRIMARY KEY(site_id,node_id),
    CHECK(mode = 'manual' OR cardinality(gpu_tasks) = 0)
);
ALTER TABLE bacuri_events.alert_audit DROP CONSTRAINT alert_audit_action_check;
ALTER TABLE bacuri_events.alert_audit ADD CHECK(action IN
 ('claim','record_result','close','delegate','revoke','export','camera_control','recording_policy','manual_recording','recording_storage','archive_hold','case_change','video_rule','node_acceleration'));
INSERT INTO bacuri_meta.schema_migrations VALUES (33);
COMMIT;
