BEGIN;
SET LOCAL search_path = bacuri_inventory, pg_catalog;
CREATE TABLE media_sessions (
    id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
    browser text NOT NULL REFERENCES browser_sessions(token_hash) ON DELETE CASCADE,
    site_id uuid NOT NULL, point_id uuid NOT NULL,
    begin_us bigint NOT NULL, end_us bigint NOT NULL CHECK (begin_us < end_us),
    request_key uuid, expires_at timestamptz NOT NULL, closed boolean NOT NULL DEFAULT false,
    UNIQUE(browser,site_id,point_id,request_key),
    FOREIGN KEY(site_id,point_id) REFERENCES observation_points
);
CREATE INDEX media_expiry ON media_sessions(expires_at);
CREATE TABLE playback_parts (
    session_id uuid NOT NULL REFERENCES media_sessions ON DELETE CASCADE,
    init_id uuid NOT NULL DEFAULT gen_random_uuid(), media_id uuid NOT NULL DEFAULT gen_random_uuid(),
    site_id uuid NOT NULL, block_id uuid NOT NULL, recording_epoch_id uuid NOT NULL, owner_generation bigint NOT NULL,
    PRIMARY KEY(session_id, media_id), UNIQUE(session_id, init_id),
    FOREIGN KEY(site_id,block_id,recording_epoch_id,owner_generation) REFERENCES bacuri_archive.blocks
);
CREATE INDEX blocks_by_interval ON bacuri_archive.blocks USING gist(int8range(begin_utc_us,end_utc_us,'[)'));
CREATE INDEX gaps_by_interval ON bacuri_archive.gaps USING gist(int8range(begin_utc_us,end_utc_us,'[)'));
CREATE VIEW visible_points AS
SELECT r.user_id,r.role,p.site_id,p.point_id FROM site_roles r JOIN observation_points p USING(site_id)
WHERE r.role='administrator' OR EXISTS(SELECT FROM point_grants g WHERE
(g.user_id,g.site_id,g.point_id)=(r.user_id,p.site_id,p.point_id));
CREATE VIEW media_rights AS
SELECT m.*, b.user_id,
    least(m.expires_at,b.expires_at,b.last_active+interval '30 minutes') AS valid_until
FROM media_sessions m JOIN browser_sessions b ON b.token_hash=m.browser
JOIN visible_points v ON (v.user_id,v.site_id,v.point_id)=(b.user_id,m.site_id,m.point_id);
INSERT INTO schema_migrations VALUES (4);
COMMIT;
