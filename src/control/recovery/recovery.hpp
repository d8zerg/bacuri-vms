#pragma once
#include <iosfwd>

namespace bacuri::vms::control
{
void backup_configuration(std::ostream& output);
void restore_configuration(std::istream& input);
class Database;
enum class StorageLayout { legacy, module_schemas };
void rebuild_archive(Database& database, std::ostream& output, StorageLayout layout);
void rebuild_archive(std::ostream& output);
} // namespace bacuri::vms::control
