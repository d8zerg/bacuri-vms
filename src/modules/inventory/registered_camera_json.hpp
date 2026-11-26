#pragma once
#include "modules/inventory/registration_types.hpp"
#include <nlohmann/json.hpp>
namespace bacuri::vms::control
{
inline void from_json(const nlohmann::json& j, CameraIdentification& c)
{
    c = {j.at("endpoint_id"), j.at("manufacturer"), j.at("model"),  j.at("firmware"),
         j.at("serial"),      j.at("ptz"),          j.at("events"), {}};
    if (j.contains("channel_token"))
        c.channel_token = j.at("channel_token").get<std::string>();
}
inline void to_json(nlohmann::json& j, const CameraIdentification& c)
{
    j = {{"endpoint_id", c.endpoint_id},
         {"manufacturer", c.manufacturer},
         {"model", c.model},
         {"firmware", c.firmware},
         {"serial", c.serial},
         {"ptz", c.ptz},
         {"events", c.events}};
    if (c.channel_token)
        j["channel_token"] = *c.channel_token;
}
inline void from_json(const nlohmann::json& j, RegisteredCamera& c)
{
    c = {j.at("id"),
         j.at("point_id"),
         j.at("name"),
         j.at("main_url"),
         j.at("secondary_url"),
         j.at("desired_revision"),
         j.at("binding_revision"),
         j.at("state"),
         j.at("has_credentials"),
         {},
         {},
         {}};
    if (!j.at("applied_revision").is_null())
        c.applied_revision = j.at("applied_revision").get<std::string>();
    if (j.contains("apply_failure"))
        c.apply_failure = j.at("apply_failure").get<std::string>();
    if (j.contains("endpoint_id"))
        c.identification = j.get<CameraIdentification>();
}
inline void to_json(nlohmann::json& j, const RegisteredCamera& c)
{
    j = {{"id", c.id},
         {"point_id", c.point_id},
         {"name", c.name},
         {"main_url", c.main_url},
         {"secondary_url", c.secondary_url},
         {"desired_revision", c.desired_revision},
         {"binding_revision", c.binding_revision},
         {"state", c.state},
         {"has_credentials", c.has_credentials}};
    j["applied_revision"] = c.applied_revision ? nlohmann::json(*c.applied_revision) : nlohmann::json(nullptr);
    if (c.apply_failure)
        j["apply_failure"] = *c.apply_failure;
    if (c.identification)
        j.update(nlohmann::json(*c.identification));
}
} // namespace bacuri::vms::control
