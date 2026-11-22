DO $$ BEGIN
    IF current_setting('server_version_num')::integer / 10000 <> 17 THEN
        RAISE EXCEPTION 'Bacuri VMS requires PostgreSQL 17 on the configured connection';
    END IF;
END $$;
SELECT 'CREATE ROLE "bacuri-core" LOGIN' WHERE NOT EXISTS(SELECT FROM pg_roles WHERE rolname='bacuri-core') \gexec
SELECT 'CREATE DATABASE bacuri_vms OWNER "bacuri-core"' WHERE NOT EXISTS(SELECT FROM pg_database WHERE datname='bacuri_vms') \gexec
DO $$ BEGIN
    IF NOT EXISTS (SELECT FROM pg_database d JOIN pg_roles r ON r.oid=d.datdba WHERE d.datname='bacuri_vms' AND r.rolname='bacuri-core')
    THEN RAISE EXCEPTION 'Unexpected database owner'; END IF;
END $$;
