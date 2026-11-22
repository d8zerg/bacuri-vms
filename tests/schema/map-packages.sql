INSERT INTO bacuri_maps.map_regions(site_id,revision,name,source_time,bounds,min_zoom,max_zoom)
SELECT site_id,'20000000-0000-4000-8000-000000000001','Original region',now(),'[1,2,3,4]',0,1
FROM bacuri_inventory.sites LIMIT 1;
INSERT INTO bacuri_maps.map_tiles SELECT site_id,0,0,0,'tile'::bytea FROM bacuri_maps.map_regions;
\ir ../../storage/postgresql/migrations/0017_map_packages.sql
DO $$
BEGIN
    ASSERT EXISTS (SELECT FROM bacuri_maps.map_regions JOIN bacuri_maps.map_tiles USING(site_id,region_id)
                   WHERE region_id='Original region' AND revision='20000000-0000-4000-8000-000000000001'
                     AND image='tile'::bytea);
    ASSERT (SELECT max(version) FROM bacuri_meta.schema_migrations)=17;
END $$;
