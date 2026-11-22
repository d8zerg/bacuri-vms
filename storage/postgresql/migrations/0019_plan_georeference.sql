BEGIN;
SET LOCAL search_path=pg_catalog,pg_temp;
ALTER TABLE bacuri_maps.floor_plans ADD COLUMN georeference jsonb;
ALTER TABLE bacuri_maps.point_placements ADD COLUMN geo_primary boolean CHECK (geo_primary IS NOT TRUE OR plan_id IS NOT NULL);
CREATE UNIQUE INDEX one_geographic_plan_per_point ON bacuri_maps.point_placements(site_id,point_id) WHERE geo_primary;
INSERT INTO bacuri_meta.schema_migrations VALUES(19);
COMMIT;
