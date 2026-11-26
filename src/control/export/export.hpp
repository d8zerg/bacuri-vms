#include "control/access/session.hpp"
#pragma once
#include "control/api.hpp"
namespace bacuri::vms::control
{
void export_case(const Request&, Response&, const Sessions&, const Key&, const std::string& site,
                 const std::string& id);
void export_fragment(const Request&, Response&, const Sessions&, const Key&, const std::string& site,
                     const std::string& point);
} // namespace bacuri::vms::control
