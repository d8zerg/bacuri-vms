WITH cutoff AS MATERIALIZED (
    SELECT bacuri_archive.point_cutoff($1,$2) AS end_us
), held AS MATERIALIZED (
    SELECT DISTINCT h.block_id
    FROM bacuri_archive.archive_holds a JOIN bacuri_archive.held_blocks h ON (h.site_id,h.hold_id)=(a.site_id,a.id)
    WHERE a.site_id=$1 AND a.point_id=$2 AND a.expires_us>bacuri_archive.retention_now_us()
), catalog AS NOT MATERIALIZED (
    SELECT begin_utc_us AS a, end_utc_us AS b, interval_kind AS state, time_basis, uncertainty_us, gap_reason,
           int8range(begin_utc_us, end_utc_us, '[)') AS coverage
    FROM bacuri_archive.catalog_intervals
    WHERE site_id = $1 AND point_id = $2 AND end_utc_us > (SELECT end_us FROM cutoff)
      AND ($6 = '' OR binding_revision = nullif($6, '')::bigint)
    UNION ALL
    SELECT b.begin_utc_us,b.end_utc_us,CASE WHEN b.unavailable_reason IS NULL THEN 'recorded' ELSE 'gap' END,
           CASE WHEN b.unavailable_reason IS NULL THEN b.time_basis END,
           CASE WHEN b.unavailable_reason IS NULL THEN b.uncertainty_us END,
           CASE WHEN b.unavailable_reason IS NOT NULL THEN 'storage_failure' END,
           int8range(b.begin_utc_us,b.end_utc_us,'[)')
    FROM held JOIN bacuri_archive.blocks b ON b.block_id=held.block_id AND b.site_id=$1
    WHERE b.point_id=$2 AND b.end_utc_us <= (SELECT end_us FROM cutoff)
      AND ($6='' OR b.binding_revision=nullif($6,'')::bigint)
), pending AS MATERIALIZED (
    SELECT a, b FROM jsonb_to_recordset($5::jsonb) AS p(a bigint, b bigint)
    WHERE a < $4 AND b > $3 AND b > (SELECT end_us FROM cutoff)
), intervals AS (
    SELECT a, b FROM catalog WHERE coverage && int8range($3, $4, '[)')
    UNION ALL SELECT a, b FROM pending
), boundaries AS (
    SELECT DISTINCT t FROM (
        SELECT unnest(ARRAY[greatest(a, $3::bigint), least(b, $4::bigint)]) AS t FROM intervals
        UNION ALL SELECT $3 UNION ALL SELECT $4
    ) edges ORDER BY t LIMIT 1002
), spans AS (
    SELECT t, lead(t) OVER (ORDER BY t) AS e FROM boundaries
), page_catalog AS MATERIALIZED (
    SELECT * FROM catalog WHERE coverage && int8range($3, (SELECT max(t) FROM boundaries), '[)')
)
SELECT t, e, coalesce(c.state, p.state, 'unavailable'), c.time_basis, c.uncertainty_us,
       CASE WHEN c.state IS NULL AND p.state IS NULL THEN 'unknown_coverage' ELSE c.gap_reason END
FROM spans
LEFT JOIN LATERAL (
    SELECT state, time_basis, uncertainty_us, gap_reason FROM page_catalog WHERE coverage @> t
    ORDER BY (state = 'recorded') DESC, a DESC, b DESC, time_basis, uncertainty_us, gap_reason LIMIT 1
) c ON true
LEFT JOIN LATERAL (
    SELECT 'pending_catalog'::text AS state FROM pending WHERE a <= t AND b > t LIMIT 1
) p ON c.state IS NULL
WHERE e IS NOT NULL ORDER BY t
