#pragma once
#include "platform/secrets.hpp"
#include <iosfwd>

namespace bacuri::vms::postgresql
{
class QuerySession;
}

namespace bacuri::vms::control
{
void backup_configuration(std::ostream& output);
void backup_configuration(postgresql::QuerySession&, std::ostream& output, const std::string& site,
                          const std::string& node, const platform::Key& key);
void restore_configuration(std::istream& input);
} // namespace bacuri::vms::control
