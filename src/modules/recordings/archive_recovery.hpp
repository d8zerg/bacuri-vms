#pragma once
#include <iosfwd>

namespace bacuri::vms::postgresql
{
class Database;
}

namespace bacuri::vms::control
{
enum class StorageLayout { legacy, module_schemas };
void rebuild_archive(postgresql::Database& database, std::ostream& output, StorageLayout layout);
void rebuild_archive(std::ostream& output);
} // namespace bacuri::vms::control
