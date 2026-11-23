BEGIN;
ALTER TABLE bacuri_media_sessions.media_sessions
  ADD COLUMN presentation_request jsonb NOT NULL DEFAULT '{}',
  ADD COLUMN presentation jsonb NOT NULL DEFAULT '{}';
CREATE TABLE bacuri_media_sessions.monitor_layouts (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  site_id uuid NOT NULL,
  owner_id uuid NOT NULL,
  name text NOT NULL CHECK (length(name) BETWEEN 1 AND 80),
  shared boolean NOT NULL,
  points uuid[] NOT NULL CHECK (cardinality(points) BETWEEN 1 AND 16),
  revision bigint NOT NULL DEFAULT 1
);
CREATE INDEX monitor_layouts_by_site ON bacuri_media_sessions.monitor_layouts(site_id,owner_id);
INSERT INTO bacuri_meta.schema_migrations VALUES (25);
COMMIT;
