BEGIN;
SET LOCAL plpgsql.check_asserts = on;

CREATE TEMP TABLE block_fixture (manifest jsonb NOT NULL);
INSERT INTO block_fixture VALUES (:'manifest_fixture');
PREPARE insert_block(jsonb) AS :insert_block_sql
PREPARE read_block(uuid, uuid, uuid, bigint) AS :read_block_sql

DO $test$
DECLARE
    manifest jsonb := (SELECT f.manifest FROM block_fixture AS f);
    site uuid := (manifest ->> 'site_id')::uuid;
    point uuid := (manifest ->> 'point_id')::uuid;
    block uuid := (manifest ->> 'block_id')::uuid;
    epoch uuid := (manifest ->> 'recording_epoch_id')::uuid;
    device uuid := (manifest ->> 'device_id')::uuid;
    channel uuid := (manifest ->> 'channel_id')::uuid;
    profile uuid := (manifest ->> 'profile_id')::uuid;
    replacement_device uuid := '40000000-0000-4000-8000-000000000002';
    replacement_channel uuid := '30000000-0000-4000-8000-000000000002';
    replacement_profile uuid := 'b0000000-0000-4000-8000-000000000002';
    replacement_block uuid := '60000000-0000-4000-8000-000000000002';
    event_id uuid := '90000000-0000-4000-8000-000000000001';
    source uuid := '80000000-0000-4000-8000-000000000001';
    inserted_block uuid;
    stored jsonb;
    revised jsonb;
