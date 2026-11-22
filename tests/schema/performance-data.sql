\set site '10000000-0000-4000-8000-000000000001'
\set point '20000000-0000-4000-8000-000000000001'
BEGIN;
INSERT INTO bacuri_inventory.sites VALUES (:'site','Performance fixture','UTC');
INSERT INTO bacuri_inventory.observation_points VALUES (:'site',:'point','Archive camera');
INSERT INTO bacuri_inventory.devices
SELECT :'site',(j->>'device_id')::uuid,'Camera' FROM (SELECT :'manifest'::jsonb AS j) f;
INSERT INTO bacuri_inventory.channels
SELECT :'site',(j->>'channel_id')::uuid,(j->>'device_id')::uuid,0 FROM (SELECT :'manifest'::jsonb AS j) f;
INSERT INTO bacuri_inventory.stream_profiles
SELECT :'site',(j->>'profile_id')::uuid,(j->>'channel_id')::uuid,'main' FROM (SELECT :'manifest'::jsonb AS j) f;
INSERT INTO bacuri_inventory.point_bindings
SELECT :'site',:'point',1,(j->>'channel_id')::uuid,0 FROM (SELECT :'manifest'::jsonb AS j) f;
INSERT INTO bacuri_archive.blocks(manifest)
SELECT :'manifest'::jsonb || jsonb_build_object('block_id',md5(n::text)::uuid,'begin_utc_us',n*10,'end_utc_us',n*10+7)
FROM generate_series(1,10000) n;
INSERT INTO bacuri_archive.gaps SELECT :'site',:'point',1,n*10+6,n*10+9,'source_offline' FROM generate_series(1,10000) n;
INSERT INTO bacuri_archive.retired_through VALUES (:'site',50007);

INSERT INTO bacuri_maps.map_sources(id,site_id,kind,state,size_bytes,touched,entries)
SELECT md5(n::text)::uuid,:'site','online',CASE WHEN n=20000 THEN 'pending' ELSE 'ready' END,0,
       now()+n*interval '1 second',jsonb_build_array(jsonb_build_object('id',n::text))
FROM generate_series(1,20000) n;
INSERT INTO bacuri_maps.map_jobs(site_id,source_id,region_id,name,mode,state,size_bytes,next_attempt,created)
SELECT :'site',md5(n::text)::uuid,n::text,'region','install',
       CASE WHEN n BETWEEN 19900 AND 19990 THEN 'paused' WHEN n>=19991 THEN 'queued' ELSE 'installed' END,1024,
       CASE WHEN n>19991 THEN now()+interval '1 day' ELSE now()-interval '1 day' END, now()+n*interval '1 second'
FROM generate_series(1,20000) n;

INSERT INTO bacuri_maps.floor_plans(site_id,plan_id,name,image,width,height)
SELECT :'site',md5(n::text)::uuid,'Plan','',1000,1000 FROM generate_series(1,500) n;
INSERT INTO bacuri_inventory.observation_points SELECT :'site',md5(n::text)::uuid,'Point' FROM generate_series(1,20000) n;
INSERT INTO bacuri_maps.point_placements(site_id,point_id,plan_id,x,y)
SELECT :'site',md5(n::text)::uuid,md5((1+n%500)::text)::uuid,0.5,0.5 FROM generate_series(1,20000) n;
INSERT INTO bacuri_events.observations(site_id,source_id,observation_id,type,schema_version,point_id,binding_revision,
                                       origin,observed_at_utc_us,received_at_utc_us,time_basis,attributes)
SELECT :'site',:'point',md5(n::text)::uuid,'alarm',1,:'point',1,'fixture',n,n,'reception','{}'
FROM generate_series(1,20000) n;
INSERT INTO bacuri_events.alerts(site_id,position,source_id,observation_id,point_id)
SELECT :'site',n,:'point',md5(n::text)::uuid,:'point' FROM generate_series(1,20000) n;
COMMIT;
ANALYZE bacuri_events.alerts;
ANALYZE bacuri_events.observations;
VACUUM ANALYZE bacuri_maps.point_placements;
VACUUM ANALYZE bacuri_maps.map_sources;
VACUUM ANALYZE bacuri_maps.map_jobs;
ANALYZE bacuri_archive.blocks;
ANALYZE bacuri_archive.gaps;
ANALYZE bacuri_archive.retired_through;
