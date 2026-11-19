PRAGMA journal_mode = WAL;
PRAGMA synchronous = FULL;

BEGIN IMMEDIATE;

CREATE TABLE committed_blocks (
    site_id TEXT NOT NULL,
    block_id TEXT NOT NULL,
    recording_epoch_id TEXT NOT NULL,
    owner_generation INTEGER NOT NULL CHECK (owner_generation > 0),
    manifest_json TEXT NOT NULL CHECK (json_valid(manifest_json)),
    relative_directory TEXT NOT NULL CHECK (
        relative_directory <> ''
        AND substr(relative_directory, 1, 1) <> '/'
        AND instr('/' || relative_directory || '/', '/../') = 0
        AND instr('/' || relative_directory || '/', '/./') = 0
    ),
    committed_at_utc_us INTEGER NOT NULL,
    registered_at_utc_us INTEGER,
    PRIMARY KEY (site_id, block_id, recording_epoch_id, owner_generation),
    CHECK (
        json_type(manifest_json) IS 'object'
        AND json_extract(manifest_json, '$.manifest_version') IS 1
        AND json_extract(manifest_json, '$.site_id') IS site_id
        AND json_extract(manifest_json, '$.block_id') IS block_id
        AND json_extract(manifest_json, '$.recording_epoch_id') IS recording_epoch_id
        AND json_type(manifest_json, '$.owner_generation') IS 'integer'
        AND json_extract(manifest_json, '$.owner_generation') IS owner_generation
    )
) STRICT, WITHOUT ROWID;

CREATE INDEX committed_blocks_pending ON committed_blocks (committed_at_utc_us)
WHERE registered_at_utc_us IS NULL;

PRAGMA user_version = 1;
COMMIT;