BEGIN
    ASSERT (SELECT version FROM bacuri_inventory.schema_migrations) = 1, 'initial schema version';
    INSERT INTO bacuri_inventory.sites VALUES (site, 'Test site', 'UTC');
    INSERT INTO bacuri_inventory.observation_points VALUES (site, point, 'Test point');
    INSERT INTO bacuri_inventory.devices
    VALUES (site, device, 'Original camera'), (site, replacement_device, 'Replacement camera');
    INSERT INTO bacuri_inventory.channels
    VALUES (site, channel, device, 0), (site, replacement_channel, replacement_device, 0);
    INSERT INTO bacuri_inventory.stream_profiles
    VALUES (site, profile, channel, 'main'), (site, replacement_profile, replacement_channel, 'main');
    INSERT INTO bacuri_inventory.point_bindings VALUES (site, point, 1, channel, 0);

    INSERT INTO bacuri_inventory.configuration_versions VALUES (site, device, 1, '{}'), (site, device, 2, '{}');
    INSERT INTO bacuri_inventory.configuration_progress VALUES (site, device, 2, 1, 'failed', 'camera_offline');
    ASSERT EXISTS (
        SELECT 1 FROM bacuri_inventory.configuration_progress
        WHERE site_id = site AND device_id = device AND desired_revision = 2 AND applied_revision = 1
    ), 'desired and applied configuration remain separate on failure';

    EXECUTE format('EXECUTE insert_block(%L)', manifest) INTO inserted_block;
    ASSERT inserted_block IS NOT DISTINCT FROM block, 'first block registration';
    EXECUTE format('EXECUTE insert_block(%L)', manifest) INTO inserted_block;
    ASSERT inserted_block IS NULL, 'identical retry does not insert a row';

    revised := jsonb_set(manifest, '{media,size_bytes}', '8192');
    EXECUTE format('EXECUTE insert_block(%L)', revised) INTO inserted_block;
    ASSERT inserted_block IS NULL, 'different payload for the same key does not replace the stored block';
    EXECUTE format('EXECUTE read_block(%L, %L, %L, 1)', site, block, epoch) INTO stored;
    ASSERT stored IS NOT DISTINCT FROM manifest AND stored IS DISTINCT FROM revised,
        'key lookup enables conflict detection and the original manifest is preserved';

    INSERT INTO bacuri_archive.gaps VALUES (site, point, 1, 1118, 1150, 'source_offline');
    INSERT INTO bacuri_inventory.point_bindings VALUES (site, point, 2, replacement_channel, 1150);
    revised := manifest || jsonb_build_object(
        'block_id', replacement_block,
        'binding_revision', 2,
        'device_id', replacement_device,
        'channel_id', replacement_channel,
        'profile_id', replacement_profile,
        'begin_utc_us', 1150,
        'end_utc_us', 1250,
        'time', jsonb_build_object(
            'basis', 'reception',
            'received_at_utc_us', 1150,
            'source_time_us', 100,
            'clock_epoch_id', NULL,
            'uncertainty_us', NULL
        )
    );
    EXECUTE format('EXECUTE insert_block(%L)', revised) INTO inserted_block;
    ASSERT inserted_block IS NOT DISTINCT FROM replacement_block, 'register replacement camera video';

    ASSERT (SELECT count(*) FROM bacuri_inventory.observation_points WHERE site_id = site) = 1,
        'replacement preserves the observation point';
    ASSERT (SELECT array_agg(device_id ORDER BY binding_revision)
            FROM bacuri_archive.blocks WHERE site_id = site AND point_id = point) = ARRAY[device, replacement_device],
        'each block retains the physical source and binding revision';
    ASSERT (SELECT array_agg(interval_kind ORDER BY begin_utc_us)
            FROM bacuri_archive.timeline_intervals WHERE site_id = site AND point_id = point)
        = ARRAY['recorded', 'gap', 'recorded'],
        'timeline distinguishes an explicit gap from available footage';
    ASSERT EXISTS (
        SELECT 1 FROM bacuri_archive.timeline_intervals
        WHERE site_id = site AND point_id = point AND begin_utc_us = 1018
          AND time_basis = 'source_clock' AND uncertainty_us = 8
    ), 'known uncertainty is preserved';
    ASSERT EXISTS (
        SELECT 1 FROM bacuri_archive.timeline_intervals
        WHERE site_id = site AND point_id = point AND begin_utc_us = 1150
          AND interval_kind = 'recorded' AND time_basis = 'reception' AND uncertainty_us IS NULL
    ), 'receipt-based time is available and uncertainty is unspecified';

    BEGIN
        EXECUTE format('EXECUTE insert_block(%L)',
            revised || jsonb_build_object('owner_generation', 2, 'channel_id', channel)
        );
        RAISE EXCEPTION 'block accepted with a channel from another binding';
    EXCEPTION WHEN foreign_key_violation THEN
        NULL;
    END;

    BEGIN
        EXECUTE format('EXECUTE insert_block(%L)',
            revised || jsonb_build_object('owner_generation', 2, 'end_utc_us', 1150)
        );
        RAISE EXCEPTION 'empty recording interval accepted';
    EXCEPTION WHEN check_violation THEN
        NULL;
    END;

    EXECUTE format('EXECUTE insert_block(%L)', jsonb_set(manifest, '{owner_generation}', '2')) INTO inserted_block;
    ASSERT inserted_block IS NOT DISTINCT FROM block, 'owner generation is part of the persisted key';
    EXECUTE format('EXECUTE insert_block(%L)', jsonb_set(
        manifest, '{recording_epoch_id}', '"70000000-0000-4000-8000-000000000002"'
    )) INTO inserted_block;
    ASSERT inserted_block IS NOT DISTINCT FROM block, 'recording epoch is part of the persisted key';

    INSERT INTO bacuri_events.observations (
        site_id, source_id, observation_id, type, schema_version, point_id, binding_revision, origin,
        observed_at_utc_us, received_at_utc_us, source_time_us, time_basis, attributes
    ) VALUES (site, source, event_id, 'motion', 1, point, 2, 'camera', 1175, 1175, 117, 'reception', '{}');
    INSERT INTO bacuri_events.observations (
        site_id, source_id, observation_id, type, schema_version, point_id, binding_revision, origin,
        observed_at_utc_us, received_at_utc_us, source_time_us, time_basis, attributes
    ) VALUES (site, source, event_id, 'motion', 1, point, 2, 'camera', 1180, 1180, 117, 'reception', '{}')
    ON CONFLICT (site_id, source_id, observation_id) DO NOTHING;

    ASSERT (SELECT count(*) FROM bacuri_events.observations
            WHERE site_id = site AND source_id = source AND observation_id = event_id) = 1,
        'observation retry uses its source key';
    ASSERT (SELECT received_at_utc_us FROM bacuri_events.observations
            WHERE site_id = site AND source_id = source AND observation_id = event_id) = 1175,
        'observation replay preserves the original receive time';
END;
$test$;

\if :{?persist_fixture}
COMMIT;
\else
ROLLBACK;
\endif
