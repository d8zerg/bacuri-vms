BEGIN;

CREATE SCHEMA bacuri_inventory;
CREATE SCHEMA bacuri_archive;
CREATE SCHEMA bacuri_events;

CREATE TABLE bacuri_inventory.schema_migrations (
    version integer PRIMARY KEY CHECK (version > 0)
);

CREATE TABLE bacuri_inventory.sites (
    site_id uuid PRIMARY KEY,
    name text NOT NULL CHECK (btrim(name) <> ''),
    time_zone text NOT NULL CHECK (btrim(time_zone) <> '')
);

CREATE TABLE bacuri_inventory.observation_points (
    site_id uuid NOT NULL REFERENCES bacuri_inventory.sites,
    point_id uuid NOT NULL,
    name text NOT NULL CHECK (btrim(name) <> ''),
    PRIMARY KEY (site_id, point_id)
);

CREATE TABLE bacuri_inventory.devices (
    site_id uuid NOT NULL REFERENCES bacuri_inventory.sites,
    device_id uuid NOT NULL,
    name text NOT NULL CHECK (btrim(name) <> ''),
    PRIMARY KEY (site_id, device_id)
);

CREATE TABLE bacuri_inventory.channels (
    site_id uuid NOT NULL,
    channel_id uuid NOT NULL,
    device_id uuid NOT NULL,
    channel_index integer NOT NULL CHECK (channel_index >= 0),
    PRIMARY KEY (site_id, channel_id),
    UNIQUE (site_id, channel_id, device_id),
    UNIQUE (site_id, device_id, channel_index),
    FOREIGN KEY (site_id, device_id) REFERENCES bacuri_inventory.devices
);

CREATE TABLE bacuri_inventory.stream_profiles (
    site_id uuid NOT NULL,
    profile_id uuid NOT NULL,
    channel_id uuid NOT NULL,
    stream_role text NOT NULL CHECK (stream_role IN ('main', 'secondary')),
    PRIMARY KEY (site_id, profile_id),
    UNIQUE (site_id, profile_id, channel_id),
    UNIQUE (site_id, channel_id, stream_role),
    FOREIGN KEY (site_id, channel_id) REFERENCES bacuri_inventory.channels
);

CREATE TABLE bacuri_inventory.point_bindings (
    site_id uuid NOT NULL,
    point_id uuid NOT NULL,
    binding_revision bigint NOT NULL CHECK (binding_revision > 0),
    channel_id uuid NOT NULL,
    valid_from_utc_us bigint NOT NULL,
    PRIMARY KEY (site_id, point_id, binding_revision),
    UNIQUE (site_id, point_id, valid_from_utc_us),
    UNIQUE (site_id, point_id, binding_revision, channel_id),
    FOREIGN KEY (site_id, point_id) REFERENCES bacuri_inventory.observation_points,
    FOREIGN KEY (site_id, channel_id) REFERENCES bacuri_inventory.channels
);

CREATE TABLE bacuri_inventory.configuration_versions (
    site_id uuid NOT NULL,
    device_id uuid NOT NULL,
    revision bigint NOT NULL CHECK (revision > 0),
    settings jsonb NOT NULL CHECK (jsonb_typeof(settings) = 'object'),
    PRIMARY KEY (site_id, device_id, revision),
    FOREIGN KEY (site_id, device_id) REFERENCES bacuri_inventory.devices
);

CREATE TABLE bacuri_inventory.configuration_progress (
    site_id uuid NOT NULL,
    device_id uuid NOT NULL,
    desired_revision bigint NOT NULL,
    applied_revision bigint,
    application_state text NOT NULL CHECK (application_state IN ('pending', 'applied', 'failed')),
    failure_code text CHECK (btrim(failure_code) <> ''),
    PRIMARY KEY (site_id, device_id),
    FOREIGN KEY (site_id, device_id, desired_revision) REFERENCES bacuri_inventory.configuration_versions,
    FOREIGN KEY (site_id, device_id, applied_revision) REFERENCES bacuri_inventory.configuration_versions,
    CHECK (applied_revision <= desired_revision),
    CHECK ((application_state = 'applied') = coalesce(applied_revision = desired_revision, false)),
    CHECK ((application_state = 'failed') = (failure_code IS NOT NULL))
);

