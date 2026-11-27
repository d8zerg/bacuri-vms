#pragma once
#include <nlohmann/json_fwd.hpp>
#include "modules/access/session.hpp"
#include "services/control/transport/routes.hpp"

namespace bacuri::vms::control
{
nlohmann::json current_node_probe(const std::string& node, bool prepare);
std::string authorize_node(postgresql::Database&, const Sessions&, const Request&, const std::string& site,
                           const PathMatch&, bool admin);
void node_settings(const Request&, Response&, const Sessions&, const std::string& site, const PathMatch&,
                   bool preview);
void node_probe(const Request&, Response&, const Sessions&, const std::string& site, const PathMatch& match);
} // namespace bacuri::vms::control
