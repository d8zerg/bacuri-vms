BEGIN;
SET LOCAL search_path = bacuri_events,bacuri_inventory,pg_catalog;

ALTER TABLE alerts
    ADD COLUMN work_state text NOT NULL DEFAULT 'new' CHECK (work_state IN ('new','in_progress','closed')),
    ADD COLUMN work_version integer NOT NULL DEFAULT 0 CHECK (work_version >= 0),
    ADD COLUMN owner_id uuid REFERENCES local_users,
    ADD COLUMN work_result text NOT NULL DEFAULT '',
    ADD CHECK ((owner_id IS NULL) = (work_state = 'new')),
    ADD CHECK (work_state <> 'closed' OR work_result <> '');

ALTER TABLE access_versions ADD COLUMN work_revision bigint NOT NULL DEFAULT 0;

CREATE TABLE alert_delegations (
    delegation_id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
    site_id uuid NOT NULL,
    point_id uuid NOT NULL,
    issuer_id uuid NOT NULL REFERENCES local_users,
    service_name text NOT NULL,
    action text NOT NULL CHECK (action IN ('claim','record_result','close')),
    token_hash text NOT NULL UNIQUE,
    expires_at timestamptz NOT NULL,
    revoked_at timestamptz,
    FOREIGN KEY (site_id,point_id) REFERENCES observation_points
);
CREATE INDEX alert_delegations_by_issuer ON alert_delegations(site_id,issuer_id,point_id);

CREATE TABLE alert_audit (
    event_id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
    site_id uuid NOT NULL REFERENCES sites,
    point_id uuid,
    alert_id uuid,
    initiator_id uuid NOT NULL REFERENCES local_users,
    delegation_id uuid REFERENCES alert_delegations,
    actor_kind text NOT NULL CHECK (actor_kind IN ('user','service')),
    action text NOT NULL CHECK (action IN ('claim','record_result','close','delegate','revoke')),
    outcome text NOT NULL CHECK (outcome IN ('applied','denied','conflict')),
    reason text NOT NULL,
    from_state text,
    to_state text,
    owner_id uuid REFERENCES local_users,
    version integer,
    request_key uuid,
    recorded_at timestamptz NOT NULL DEFAULT clock_timestamp()
);
CREATE UNIQUE INDEX alert_command_replays ON alert_audit(site_id,initiator_id,
    (CASE WHEN actor_kind='service' THEN delegation_id END),request_key) NULLS NOT DISTINCT
    WHERE request_key IS NOT NULL AND outcome='applied';
CREATE INDEX alert_audit_by_time ON alert_audit(site_id,recorded_at DESC,event_id DESC);
CREATE INDEX alert_audit_by_alert ON alert_audit(site_id,alert_id,version DESC) WHERE outcome='applied';
CREATE VIEW alert_audit_details AS
SELECT e.*, jsonb_build_object('id',event_id,'alert_id',alert_id,'point_id',e.point_id,
    'initiator',u.username,'initiator_id',initiator_id,'actor_kind',actor_kind,'delegation_id',e.delegation_id,
    'service',d.service_name,'delegated_action',d.action,'expires_at',
    to_char(d.expires_at AT TIME ZONE 'UTC','YYYY-MM-DD"T"HH24:MI:SS.US"Z"'),
    'action',e.action,'outcome',outcome,'reason',reason,'owner',o.username,
    'from_state',from_state,'to_state',to_state,'version',version,
    'recorded_at',to_char(recorded_at AT TIME ZONE 'UTC','YYYY-MM-DD"T"HH24:MI:SS.US"Z"')) AS body
FROM alert_audit e JOIN local_users u ON u.user_id=e.initiator_id
LEFT JOIN local_users o ON o.user_id=e.owner_id LEFT JOIN alert_delegations d USING(delegation_id);

CREATE FUNCTION protect_alert_audit() RETURNS trigger LANGUAGE plpgsql AS $$
BEGIN
    RAISE EXCEPTION 'Alert audit entries are append-only';
END $$;
CREATE TRIGGER alert_audit_immutable BEFORE UPDATE OR DELETE OR TRUNCATE ON alert_audit
FOR EACH STATEMENT EXECUTE FUNCTION protect_alert_audit();

INSERT INTO schema_migrations VALUES (11);
COMMIT;
