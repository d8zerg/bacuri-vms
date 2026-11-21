BEGIN;
SET LOCAL search_path = bacuri_inventory, pg_catalog;
CREATE TABLE map_regions (
    site_id uuid PRIMARY KEY REFERENCES sites,
    revision uuid NOT NULL DEFAULT gen_random_uuid(),
    name text NOT NULL,
    source_time timestamptz NOT NULL,
    bounds jsonb NOT NULL,
    min_zoom integer NOT NULL,
    max_zoom integer NOT NULL
);
CREATE TABLE map_tiles (
    site_id uuid REFERENCES map_regions ON DELETE CASCADE,
    z integer, x integer, y integer, image bytea NOT NULL,
    PRIMARY KEY (site_id,z,x,y)
);
CREATE TABLE floor_plans (
    site_id uuid REFERENCES sites, plan_id uuid DEFAULT gen_random_uuid(),
    name text NOT NULL, image bytea NOT NULL, width integer NOT NULL, height integer NOT NULL,
    PRIMARY KEY (site_id,plan_id)
);
CREATE TABLE point_placements (
    site_id uuid NOT NULL, point_id uuid NOT NULL, plan_id uuid,
    x double precision NOT NULL, y double precision NOT NULL,
    FOREIGN KEY (site_id,point_id) REFERENCES observation_points ON DELETE CASCADE,
    FOREIGN KEY (site_id,plan_id) REFERENCES floor_plans ON DELETE CASCADE,
    UNIQUE NULLS NOT DISTINCT (site_id,point_id,plan_id),
    CHECK (CASE WHEN plan_id IS NULL THEN x BETWEEN -180 AND 180 AND y BETWEEN -85.051129 AND 85.051129
           ELSE x BETWEEN 0 AND 1 AND y BETWEEN 0 AND 1 END)
);
CREATE VIEW visible_plans AS
SELECT r.user_id,p.site_id,p.plan_id FROM site_roles r JOIN floor_plans p USING(site_id)
WHERE r.role='administrator' OR EXISTS (
    SELECT FROM point_placements l JOIN visible_points v USING(site_id,point_id)
    WHERE (v.user_id,l.site_id,l.plan_id)=(r.user_id,p.site_id,p.plan_id)
);
INSERT INTO schema_migrations VALUES (10);
COMMIT;
