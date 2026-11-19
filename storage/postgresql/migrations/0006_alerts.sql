BEGIN;
SET LOCAL search_path = bacuri_events, bacuri_inventory, pg_catalog;
CREATE TABLE feed_heads (
    site_id uuid PRIMARY KEY REFERENCES sites,
    position bigint NOT NULL CHECK (position > 0)
);
CREATE TABLE alerts (
    site_id uuid NOT NULL,
    position bigint NOT NULL,
    alert_id uuid NOT NULL DEFAULT gen_random_uuid(),
    source_id uuid NOT NULL,
    observation_id uuid NOT NULL,
    point_id uuid NOT NULL,
    PRIMARY KEY (site_id,position),
    UNIQUE (site_id,alert_id),
    UNIQUE (site_id,source_id,observation_id),
    FOREIGN KEY (site_id,source_id,observation_id) REFERENCES observations,
    FOREIGN KEY (site_id,point_id) REFERENCES observation_points
);
CREATE INDEX alerts_by_point ON alerts(site_id,point_id,position DESC);
CREATE TABLE alert_outbox (
    site_id uuid NOT NULL,
    position bigint NOT NULL,
    PRIMARY KEY(site_id,position),
    FOREIGN KEY(site_id,position) REFERENCES alerts
);
CREATE VIEW alert_details AS
SELECT a.*, o.binding_revision,o.type,o.observed_at_utc_us,o.time_basis,o.uncertainty_us
FROM alerts a JOIN observations o USING(site_id,source_id,observation_id);
CREATE TABLE access_versions (
    user_id uuid NOT NULL, site_id uuid NOT NULL, revision bigint NOT NULL,
    PRIMARY KEY(user_id,site_id)
);
CREATE FUNCTION touch_alert_scope() RETURNS trigger LANGUAGE plpgsql AS $$
BEGIN
    IF TG_OP <> 'INSERT' THEN
        INSERT INTO bacuri_events.access_versions VALUES (OLD.user_id,OLD.site_id,1)
        ON CONFLICT(user_id,site_id) DO UPDATE SET revision=bacuri_events.access_versions.revision+1;
    END IF;
    IF TG_OP <> 'DELETE' THEN
        INSERT INTO bacuri_events.access_versions VALUES (NEW.user_id,NEW.site_id,1)
        ON CONFLICT(user_id,site_id) DO UPDATE SET revision=bacuri_events.access_versions.revision+1;
    END IF;
    RETURN NULL;
END $$;
CREATE TRIGGER alert_role_change AFTER INSERT OR UPDATE OR DELETE ON site_roles
FOR EACH ROW EXECUTE FUNCTION touch_alert_scope();
CREATE TRIGGER alert_grant_change AFTER INSERT OR UPDATE OR DELETE ON point_grants
FOR EACH ROW EXECUTE FUNCTION touch_alert_scope();
INSERT INTO schema_migrations VALUES (6);
COMMIT;
