SELECT manifest
FROM bacuri_archive.blocks
WHERE site_id = $1::uuid
  AND block_id = $2::uuid
  AND recording_epoch_id = $3::uuid
  AND owner_generation = $4::bigint;
