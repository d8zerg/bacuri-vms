BEGIN IMMEDIATE;
CREATE TABLE volume_owner (
    singleton INTEGER PRIMARY KEY CHECK (singleton=1),
    site_id TEXT NOT NULL, node_id TEXT NOT NULL, epoch TEXT NOT NULL,
    generation INTEGER NOT NULL CHECK (generation>0)
) STRICT;
ALTER TABLE committed_blocks ADD COLUMN unavailable_reason TEXT CHECK (unavailable_reason IN ('missing','corrupt'));
ALTER TABLE committed_blocks ADD COLUMN catalog_error TEXT;
CREATE UNIQUE INDEX committed_directory ON committed_blocks(relative_directory);
DROP INDEX committed_blocks_pending;
CREATE INDEX committed_blocks_pending ON committed_blocks(committed_at_utc_us)
WHERE registered_at_utc_us IS NULL AND catalog_error IS NULL;
PRAGMA user_version=2;
COMMIT;
