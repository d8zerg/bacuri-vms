BEGIN;
SET LOCAL search_path = pg_catalog, pg_temp;

ALTER TABLE bacuri_access.site_roles ADD COLUMN enabled boolean NOT NULL DEFAULT true;
ALTER TABLE bacuri_access.browser_sessions ADD COLUMN login_method text NOT NULL DEFAULT 'local'
    CHECK (login_method IN ('local','oidc')),
    ADD COLUMN oidc_site uuid, ADD COLUMN oidc_until timestamptz, ADD COLUMN sealed_refresh_token text,
    ADD CHECK ((login_method='local') = (oidc_site IS NULL)), ADD CHECK ((login_method='local') = (oidc_until IS NULL)),
    ADD CHECK ((login_method='local') = (sealed_refresh_token IS NULL));

CREATE INDEX browser_oidc_refresh ON bacuri_access.browser_sessions(oidc_site,oidc_until) WHERE oidc_until IS NOT NULL;

CREATE TABLE bacuri_access.resource_groups (
    site_id uuid, group_id uuid DEFAULT gen_random_uuid(),
    name text NOT NULL, PRIMARY KEY(site_id,group_id), UNIQUE(site_id,name)
);
CREATE TABLE bacuri_access.group_points (
    site_id uuid, group_id uuid, point_id uuid,
    PRIMARY KEY(site_id,group_id,point_id),
    FOREIGN KEY(site_id,group_id) REFERENCES bacuri_access.resource_groups ON DELETE CASCADE
);
CREATE TABLE bacuri_access.scoped_grants (
    grant_id uuid PRIMARY KEY, site_id uuid NOT NULL, user_id uuid NOT NULL,
    point_id uuid, group_id uuid, action text NOT NULL CHECK (action IN
        ('view','audio','ptz','configure','protect','export')), begin_us bigint, end_us bigint,
    CHECK (num_nonnulls(point_id,group_id)=1),
    CHECK ((begin_us IS NULL)=(end_us IS NULL) AND (begin_us IS NULL OR (action IN ('export','view') AND begin_us<end_us))),
    FOREIGN KEY(user_id,site_id) REFERENCES bacuri_access.site_roles,
    FOREIGN KEY(site_id,group_id) REFERENCES bacuri_access.resource_groups
);
CREATE INDEX scoped_grants_user ON bacuri_access.scoped_grants(site_id,user_id);
CREATE VIEW bacuri_access.effective_grants AS
    SELECT g.site_id,g.user_id,coalesce(g.point_id,m.point_id) AS point_id,g.action,g.begin_us,g.end_us,g.grant_id
    FROM bacuri_access.scoped_grants g LEFT JOIN bacuri_access.group_points m USING(site_id,group_id)
    UNION ALL SELECT p.site_id,p.user_id,p.point_id,a.action,NULL,NULL,
       md5(p.site_id::text||p.user_id::text||p.point_id::text)::uuid
       FROM bacuri_access.point_grants p CROSS JOIN (VALUES ('view'),('ptz')) a(action)
    UNION ALL SELECT site_id,user_id,point_id,'export',begin_us,end_us,grant_id FROM bacuri_access.export_grants;

CREATE TABLE bacuri_access.privacy_policies (
    site_id uuid, point_id uuid, live boolean NOT NULL, archive boolean NOT NULL,
    thumbnail boolean NOT NULL, export boolean NOT NULL,
    regions jsonb NOT NULL DEFAULT '[]' CHECK (jsonb_typeof(regions)='array' AND jsonb_array_length(regions)<=32),
    PRIMARY KEY(site_id,point_id)
);
CREATE FUNCTION bacuri_access.can_use(s uuid,u uuid,p uuid,operation text,a bigint,b bigint,surface text)
RETURNS boolean LANGUAGE sql STABLE AS $$
    SELECT EXISTS(SELECT FROM bacuri_access.site_roles r WHERE r.site_id=s AND r.user_id=u AND r.enabled
        AND ((r.role='administrator' AND operation <> 'export') OR EXISTS (
            SELECT FROM bacuri_access.effective_grants g WHERE (g.site_id,g.user_id,g.point_id)=(s,u,p)
            AND g.action=operation AND (g.begin_us IS NULL OR (a>=g.begin_us AND b<=g.end_us AND a<b)))))
        AND NOT EXISTS(SELECT FROM bacuri_access.privacy_policies q WHERE (q.site_id,q.point_id)=(s,p)
            AND CASE surface WHEN 'live' THEN q.live WHEN 'archive' THEN q.archive
                WHEN 'thumbnail' THEN q.thumbnail WHEN 'export' THEN q.export ELSE false END)
