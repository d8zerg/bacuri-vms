INSERT INTO bacuri_archive.blocks (manifest)
VALUES ($1::jsonb)
ON CONFLICT (site_id, block_id, recording_epoch_id, owner_generation) DO NOTHING
RETURNING block_id;
