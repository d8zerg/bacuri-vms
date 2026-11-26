#pragma once
#include "modules/access/session.hpp"
namespace bacuri::vms::control::access
{
bool administration(const Request&, Response&, const Sessions&, const Key&, const std::string&, std::string_view);
}
