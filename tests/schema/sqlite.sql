BEGIN IMMEDIATE;

CREATE TEMP TABLE fixture (manifest TEXT NOT NULL) STRICT;
INSERT INTO fixture VALUES (CAST(readfile('tests/schema/block-manifest-v1.json') AS TEXT));

INSERT INTO committed_blocks (
    site_id, block_id, recording_epoch_id, owner_generation, manifest_json, relative_directory, committed_at_utc_us
)
SELECT json_extract(manifest, '$.site_id'), json_extract(manifest, '$.block_id'),
       json_extract(manifest, '$.recording_epoch_id'), json_extract(manifest, '$.owner_generation'),
       manifest, 'blocks/first', 1120
FROM fixture
WHERE true
ON CONFLICT (site_id, block_id, recording_epoch_id, owner_generation) DO NOTHING;

INSERT INTO committed_blocks (
    site_id, block_id, recording_epoch_id, owner_generation, manifest_json, relative_directory, committed_at_utc_us
)
SELECT json_extract(manifest, '$.site_id'), json_extract(manifest, '$.block_id'),
       json_extract(manifest, '$.recording_epoch_id'), json_extract(manifest, '$.owner_generation'),
       manifest, 'blocks/first', 1130
FROM fixture
WHERE true
ON CONFLICT (site_id, block_id, recording_epoch_id, owner_generation) DO NOTHING;

CREATE TEMP TABLE checks (label TEXT, passed INTEGER NOT NULL CHECK (passed = 1)) STRICT;
INSERT INTO checks VALUES ('duplicate block remains one row', (SELECT count(*) = 1 FROM committed_blocks));
INSERT INTO checks VALUES (
    'retry preserves the original commit time',
    (SELECT committed_at_utc_us = 1120 FROM committed_blocks)
);
INSERT INTO checks VALUES (
    'committed block awaits catalog registration',
    (SELECT registered_at_utc_us IS NULL FROM committed_blocks)
);
UPDATE committed_blocks SET registered_at_utc_us = 1155;

COMMIT;
