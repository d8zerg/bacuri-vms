SELECT 'CREATE ROLE "bacuri-core" LOGIN' WHERE NOT EXISTS(SELECT FROM pg_roles WHERE rolname='bacuri-core') \gexec
SELECT 'CREATE DATABASE bacuri_vms OWNER "bacuri-core"' WHERE NOT EXISTS(SELECT FROM pg_database WHERE datname='bacuri_vms') \gexec
DO $$ BEGIN
    IF NOT EXISTS (SELECT FROM pg_database d JOIN pg_roles r ON r.oid=d.datdba WHERE d.datname='bacuri_vms' AND r.rolname='bacuri-core')
    THEN RAISE EXCEPTION 'Unexpected database owner'; END IF;
END $$;
