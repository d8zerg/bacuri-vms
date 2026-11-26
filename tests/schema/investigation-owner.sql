INSERT INTO bacuri_archive.investigations
VALUES ('10000000-0000-4000-8000-000000000001','60000000-0000-4000-8000-000000000002','Case before migration',
        '20000000-0000-4000-8000-000000000001','transfer','[]',100,7);
\ir ../../storage/postgresql/migrations/0031_investigation_owner.sql
DO $$
BEGIN
    ASSERT to_regclass('bacuri_archive.investigations') IS NULL;
    ASSERT EXISTS (SELECT FROM bacuri_investigations.investigations
                   WHERE number='Case before migration' AND revision=7 AND notes='transfer');
END $$;
