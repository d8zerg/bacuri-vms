#pragma once
#include "modules/access/session.hpp"
#include "services/control/transport/routes.hpp"

namespace bacuri::vms::control
{
void node_probe(const Request&, Response&, const Sessions&, const std::string& site, const PathMatch& match);
}
