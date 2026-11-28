#pragma once
#include "modules/access/session.hpp"
namespace bacuri::vms::control::access
{
void snapshot_receipt(const Request&, Response&, const Sessions&, const std::string&, const std::string&);
bool administration(const Request&, Response&, const Sessions&, const Key&, const std::string&, std::string_view);
} // namespace bacuri::vms::control::access
