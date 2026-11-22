DO $$
DECLARE
    dependency record;
BEGIN
    SELECT l.* INTO dependency FROM pg_temp.module_links l
    JOIN pg_namespace n ON n.oid=l.source_schema JOIN pg_namespace dest ON dest.oid=l.target_schema
    WHERE starts_with(n.nspname,'bacuri_') AND starts_with(dest.nspname,'bacuri_') AND l.source_schema<>l.target_schema
      AND NOT EXISTS (SELECT FROM pg_temp.original_links o
                      WHERE (o.kind,o.name,o.source,o.target)=(l.kind,l.name,l.source,l.target))
    LIMIT 1;
    IF FOUND THEN
        RAISE EXCEPTION 'New cross-schema % % on %',dependency.kind,dependency.name,dependency.source::regclass;
    END IF;

    SELECT v.oid::regclass AS view_name,t.oid::regclass AS relation INTO dependency
    FROM pg_rewrite r JOIN pg_class v ON v.oid=r.ev_class JOIN pg_namespace n ON n.oid=v.relnamespace
    JOIN pg_depend d ON d.classid='pg_rewrite'::regclass AND d.objid=r.oid AND d.refclassid='pg_class'::regclass
    JOIN pg_class t ON t.oid=d.refobjid JOIN pg_namespace dest ON dest.oid=t.relnamespace
    WHERE v.relkind IN ('v','m') AND starts_with(n.nspname,'bacuri_') AND starts_with(dest.nspname,'bacuri_')
      AND t.relnamespace<>v.relnamespace
    LIMIT 1;
    IF FOUND THEN
        RAISE EXCEPTION 'View % reads cross-schema relation %',dependency.view_name,dependency.relation;
    END IF;
END $$;
