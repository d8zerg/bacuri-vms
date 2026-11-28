BEGIN;
ALTER TABLE bacuri_inventory.sites
    ADD COLUMN keymap jsonb NOT NULL DEFAULT '{}' CHECK (jsonb_typeof(keymap) = 'object'),
    ADD COLUMN keymap_revision bigint NOT NULL DEFAULT 0 CHECK (keymap_revision >= 0);
INSERT INTO bacuri_meta.schema_migrations VALUES (35);
COMMIT;
