SET search_path = pg_catalog, pg_temp;
CREATE TEMP TABLE original_tables AS
    SELECT c.oid,c.relfilenode FROM pg_class c JOIN pg_namespace n ON n.oid=c.relnamespace
    WHERE c.relkind='r' AND n.nspname IN ('bacuri_inventory','bacuri_events','bacuri_archive');

-- Keep the existing referential and trigger contracts visible when a later migration adds a dependency.
CREATE TEMP VIEW module_links AS
    SELECT 'foreign key' AS kind,k.conname AS name,t.oid AS source,r.oid AS target,
           t.relnamespace AS source_schema,r.relnamespace AS target_schema
    FROM pg_constraint k JOIN pg_class t ON t.oid=k.conrelid JOIN pg_class r ON r.oid=k.confrelid
    WHERE k.contype='f'
    UNION ALL
    SELECT 'trigger',g.tgname,t.oid,f.oid,t.relnamespace,f.pronamespace
    FROM pg_trigger g JOIN pg_class t ON t.oid=g.tgrelid JOIN pg_proc f ON f.oid=g.tgfoid
    WHERE NOT g.tgisinternal;
CREATE TEMP TABLE original_links AS SELECT kind,name,source,target FROM module_links;

DO $$
DECLARE
    site uuid := '10000000-0000-4000-8000-000000000001';
    point uuid;
    account uuid;
    plan uuid := gen_random_uuid();
BEGIN
    SELECT point_id INTO STRICT point FROM bacuri_inventory.observation_points WHERE site_id=site;
    INSERT INTO bacuri_inventory.local_users(username,password_hash) VALUES ('migration-operator','test') RETURNING user_id INTO account;
    INSERT INTO bacuri_inventory.site_roles VALUES (account,site,'operator');
    INSERT INTO bacuri_inventory.point_grants VALUES (account,site,point);
    INSERT INTO bacuri_inventory.webauthn_credentials VALUES (account,'migration-credential','test',1);
    INSERT INTO bacuri_inventory.browser_sessions(token_hash,user_id,expires_at) VALUES ('migration-browser',account,now()+interval '1 hour');
    INSERT INTO bacuri_inventory.media_sessions(browser,site_id,point_id,expires_at) VALUES ('migration-browser',site,point,now()+interval '1 hour');
    INSERT INTO bacuri_inventory.archive_retention VALUES (site,45,20);
    INSERT INTO bacuri_inventory.floor_plans VALUES (site,plan,'Migration plan','x',100,100);
    INSERT INTO bacuri_inventory.point_placements VALUES (site,point,plan,0.5,0.5);
END $$;

\ir ../../storage/postgresql/migrations/0016_module_schemas.sql

DO $$
DECLARE
    site uuid := '10000000-0000-4000-8000-000000000001';
    account uuid;
    old_revision bigint;
BEGIN
    ASSERT (SELECT max(version) FROM bacuri_meta.schema_migrations)=16;
    ASSERT NOT EXISTS (SELECT FROM original_tables t LEFT JOIN pg_class c USING(oid)
                       WHERE c.oid IS NULL OR c.relfilenode<>t.relfilenode), 'table data rewritten or lost';
    ASSERT NOT EXISTS (
        SELECT FROM pg_namespace n, LATERAL aclexplode(coalesce(n.nspacl,acldefault('n',n.nspowner))) acl
        WHERE starts_with(n.nspname,'bacuri_') AND acl.grantee=0
    ), 'public schema privileges';
    SELECT user_id INTO STRICT account FROM bacuri_access.local_users WHERE username='migration-operator';
    ASSERT EXISTS (SELECT FROM bacuri_access.point_grants WHERE user_id=account);
    ASSERT EXISTS (SELECT FROM bacuri_maps.point_placements JOIN bacuri_maps.floor_plans USING(site_id,plan_id) WHERE name='Migration plan');
    ASSERT EXISTS (SELECT FROM bacuri_media_sessions.media_sessions WHERE browser='migration-browser');
    ASSERT EXISTS (SELECT FROM bacuri_access.credential_audit WHERE user_id=account AND action='enrolled');
    ASSERT (SELECT days FROM bacuri_archive.archive_retention WHERE site_id=site)=45;
    PERFORM bacuri_archive.advance_cutoff(site);
    ASSERT (SELECT end_us FROM bacuri_archive.retired_through WHERE site_id=site)=bacuri_archive.retention_cutoff(site);
    DELETE FROM bacuri_access.webauthn_credentials WHERE user_id=account;
    ASSERT EXISTS (SELECT FROM bacuri_access.credential_audit WHERE user_id=account AND action='revoked');
    SELECT revision INTO STRICT old_revision FROM bacuri_events.access_versions WHERE site_id=site AND user_id=account;
    UPDATE bacuri_access.site_roles SET role='administrator' WHERE site_id=site AND user_id=account;
    ASSERT (SELECT revision FROM bacuri_events.access_versions WHERE site_id=site AND user_id=account)>old_revision;
    ASSERT NOT EXISTS (SELECT FROM bacuri_access.browser_sessions WHERE user_id=account), 'role change must revoke the session';
    ASSERT NOT EXISTS (SELECT FROM bacuri_media_sessions.media_sessions WHERE browser='migration-browser'), 'revocation cascade';
END $$;
