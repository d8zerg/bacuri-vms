BEGIN;
SET LOCAL search_path = bacuri_inventory, pg_catalog;

CREATE TABLE archive_recovery (
    site_id uuid PRIMARY KEY REFERENCES sites,
    reason text NOT NULL CHECK (reason IN ('awaiting_verification','verifying','volume_unavailable'))
);

CREATE OR REPLACE VIEW media_rights AS
SELECT m.id,m.browser,m.site_id,m.point_id,m.begin_us,m.end_us,m.request_key,m.expires_at,m.closed,b.user_id,
    least(m.expires_at,b.expires_at,b.last_active+interval '30 minutes') AS valid_until
FROM media_sessions m JOIN browser_sessions b ON b.token_hash=m.browser
JOIN visible_points v ON (v.user_id,v.site_id,v.point_id)=(b.user_id,m.site_id,m.point_id)
WHERE m.begin_us IS NULL OR NOT EXISTS (SELECT FROM archive_recovery r WHERE r.site_id=m.site_id);

INSERT INTO schema_migrations VALUES (7);
COMMIT;
