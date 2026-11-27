BEGIN;
SET LOCAL search_path=pg_catalog,pg_temp;
ALTER TABLE bacuri_events.alert_audit DROP CONSTRAINT alert_audit_action_check;
ALTER TABLE bacuri_events.alert_audit ADD CHECK(action IN
 ('claim','record_result','close','delegate','revoke','export','camera_control','recording_policy','manual_recording','recording_storage','archive_hold','case_change','video_rule','node_acceleration'));
INSERT INTO bacuri_meta.schema_migrations VALUES (34);
COMMIT;
