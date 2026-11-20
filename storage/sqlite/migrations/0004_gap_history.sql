BEGIN IMMEDIATE;
ALTER TABLE pending_gaps RENAME TO archive_gaps;
ALTER TABLE archive_gaps ADD COLUMN registered INTEGER NOT NULL DEFAULT 0 CHECK (registered IN (0,1));
CREATE INDEX archive_gaps_pending ON archive_gaps(id) WHERE registered=0;
PRAGMA user_version=4;
COMMIT;
