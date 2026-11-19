BEGIN IMMEDIATE;
CREATE TABLE pending_gaps (
    id INTEGER PRIMARY KEY, point TEXT NOT NULL, binding INTEGER NOT NULL,
    begin_us INTEGER NOT NULL, end_us INTEGER NOT NULL CHECK(end_us>begin_us),
    reason TEXT NOT NULL CHECK(reason IN ('source_offline','overload','storage_failure')),
    UNIQUE(point,binding,begin_us,end_us)
) STRICT;
CREATE INDEX pending_point_time ON committed_blocks((manifest_json->>'point_id'),(manifest_json->>'begin_utc_us'))
WHERE registered_at_utc_us IS NULL AND catalog_error IS NULL AND unavailable_reason IS NULL;
PRAGMA user_version=3;
COMMIT;
