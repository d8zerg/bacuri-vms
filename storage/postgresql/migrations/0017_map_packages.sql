BEGIN;
SET LOCAL statement_timeout=0;
SET LOCAL search_path=pg_catalog,pg_temp;
ALTER TABLE bacuri_maps.map_regions ADD COLUMN region_id text;
UPDATE bacuri_maps.map_regions SET region_id=name;
ALTER TABLE bacuri_maps.map_regions ALTER COLUMN region_id SET NOT NULL;
ALTER TABLE bacuri_maps.map_tiles ADD COLUMN region_id text;
UPDATE bacuri_maps.map_tiles t SET region_id=r.region_id FROM bacuri_maps.map_regions r WHERE t.site_id=r.site_id;
ALTER TABLE bacuri_maps.map_tiles DROP CONSTRAINT map_tiles_site_id_fkey, DROP CONSTRAINT map_tiles_pkey;
ALTER TABLE bacuri_maps.map_regions DROP CONSTRAINT map_regions_pkey, ADD PRIMARY KEY(site_id,region_id);
ALTER TABLE bacuri_maps.map_tiles ADD PRIMARY KEY(site_id,region_id,z,x,y),
    ADD FOREIGN KEY(site_id,region_id) REFERENCES bacuri_maps.map_regions(site_id,region_id) ON DELETE CASCADE;

CREATE TABLE bacuri_maps.map_sources (
    id uuid PRIMARY KEY DEFAULT gen_random_uuid(), site_id uuid NOT NULL,
    kind text NOT NULL CHECK(kind IN ('online','file')), url text NOT NULL DEFAULT '',
    state text NOT NULL CHECK(state IN ('uploading','pending','ready','failed')),
    size_bytes bigint NOT NULL, received bigint NOT NULL DEFAULT 0, entries jsonb NOT NULL DEFAULT '[]',
    error text NOT NULL DEFAULT '', touched timestamptz NOT NULL DEFAULT now()
);
CREATE INDEX map_sources_by_site ON bacuri_maps.map_sources(site_id,touched DESC);
CREATE TABLE bacuri_maps.map_jobs (
    id uuid PRIMARY KEY DEFAULT gen_random_uuid(),site_id uuid NOT NULL,
    source_id uuid NOT NULL REFERENCES bacuri_maps.map_sources ON DELETE CASCADE,
    region_id text NOT NULL, name text NOT NULL, mode text NOT NULL CHECK(mode IN ('install','update')),
    state text NOT NULL CHECK(state IN ('queued','downloading','retrying','paused','installing','installed','failed')),
    size_bytes bigint NOT NULL, received bigint NOT NULL DEFAULT 0, error text NOT NULL DEFAULT '',
    next_attempt timestamptz NOT NULL DEFAULT now(), created timestamptz NOT NULL DEFAULT now()
);
CREATE UNIQUE INDEX map_job_active ON bacuri_maps.map_jobs(site_id,region_id) WHERE state NOT IN ('installed','failed');
CREATE TABLE bacuri_maps.map_catalog_settings(site_id uuid PRIMARY KEY,url text NOT NULL);
INSERT INTO bacuri_meta.schema_migrations VALUES(17);
COMMIT;
