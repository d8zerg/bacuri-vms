BEGIN;
ALTER TABLE bacuri_inventory.ptz_commands DROP CONSTRAINT ptz_commands_command_check;
ALTER TABLE bacuri_inventory.ptz_commands ADD CHECK (command IN ('left','right','up','down','stop','zoom_in','zoom_out'));
INSERT INTO bacuri_meta.schema_migrations VALUES (23);
COMMIT;
