#pragma once
#include "application/control_api/http_json.hpp"
#include "modules/inventory/selection_json.hpp"
namespace bacuri::vms::control::onvif
{
struct ProbeCandidate {
    std::string id, url, ip;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(ProbeCandidate, id, url, ip)
template <typename T>
concept SelectionTicket = std::same_as<T, ProbeCandidate> || std::same_as<T, CameraSelection>;
template <SelectionTicket T> std::string ticket_context(const std::string& site)
{
    if constexpr (std::same_as<T, ProbeCandidate>)
        return "probe/" + site;
    return "camera/" + site;
}
template <SelectionTicket T> std::string issue_ticket(const Key& key, const std::string& site, const T& value)
{
    Json payload = value;
    payload["expires"] = g_get_real_time() / G_USEC_PER_SEC + 600;
    return seal(key, ticket_context<T>(site), payload.dump());
}
template <SelectionTicket T> T read_ticket(const Key& key, const std::string& site, const std::string& token)
{
    try {
        const Json payload = Json::parse(unseal(token, key, ticket_context<T>(site)));
        if (payload.at("expires").get<gint64>() > g_get_real_time() / G_USEC_PER_SEC)
            return payload.get<T>();
    } catch (const std::exception&) {
        throw Failure{ErrorCode::camera_selection_expired};
    }
    throw Failure{ErrorCode::camera_selection_expired};
}
} // namespace bacuri::vms::control::onvif
