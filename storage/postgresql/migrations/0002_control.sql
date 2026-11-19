BEGIN;
SET LOCAL search_path = bacuri_inventory, pg_catalog;

CREATE TABLE local_users (
    user_id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
    username text UNIQUE NOT NULL,
    password_hash text NOT NULL
);
CREATE TABLE site_roles (
    user_id uuid REFERENCES local_users,
    site_id uuid REFERENCES sites,
    role text NOT NULL CHECK (role IN ('administrator', 'operator')),
    PRIMARY KEY (user_id, site_id)
);
CREATE TABLE point_grants (
    user_id uuid NOT NULL,
    site_id uuid NOT NULL,
    point_id uuid NOT NULL,
    PRIMARY KEY (user_id, site_id, point_id),
    FOREIGN KEY (user_id, site_id) REFERENCES site_roles,
    FOREIGN KEY (site_id, point_id) REFERENCES observation_points
);
CREATE TABLE browser_sessions (
    token_hash text PRIMARY KEY,
    user_id uuid NOT NULL REFERENCES local_users,
    expires_at timestamptz NOT NULL,
    last_active timestamptz NOT NULL DEFAULT now()
);
CREATE TABLE login_attempts (
    client_address inet PRIMARY KEY,
    started_at timestamptz NOT NULL,
    attempts integer NOT NULL
);
CREATE TABLE camera_registry (
    site_id uuid NOT NULL,
    device_id uuid NOT NULL,
    point_id uuid NOT NULL,
    secret_revision bigint,
    sealed_credentials text,
    PRIMARY KEY (site_id, device_id),
    UNIQUE (site_id, point_id),
    FOREIGN KEY (site_id, device_id) REFERENCES devices,
    FOREIGN KEY (site_id, point_id) REFERENCES observation_points,
    CHECK ((secret_revision IS NULL) = (sealed_credentials IS NULL))
);
CREATE TABLE recorder_delivery (
    site_id uuid PRIMARY KEY REFERENCES sites,
    desired_revision bigint NOT NULL CHECK (desired_revision > 0),
    decided_revision bigint NOT NULL DEFAULT 0
);
CREATE TABLE recorder_attempt (
    site_id uuid PRIMARY KEY REFERENCES recorder_delivery,
    generation bigint NOT NULL,
    sealed_snapshot text NOT NULL,
    revisions jsonb NOT NULL
);
CREATE TABLE registration_requests (
    site_id uuid NOT NULL REFERENCES sites,
    user_id uuid NOT NULL REFERENCES local_users,
    request_key uuid NOT NULL,
    fingerprint text NOT NULL,
    response jsonb NOT NULL,
    expires_at timestamptz NOT NULL,
    PRIMARY KEY (site_id, user_id, request_key)
);
CREATE TABLE configuration_audit (
    request_id uuid PRIMARY KEY,
    user_id uuid NOT NULL REFERENCES local_users,
    site_id uuid NOT NULL,
    device_id uuid NOT NULL,
    action text NOT NULL,
    previous_revision bigint,
    revision bigint NOT NULL,
    outcome text NOT NULL,
    recorded_at timestamptz NOT NULL DEFAULT now(),
    FOREIGN KEY (site_id, device_id) REFERENCES devices
);
CREATE VIEW camera_state AS
SELECT r.site_id, r.device_id, r.point_id, jsonb_build_object(
    'id', r.device_id, 'point_id', r.point_id, 'name', d.name,
    'main_url', v.settings->>'main_url', 'secondary_url', v.settings->>'secondary_url',
    'has_credentials', r.sealed_credentials IS NOT NULL,
    'desired_revision', p.desired_revision::text, 'applied_revision', p.applied_revision::text,
    'state', CASE p.application_state WHEN 'pending' THEN 'applying' WHEN 'failed' THEN 'rejected' ELSE 'applied' END
) || CASE WHEN p.failure_code IS NULL THEN '{}'::jsonb ELSE jsonb_build_object('apply_failure',p.failure_code) END AS body
FROM camera_registry r JOIN devices d USING (site_id, device_id)
JOIN configuration_progress p USING (site_id, device_id)
JOIN configuration_versions v ON (v.site_id,v.device_id,v.revision)=(p.site_id,p.device_id,p.desired_revision);

INSERT INTO schema_migrations VALUES (2);
COMMIT;
