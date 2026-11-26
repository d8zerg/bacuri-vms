#pragma once

namespace bacuri::vms::postgresql
{
inline constexpr char schema_version_query[] = "SELECT max(version) FROM bacuri_meta.schema_migrations";
inline constexpr char legacy_schema_version_query[] =
    "SELECT max(version) FROM bacuri_inventory.schema_migrations";
void migrate();
} // namespace bacuri::vms::postgresql
