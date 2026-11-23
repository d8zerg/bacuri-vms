BEGIN IMMEDIATE;
CREATE TABLE recording_plan(singleton INTEGER PRIMARY KEY CHECK(singleton=1), payload TEXT NOT NULL) STRICT;
INSERT INTO recording_plan SELECT 1,'{}' WHERE EXISTS(SELECT 1 FROM volume_owner);
CREATE TABLE local_point_retention(point_id TEXT PRIMARY KEY,cutoff_us INTEGER NOT NULL) STRICT;
CREATE INDEX committed_by_point_end ON committed_blocks((manifest_json->>'point_id'),(manifest_json->>'end_utc_us'));
CREATE INDEX gaps_by_point_end ON archive_gaps(point,end_us);
PRAGMA user_version=6;
COMMIT;
