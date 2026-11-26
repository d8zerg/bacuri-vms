// WS-Discovery of cameras on the camera networks.
#include "modules/inventory/onvif.hpp"
#include "modules/inventory/onvif/soap.hpp"
#include "modules/inventory/onvif/ticket_json.hpp"

namespace bacuri::vms::control
{
using namespace onvif;
namespace
{
void collect_probe_matches(std::vector<DiscoveredCamera>& found, std::set<std::string>& seen, const Doc& doc,
                           const std::string& mid, const Key& key, const std::string& site, GInetAddress* sender)
{
    Chars ip{g_inet_address_to_string(sender), g_free};
    auto* root = xmlDocGetRootElement(doc.get());
    auto* header = child(root, soap, "Header");
    if (!named(root, soap, "Envelope") ||
        value(child(header, addressing, "Action")) != std::string{discovery} + "/ProbeMatches" ||
        value(child(header, addressing, "RelatesTo")) != mid)
        return;
    const auto* list = child(child(root, soap, "Body"), discovery, "ProbeMatches");
    for (auto* m = list ? list->children : nullptr; m && found.size() < 64; m = m->next) {
        if (!named(m, discovery, "ProbeMatch"))
            continue;
        auto id = value(child(child(m, addressing, "EndpointReference"), addressing, "Address"));
        std::istringstream urls(value(child(m, discovery, "XAddrs")));
        std::string candidate;
        for (std::string url; urls >> url;) {
            try {
                if (url.starts_with("https://")) {
                    https_uri(url);
                    candidate = url;
                    break;
                }
                if (!candidate.empty() || !url.starts_with("http://"))
                    continue;
                auto uri = https_uri("https" + url.substr(4));
                if (g_uri_get_port(uri.get()) > 0 && g_uri_get_port(uri.get()) != 80)
                    continue;
                // Some devices advertise only their HTTP address even when the same service accepts TLS.
                Chars secure{g_uri_join(G_URI_FLAGS_ENCODED, "https", nullptr, g_uri_get_host(uri.get()), -1,
                                        g_uri_get_path(uri.get()), g_uri_get_query(uri.get()), nullptr),
                             g_free};
                candidate = secure.get();
            } catch (const Failure&) {
                continue;
            }
        }
        if (!candidate.empty() && seen.insert(id).second)
            found.push_back({id, candidate, issue_ticket(key, site, ProbeCandidate{id, candidate, ip.get()})});
    }
}
} // namespace

std::vector<DiscoveredCamera> discover_onvif(const CameraNetworks& networks, const Key& key,
                                             const std::string& site)
{
    const auto mid = uuid();
    const auto probe =
        envelope("<w:MessageID>" + mid +
                     "</w:MessageID><w:To>urn:schemas-xmlsoap-org:ws:2005:04:discovery</w:To><w:Action>" +
                     std::string(discovery) + "/Probe</w:Action>",
                 "<d:Probe><d:Types>dn:NetworkVideoTransmitter</d:Types></d:Probe>");
    Object<GSocket> sock{
        g_socket_new(G_SOCKET_FAMILY_IPV4, G_SOCKET_TYPE_DATAGRAM, G_SOCKET_PROTOCOL_UDP, nullptr),
        g_object_unref};
    if (!sock)
        throw Failure{ErrorCode::camera_unavailable};
    const int fd = g_socket_get_fd(sock.get());
    sockaddr_in dest{};
    dest.sin_family = AF_INET;
    dest.sin_port = htons(3702);
    inet_pton(AF_INET, "239.255.255.250", &dest.sin_addr);
    ifaddrs* addresses{};
    if (getifaddrs(&addresses) != 0)
        throw Failure{ErrorCode::camera_unavailable};
    std::unique_ptr<ifaddrs, decltype(&freeifaddrs)> interfaces{addresses, freeifaddrs};
    bool sent = false;
    for (auto* entry = addresses; entry; entry = entry->ifa_next) {
        if (!entry->ifa_addr || entry->ifa_addr->sa_family != AF_INET)
            continue;
        const auto address = reinterpret_cast<sockaddr_in*>(entry->ifa_addr)->sin_addr;
        Object<GInetAddress> ip{
            g_inet_address_new_from_bytes(reinterpret_cast<const guint8*>(&address), G_SOCKET_FAMILY_IPV4),
            g_object_unref};
        if (!networks.contains(ip.get()))
            continue;
        if (setsockopt(fd, IPPROTO_IP, IP_MULTICAST_IF, &address, sizeof address) == 0 &&
            sendto(fd, probe.data(), probe.size(), 0, reinterpret_cast<sockaddr*>(&dest), sizeof dest) > 0)
            sent = true;
    }
    for (const auto& host : networks.discovery_hosts()) {
        if (inet_pton(AF_INET, host.c_str(), &dest.sin_addr) == 1 &&
            sendto(fd, probe.data(), probe.size(), 0, reinterpret_cast<sockaddr*>(&dest), sizeof dest) > 0)
            sent = true;
    }
    if (!sent)
        throw Failure{ErrorCode::camera_discovery_unavailable};
    std::vector<DiscoveredCamera> found;
    std::set<std::string> seen;
    const auto end = Clock::now() + 1200ms;
    pollfd poller{fd, POLLIN, 0};
    while (Clock::now() < end && found.size() < 64) {
        if (poll(&poller, 1,
                 static_cast<int>(std::max(
                     0L, std::chrono::duration_cast<std::chrono::milliseconds>(end - Clock::now()).count()))) <= 0)
            break;
        sockaddr_in peer{};
        socklen_t size = sizeof peer;
        std::array<char, 65536> buffer{};
        auto n = recvfrom(fd, buffer.data(), buffer.size(), 0, reinterpret_cast<sockaddr*>(&peer), &size);
        if (n <= 0)
            continue;
        Object<GInetAddress> ip{
            g_inet_address_new_from_bytes(reinterpret_cast<const guint8*>(&peer.sin_addr), G_SOCKET_FAMILY_IPV4),
            g_object_unref};
        if (!networks.contains(ip.get()))
            continue;
        try {
            collect_probe_matches(found, seen, parse(std::string(buffer.data(), static_cast<std::size_t>(n))), mid,
                                  key, site, ip.get());
        } catch (const Failure&) {
            continue;
        }
    }
    return found;
}
} // namespace bacuri::vms::control
