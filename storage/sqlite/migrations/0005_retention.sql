BEGIN IMMEDIATE;
CREATE TABLE retention (
    singleton INTEGER PRIMARY KEY CHECK(singleton=1),
    cutoff_us INTEGER NOT NULL,
    days INTEGER NOT NULL CHECK(days BETWEEN 0 AND 36500),
    reserve_percent INTEGER NOT NULL CHECK(reserve_percent BETWEEN 1 AND 90),
    removed_blocks INTEGER NOT NULL DEFAULT 0,
    blocked_bytes INTEGER NOT NULL DEFAULT 0
) STRICT;
INSERT INTO retention(singleton,cutoff_us,days,reserve_percent) VALUES(1,-9223372036854775808,0,20);
CREATE INDEX committed_by_end ON committed_blocks((manifest_json->>'end_utc_us'));
CREATE INDEX committed_by_receipt ON committed_blocks((manifest_json->>'$.time.received_at_utc_us'));
CREATE INDEX gaps_by_end ON archive_gaps(end_us);
CREATE TRIGGER count_retirement AFTER DELETE ON committed_blocks BEGIN
    UPDATE retention SET removed_blocks=removed_blocks+1;
END;
CREATE TRIGGER resume_recording AFTER INSERT ON committed_blocks WHEN (SELECT blocked_bytes FROM retention)>0 BEGIN
    UPDATE retention SET blocked_bytes=0;
END;
PRAGMA user_version=5;
COMMIT;