$$;
CREATE TABLE bacuri_access.identity_providers (
    site_id uuid PRIMARY KEY, enabled boolean NOT NULL,
    issuer text NOT NULL, client_id text NOT NULL, sealed_secret text NOT NULL, admin_acr text NOT NULL
);
CREATE TABLE bacuri_access.external_identities (
    site_id uuid, user_id uuid, issuer text NOT NULL, subject text NOT NULL,
    PRIMARY KEY(site_id,user_id), UNIQUE(issuer,subject),
    FOREIGN KEY(user_id,site_id) REFERENCES bacuri_access.site_roles
);
CREATE TABLE bacuri_access.oidc_requests (
    state_hash text PRIMARY KEY, browser_hash text NOT NULL, verifier text NOT NULL,
    nonce text NOT NULL, site_id uuid REFERENCES bacuri_access.identity_providers ON DELETE CASCADE,
    expires_at timestamptz NOT NULL
);
CREATE TABLE bacuri_access.policy_versions (
    site_id uuid PRIMARY KEY, revision bigint NOT NULL
);
CREATE TABLE bacuri_access.access_audit (
    id uuid PRIMARY KEY DEFAULT gen_random_uuid(), site_id uuid NOT NULL,
    actor text NOT NULL, method text NOT NULL, action text NOT NULL, resource text NOT NULL,
    recorded_at timestamptz NOT NULL DEFAULT clock_timestamp()
);
CREATE INDEX access_audit_site_time ON bacuri_access.access_audit(site_id,recorded_at DESC,id);
CREATE FUNCTION bacuri_access.audit_policy_change() RETURNS trigger LANGUAGE plpgsql AS $$
DECLARE
    row_data jsonb;
    site uuid;
BEGIN
    IF current_setting('bacuri.restoring',true)='yes' THEN RETURN NULL; END IF;
    row_data := CASE WHEN TG_OP='DELETE' THEN to_jsonb(OLD) ELSE to_jsonb(NEW) END;
    site := (row_data->>'site_id')::uuid;
    INSERT INTO bacuri_access.policy_versions VALUES(site,1)
        ON CONFLICT(site_id) DO UPDATE SET revision=policy_versions.revision+1;
    INSERT INTO bacuri_access.access_audit(site_id,actor,method,action,resource)
        VALUES(site,coalesce(nullif(current_setting('bacuri.actor',true),''),'local-console'),
        coalesce(nullif(current_setting('bacuri.auth',true),''),'console'),TG_TABLE_NAME||'.'||lower(TG_OP),
        concat_ws('/',row_data->>'user_id',row_data->>'group_id',row_data->>'point_id',row_data->>'grant_id',
                  CASE WHEN TG_TABLE_NAME='identity_providers' THEN site::text END));
    RETURN NULL;
END $$;
DO $$
DECLARE relation text;
BEGIN
    FOREACH relation IN ARRAY ARRAY['site_roles','point_grants','export_grants','resource_groups','group_points',
        'scoped_grants','privacy_policies','identity_providers','external_identities'] LOOP
        EXECUTE format('CREATE TRIGGER audit_policy AFTER INSERT OR UPDATE OR DELETE ON bacuri_access.%I
            FOR EACH ROW EXECUTE FUNCTION bacuri_access.audit_policy_change()',relation);
    END LOOP;
END $$;
INSERT INTO bacuri_meta.schema_migrations VALUES (21);
COMMIT;