CREATE TABLE bacuri_archive.blocks (
    manifest jsonb NOT NULL,
    site_id uuid GENERATED ALWAYS AS ((manifest ->> 'site_id')::uuid) STORED NOT NULL,
    block_id uuid GENERATED ALWAYS AS ((manifest ->> 'block_id')::uuid) STORED NOT NULL,
    recording_epoch_id uuid GENERATED ALWAYS AS ((manifest ->> 'recording_epoch_id')::uuid) STORED NOT NULL,
    owner_generation bigint GENERATED ALWAYS AS ((manifest ->> 'owner_generation')::bigint) STORED NOT NULL,
    recording_node_id uuid GENERATED ALWAYS AS ((manifest ->> 'recording_node_id')::uuid) STORED NOT NULL,
    point_id uuid GENERATED ALWAYS AS ((manifest ->> 'point_id')::uuid) STORED NOT NULL,
    binding_revision bigint GENERATED ALWAYS AS ((manifest ->> 'binding_revision')::bigint) STORED NOT NULL,
    device_id uuid GENERATED ALWAYS AS ((manifest ->> 'device_id')::uuid) STORED NOT NULL,
    channel_id uuid GENERATED ALWAYS AS ((manifest ->> 'channel_id')::uuid) STORED NOT NULL,
    profile_id uuid GENERATED ALWAYS AS ((manifest ->> 'profile_id')::uuid) STORED NOT NULL,
    begin_utc_us bigint GENERATED ALWAYS AS ((manifest ->> 'begin_utc_us')::bigint) STORED NOT NULL,
    end_utc_us bigint GENERATED ALWAYS AS ((manifest ->> 'end_utc_us')::bigint) STORED NOT NULL,
    time_basis text GENERATED ALWAYS AS (manifest #>> '{time,basis}') STORED NOT NULL,
    received_at_utc_us bigint GENERATED ALWAYS AS ((manifest #>> '{time,received_at_utc_us}')::bigint) STORED NOT NULL,
    source_time_us bigint GENERATED ALWAYS AS ((manifest #>> '{time,source_time_us}')::bigint) STORED,
    clock_epoch_id uuid GENERATED ALWAYS AS ((manifest #>> '{time,clock_epoch_id}')::uuid) STORED,
    uncertainty_us bigint GENERATED ALWAYS AS ((manifest #>> '{time,uncertainty_us}')::bigint) STORED,
    PRIMARY KEY (site_id, block_id, recording_epoch_id, owner_generation),
    FOREIGN KEY (site_id, point_id, binding_revision, channel_id)
        REFERENCES bacuri_inventory.point_bindings (site_id, point_id, binding_revision, channel_id),
    FOREIGN KEY (site_id, channel_id, device_id)
        REFERENCES bacuri_inventory.channels (site_id, channel_id, device_id),
    FOREIGN KEY (site_id, profile_id, channel_id)
        REFERENCES bacuri_inventory.stream_profiles (site_id, profile_id, channel_id),
    CHECK (owner_generation > 0),
    CHECK (begin_utc_us < end_utc_us),
    CHECK ((manifest -> 'manifest_version' = '1'::jsonb) IS TRUE),
    CHECK ((
        manifest #>> '{media,file}' = 'media.mp4'
        AND (manifest #>> '{media,size_bytes}')::bigint > 0
        AND manifest #>> '{media,sha256}' ~ '^[0-9a-f]{64}$'
    ) IS TRUE),
    CHECK (CASE jsonb_typeof(manifest #> '{media,tracks}')
        WHEN 'array' THEN jsonb_array_length(manifest #> '{media,tracks}') > 0
        ELSE false
    END),
    CHECK (
        (time_basis = 'source_clock' AND source_time_us IS NOT NULL AND clock_epoch_id IS NOT NULL
            AND uncertainty_us IS NOT NULL AND uncertainty_us >= 0)
        OR
        (time_basis = 'reception' AND begin_utc_us = received_at_utc_us AND clock_epoch_id IS NULL
            AND uncertainty_us IS NULL)
    )
);

CREATE INDEX blocks_by_point_time ON bacuri_archive.blocks (site_id, point_id, begin_utc_us);

CREATE TABLE bacuri_archive.gaps (
    site_id uuid NOT NULL,
    point_id uuid NOT NULL,
    binding_revision bigint NOT NULL,
    begin_utc_us bigint NOT NULL,
    end_utc_us bigint NOT NULL,
    reason text NOT NULL CHECK (reason IN ('source_offline', 'storage_failure', 'overload')),
    PRIMARY KEY (site_id, point_id, binding_revision, begin_utc_us, end_utc_us),
    FOREIGN KEY (site_id, point_id, binding_revision) REFERENCES bacuri_inventory.point_bindings,
    CHECK (begin_utc_us < end_utc_us)
);

CREATE INDEX gaps_by_point_time ON bacuri_archive.gaps (site_id, point_id, begin_utc_us);

CREATE VIEW bacuri_archive.timeline_intervals AS
SELECT site_id, point_id, binding_revision, begin_utc_us, end_utc_us,
       'recorded'::text AS interval_kind, time_basis, uncertainty_us, NULL::text AS gap_reason
FROM bacuri_archive.blocks
UNION ALL
SELECT site_id, point_id, binding_revision, begin_utc_us, end_utc_us,
       'gap'::text, NULL::text, NULL::bigint, reason
FROM bacuri_archive.gaps;

CREATE TABLE bacuri_events.observations (
    site_id uuid NOT NULL REFERENCES bacuri_inventory.sites,
    source_id uuid NOT NULL,
    observation_id uuid NOT NULL,
    type text NOT NULL CHECK (btrim(type) <> ''),
    schema_version integer NOT NULL CHECK (schema_version > 0),
    point_id uuid,
    binding_revision bigint,
    origin text NOT NULL CHECK (btrim(origin) <> ''),
    observed_at_utc_us bigint NOT NULL,
    received_at_utc_us bigint NOT NULL,
    source_time_us bigint,
    time_basis text NOT NULL,
    clock_epoch_id uuid,
    uncertainty_us bigint,
    attributes jsonb NOT NULL CHECK (jsonb_typeof(attributes) = 'object'),
    PRIMARY KEY (site_id, source_id, observation_id),
    FOREIGN KEY (site_id, point_id, binding_revision) REFERENCES bacuri_inventory.point_bindings,
    CHECK ((point_id IS NULL) = (binding_revision IS NULL)),
    CHECK (
        (time_basis = 'source_clock' AND source_time_us IS NOT NULL AND clock_epoch_id IS NOT NULL
            AND uncertainty_us IS NOT NULL AND uncertainty_us >= 0)
        OR
        (time_basis = 'reception' AND observed_at_utc_us = received_at_utc_us AND clock_epoch_id IS NULL
            AND uncertainty_us IS NULL)
    )
);

CREATE INDEX observations_by_point_time ON bacuri_events.observations (site_id, point_id, observed_at_utc_us);

INSERT INTO bacuri_inventory.schema_migrations VALUES (1);

COMMIT;
