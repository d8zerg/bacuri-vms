BEGIN IMMEDIATE;
CREATE TABLE protected_blocks (
    block_id TEXT NOT NULL PRIMARY KEY
) STRICT, WITHOUT ROWID;
CREATE TABLE replica_checks (
    block_id TEXT NOT NULL PRIMARY KEY,
    checked_us INTEGER NOT NULL, verified INTEGER NOT NULL CHECK(verified IN (0,1))
) STRICT, WITHOUT ROWID;
CREATE INDEX replica_checks_by_age ON replica_checks(checked_us);
CREATE TABLE replica_deletions (block_id TEXT NOT NULL PRIMARY KEY) STRICT, WITHOUT ROWID;
PRAGMA user_version=7;
COMMIT;
