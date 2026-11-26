BEGIN IMMEDIATE;
-- SQLite cannot change a CHECK constraint, so the gap journal is rebuilt with the new reason.
CREATE TABLE archive_gaps_next (
    id INTEGER PRIMARY KEY, point TEXT NOT NULL, binding INTEGER NOT NULL,
    begin_us INTEGER NOT NULL, end_us INTEGER NOT NULL CHECK(end_us>begin_us),
    reason TEXT NOT NULL CHECK(reason IN ('source_offline','overload','storage_failure','keyframe_interval')),
    registered INTEGER NOT NULL DEFAULT 0 CHECK (registered IN (0,1)),
    UNIQUE(point,binding,begin_us,end_us)
) STRICT;
INSERT INTO archive_gaps_next(id,point,binding,begin_us,end_us,reason,registered)
SELECT id,point,binding,begin_us,end_us,reason,registered FROM archive_gaps;
DROP TABLE archive_gaps;
ALTER TABLE archive_gaps_next RENAME TO archive_gaps;
CREATE INDEX archive_gaps_pending ON archive_gaps(id) WHERE registered=0;
CREATE INDEX gaps_by_end ON archive_gaps(end_us);
CREATE INDEX gaps_by_point_end ON archive_gaps(point,end_us);
PRAGMA user_version=9;
COMMIT;
