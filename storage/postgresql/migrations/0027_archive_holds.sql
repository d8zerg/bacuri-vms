BEGIN;
SET LOCAL search_path=pg_catalog,pg_temp;
CREATE TABLE bacuri_archive.archive_holds (
    site_id uuid NOT NULL, id uuid NOT NULL, point_id uuid NOT NULL,
    begin_us bigint NOT NULL, end_us bigint NOT NULL CHECK (end_us>begin_us),
    owner_id uuid NOT NULL, reason text NOT NULL CHECK (length(trim(reason)) BETWEEN 1 AND 512),
    expires_us bigint NOT NULL, PRIMARY KEY(site_id,id)
);
CREATE TABLE bacuri_archive.held_blocks (
    site_id uuid NOT NULL, hold_id uuid NOT NULL, block_id uuid NOT NULL,
    PRIMARY KEY(site_id,hold_id,block_id),
    FOREIGN KEY(site_id,hold_id) REFERENCES bacuri_archive.archive_holds(site_id,id) ON DELETE CASCADE
);
CREATE INDEX held_by_block ON bacuri_archive.held_blocks(site_id,block_id);
CREATE INDEX holds_by_point ON bacuri_archive.archive_holds(site_id,point_id,expires_us);
CREATE FUNCTION bacuri_archive.protect_new_block() RETURNS trigger LANGUAGE plpgsql AS $$
BEGIN
    INSERT INTO bacuri_archive.held_blocks
    SELECT NEW.site_id,h.id,NEW.block_id FROM bacuri_archive.archive_holds h
    WHERE h.site_id=NEW.site_id AND h.point_id=NEW.point_id AND h.expires_us>bacuri_archive.retention_now_us()
      AND h.begin_us<NEW.end_utc_us AND NEW.begin_utc_us<h.end_us ON CONFLICT DO NOTHING;
    RETURN NULL;
END $$;
CREATE TRIGGER retain_new_material AFTER INSERT ON bacuri_archive.blocks FOR EACH ROW EXECUTE FUNCTION bacuri_archive.protect_new_block();
CREATE FUNCTION bacuri_archive.hold_until(s uuid, block uuid) RETURNS bigint LANGUAGE sql STABLE AS $$
    SELECT max(h.expires_us) FROM bacuri_archive.held_blocks b JOIN bacuri_archive.archive_holds h
    ON (h.site_id,h.id)=(b.site_id,b.hold_id) WHERE b.site_id=s AND b.block_id=block
$$;
CREATE OR REPLACE VIEW bacuri_archive.retained_blocks AS SELECT * FROM bacuri_archive.blocks
 WHERE end_utc_us>bacuri_archive.point_cutoff(site_id,point_id)
 OR bacuri_archive.hold_until(site_id,block_id)>bacuri_archive.retention_now_us();
CREATE OR REPLACE FUNCTION bacuri_archive.reap_expired(s uuid, cutoff bigint) RETURNS void LANGUAGE sql AS $$
 DELETE FROM bacuri_archive.archive_holds WHERE site_id=s AND expires_us<=bacuri_archive.retention_now_us();
 DELETE FROM bacuri_archive.blocks WHERE ctid IN (
    SELECT b.ctid FROM bacuri_archive.blocks b WHERE site_id=s
    AND end_utc_us<=bacuri_archive.point_cutoff(site_id,point_id)
    AND NOT EXISTS (SELECT FROM bacuri_archive.held_blocks h WHERE (h.site_id,h.block_id)=(b.site_id,b.block_id))
    ORDER BY end_utc_us LIMIT 512);
 DELETE FROM bacuri_archive.gaps WHERE ctid IN (
    SELECT g.ctid FROM bacuri_archive.gaps g WHERE site_id=s AND end_utc_us<=bacuri_archive.point_cutoff(site_id,point_id)
    ORDER BY end_utc_us LIMIT 512);
$$;
ALTER TABLE bacuri_access.scoped_grants DROP CONSTRAINT scoped_grants_action_check;
ALTER TABLE bacuri_access.scoped_grants ADD CHECK(action IN
 ('view','audio','audio_talk','audio_record','record','ptz','configure','protect','export','export_masked'));
DO $$
DECLARE constraint_name name;
BEGIN
    SELECT conname INTO STRICT constraint_name FROM pg_constraint
    WHERE conrelid='bacuri_access.scoped_grants'::regclass AND contype='c'
    AND pg_get_constraintdef(oid) LIKE '%begin_us%action%';
    EXECUTE format('ALTER TABLE bacuri_access.scoped_grants DROP CONSTRAINT %I',constraint_name);
END $$;
ALTER TABLE bacuri_access.scoped_grants ADD CHECK ((begin_us IS NULL)=(end_us IS NULL)
    AND (begin_us IS NULL OR (action IN ('export','view','export_masked') AND begin_us<end_us)));
ALTER TABLE bacuri_events.alert_audit DROP CONSTRAINT alert_audit_action_check;
ALTER TABLE bacuri_events.alert_audit ADD CHECK(action IN
 ('claim','record_result','close','delegate','revoke','export','camera_control','recording_policy','manual_recording','recording_storage','archive_hold','case_change','video_rule'));
CREATE TABLE bacuri_archive.motion_indexes (
    site_id uuid NOT NULL, block_id uuid NOT NULL, recording_epoch_id uuid NOT NULL, owner_generation bigint NOT NULL,
    retry_at timestamptz NOT NULL, samples jsonb, PRIMARY KEY(site_id,block_id,recording_epoch_id,owner_generation),
    FOREIGN KEY(site_id,block_id,recording_epoch_id,owner_generation)
      REFERENCES bacuri_archive.blocks(site_id,block_id,recording_epoch_id,owner_generation) ON DELETE CASCADE
);
CREATE INDEX motion_index_retry ON bacuri_archive.motion_indexes(site_id,retry_at) WHERE samples IS NULL;
CREATE TABLE bacuri_archive.investigations (
    site_id uuid NOT NULL, id uuid NOT NULL, number text NOT NULL, owner_id uuid NOT NULL,
    notes text NOT NULL, materials jsonb NOT NULL CHECK(jsonb_typeof(materials)='array'),
    expires_us bigint NOT NULL, revision bigint NOT NULL, PRIMARY KEY(site_id,id), UNIQUE(site_id,number)
);
CREATE TABLE bacuri_events.video_rules (
    site_id uuid NOT NULL, point_id uuid NOT NULL, revision bigint NOT NULL, effective_us bigint NOT NULL,
    author_id uuid NOT NULL, definition jsonb NOT NULL, PRIMARY KEY(site_id,point_id,revision)
);
CREATE TABLE bacuri_events.video_matches (
    site_id uuid NOT NULL, source_id uuid NOT NULL, observation_id uuid NOT NULL, point_id uuid NOT NULL,
    binding bigint NOT NULL, revision bigint NOT NULL, at_us bigint NOT NULL, context jsonb NOT NULL,
    preset_state text NOT NULL CHECK(preset_state IN ('none','pending','dispatching','acknowledged','rejected','unknown')),
    PRIMARY KEY(site_id,source_id,observation_id)
);
CREATE INDEX video_matches_by_point ON bacuri_events.video_matches(site_id,point_id,at_us);
CREATE INDEX pending_preset ON bacuri_events.video_matches(at_us) WHERE preset_state='pending';
INSERT INTO bacuri_meta.schema_migrations VALUES(27);
COMMIT;
