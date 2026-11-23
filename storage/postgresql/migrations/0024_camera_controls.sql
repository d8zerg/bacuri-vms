BEGIN;
ALTER TABLE bacuri_inventory.ptz_commands DROP CONSTRAINT ptz_commands_command_check;
ALTER TABLE bacuri_inventory.ptz_commands ADD CHECK (command IN
    ('left','right','up','down','stop','zoom_in','zoom_out','focus_near','focus_far','focus_stop',
     'preset_goto','tour_start','tour_stop','light_on','light_off','wiper_on','wiper_off','relay_on','relay_off'));
ALTER TABLE bacuri_inventory.ptz_commands ADD COLUMN target text NOT NULL DEFAULT '';
ALTER TABLE bacuri_inventory.ptz_commands ADD COLUMN priority integer NOT NULL DEFAULT 1;
ALTER TABLE bacuri_inventory.ptz_commands ADD COLUMN focus_revision bigint, ADD COLUMN stop_after timestamptz;
CREATE INDEX focus_stop_due ON bacuri_inventory.ptz_commands(site_id,stop_after) WHERE stop_after IS NOT NULL;
CREATE TABLE bacuri_inventory.digital_input_states (
    site_id uuid NOT NULL, device_id uuid NOT NULL, revision bigint NOT NULL, token text NOT NULL,
    active boolean NOT NULL, source_time bigint NOT NULL, observed_at timestamptz NOT NULL DEFAULT clock_timestamp(),
    PRIMARY KEY(site_id,device_id,revision,token),
    FOREIGN KEY(site_id,device_id) REFERENCES bacuri_inventory.camera_registry(site_id,device_id) ON DELETE CASCADE
);
ALTER TABLE bacuri_access.scoped_grants DROP CONSTRAINT scoped_grants_action_check;
ALTER TABLE bacuri_access.scoped_grants ADD CHECK (action IN
    ('view','audio','audio_talk','audio_record','ptz','configure','protect','export'));
CREATE TABLE bacuri_inventory.camera_talk_sessions (
    site_id uuid NOT NULL, device_id uuid NOT NULL, user_id uuid NOT NULL, session_id uuid NOT NULL,
    last_sequence integer NOT NULL, expires_at timestamptz NOT NULL, deadline timestamptz NOT NULL,
    PRIMARY KEY(site_id,device_id),FOREIGN KEY(site_id,device_id) REFERENCES bacuri_inventory.camera_registry ON DELETE CASCADE
);
ALTER TABLE bacuri_events.alert_audit DROP CONSTRAINT alert_audit_action_check,
    ADD CHECK (action IN ('claim','record_result','close','delegate','revoke','export','camera_control')),
    DROP CONSTRAINT alert_audit_outcome_check,
    ADD CHECK (outcome IN ('applied','denied','conflict') OR
        (action='camera_control' AND outcome IN ('dispatched','unknown','rejected')));
INSERT INTO bacuri_meta.schema_migrations VALUES (24);
COMMIT;
