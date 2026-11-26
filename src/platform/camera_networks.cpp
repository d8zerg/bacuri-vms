#include "platform/camera_networks.hpp"
#include "platform/settings.hpp"

#include <algorithm>
#include <sstream>
#include <stdexcept>

namespace bacuri::vms::platform
{
CameraNetworks CameraNetworks::configured()
{
    return CameraNetworks{setting("BACURI_CAMERA_NETWORKS"),
                          {.addresses = optional_setting("BACURI_LEGACY_RTSP_ADDRESSES").value_or("")}};
}

CameraNetworks::CameraNetworks(const std::string& specification, const LegacyRtsp& exception)
{
    std::istringstream configured{specification};
    std::string network;
    while (std::getline(configured, network, ',')) {
        auto* mask = g_inet_address_mask_new_from_string(network.c_str(), nullptr);
        if (!mask) {
            throw std::invalid_argument("Invalid camera network");
        }
        masks_.emplace_back(mask, g_object_unref);
    }
    std::istringstream legacy{exception.addresses};
    while (std::getline(legacy, network, ',')) {
        std::unique_ptr<GInetAddress, decltype(&g_object_unref)> address{
            g_inet_address_new_from_string(network.c_str()), g_object_unref};
        if (!address)
            throw std::invalid_argument("Expected a single legacy camera IP address");
        std::unique_ptr<gchar, decltype(&g_free)> ip{g_inet_address_to_string(address.get()), g_free};
        if (contains(address.get()))
            legacy_.emplace_back(ip.get());
    }
    std::ranges::sort(legacy_);
    legacy_.erase(std::ranges::unique(legacy_).begin(), legacy_.end());
    if (masks_.empty()) {
        throw std::invalid_argument("Missing camera networks");
    }
}

bool CameraNetworks::permits(const std::string& url) const
{
    return stream_url(url).has_value();
}

std::vector<std::string> CameraNetworks::discovery_hosts() const
{
    std::vector<std::string> hosts;
    for (const auto& mask : masks_) {
        auto* address = g_inet_address_mask_get_address(mask.get());
        if (g_inet_address_get_family(address) == G_SOCKET_FAMILY_IPV4 &&
            g_inet_address_mask_get_length(mask.get()) == 32 && !g_inet_address_get_is_multicast(address) &&
            !g_inet_address_get_is_any(address)) {
            std::unique_ptr<gchar, decltype(&g_free)> host{g_inet_address_to_string(address), g_free};
            if (std::string_view(host.get()) != "255.255.255.255")
                hosts.emplace_back(host.get());
        }
    }
    std::ranges::sort(hosts);
    hosts.erase(std::ranges::unique(hosts).begin(), hosts.end());
    return hosts;
}

bool CameraNetworks::contains(GInetAddress* address) const
{
    return std::ranges::any_of(
        masks_, [address](const auto& mask) { return g_inet_address_mask_matches(mask.get(), address); });
}

std::optional<std::string> CameraNetworks::stream_url(const std::string& url) const
{
    if (url.size() > 2048 || url.find_first_of(" \t\r\n\\#") != url.npos || url.contains('\0') ||
        !(url.starts_with("rtsp://") || url.starts_with("rtsps://"))) {
        return std::nullopt;
    }
    std::unique_ptr<GUri, decltype(&g_uri_unref)> uri{g_uri_parse(url.c_str(), G_URI_FLAGS_ENCODED, nullptr),
                                                      g_uri_unref};
    if (!uri || g_uri_get_userinfo(uri.get()) || !g_uri_get_host(uri.get()) || g_uri_get_port(uri.get()) == 0) {
        return std::nullopt;
    }
    if (const auto* query = g_uri_get_query(uri.get());
        query &&
        !g_regex_match_simple(
            "^(transmode=unicast&profile=[a-zA-Z0-9._-]{1,64}|profile=[a-zA-Z0-9._-]{1,64}(&transmode=unicast)?)$",
            query, G_REGEX_DEFAULT, G_REGEX_MATCH_DEFAULT))
        return std::nullopt;
    std::unique_ptr<GResolver, decltype(&g_object_unref)> resolver{g_resolver_get_default(), g_object_unref};
    std::unique_ptr<GList, decltype(&g_resolver_free_addresses)> addresses{
        g_resolver_lookup_by_name(resolver.get(), g_uri_get_host(uri.get()), nullptr, nullptr),
        g_resolver_free_addresses};
    if (!addresses) {
        return std::nullopt;
    }
    for (const GList* entry = addresses.get(); entry != nullptr; entry = entry->next) {
        if (!contains(G_INET_ADDRESS(entry->data)))
            return std::nullopt;
    }
    if (url.starts_with("rtsps://")) {
        if (g_uri_get_port(uri.get()) >= 0)
            return url;
        std::unique_ptr<gchar, decltype(&g_free)> explicit_port{
            g_uri_join(G_URI_FLAGS_ENCODED, "rtsps", nullptr, g_uri_get_host(uri.get()), 322,
                       g_uri_get_path(uri.get()), g_uri_get_query(uri.get()), nullptr),
            g_free};
        return std::string{explicit_port.get()};
    }
    if (addresses->next)
        return std::nullopt;
    std::unique_ptr<gchar, decltype(&g_free)> ip{g_inet_address_to_string(G_INET_ADDRESS(addresses->data)),
                                                 g_free};
    if (std::ranges::find(legacy_, ip.get()) == legacy_.end())
        return std::nullopt;
    std::unique_ptr<gchar, decltype(&g_free)> pinned{
        g_uri_join(G_URI_FLAGS_ENCODED, "rtsp", nullptr, ip.get(), g_uri_get_port(uri.get()),
                   g_uri_get_path(uri.get()), g_uri_get_query(uri.get()), nullptr),
        g_free};
    return std::string{pinned.get()};
}
} // namespace bacuri::vms::platform
