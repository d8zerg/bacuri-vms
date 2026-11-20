#pragma once
#include <iosfwd>

namespace bacuri::vms::control
{
void backup_configuration(std::ostream& output);
void restore_configuration(std::istream& input);
void rebuild_archive(std::ostream& output);
} // namespace bacuri::vms::control
