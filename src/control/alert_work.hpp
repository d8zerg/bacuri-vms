#pragma once
#include "control/api.hpp"

namespace bacuri::vms::control
{
void alert_work(const Request&, Response&, const Sessions&, const Key&, const std::string& site,
                std::string_view path);
}
