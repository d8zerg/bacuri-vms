BEGIN IMMEDIATE;
-- Integrity verification runs after the volume opens. A block is due again after each volume
-- open and after the periodic interval, so opening the volume never reads archived media.
ALTER TABLE committed_blocks ADD COLUMN verified_generation INTEGER NOT NULL DEFAULT 0 CHECK (verified_generation >= 0);
ALTER TABLE committed_blocks ADD COLUMN verified_at_utc_us INTEGER;
CREATE INDEX committed_by_verification ON committed_blocks(verified_generation);
CREATE INDEX committed_by_verified_at ON committed_blocks(verified_at_utc_us);
CREATE INDEX committed_unavailable_verification ON committed_blocks(verified_generation)
WHERE unavailable_reason IS NOT NULL;
PRAGMA user_version=8;
COMMIT;
