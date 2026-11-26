// WS-Discovery of cameras on the camera networks.
#include "control/inventory/onvif.hpp"
#include "control/inventory/onvif/soap.hpp"

namespace bacuri::vms::control
{
using namespace onvif;

Json discover_onvif(const CameraNetworks& networks, const Key& key, const std::string& site)
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
        throw Failure{503, "camera_unavailable"};
    const int fd = g_socket_get_fd(sock.get());
    sockaddr_in dest{};
    dest.sin_family = AF_INET;
    dest.sin_port = htons(3702);
    inet_pton(AF_INET, "239.255.255.250", &dest.sin_addr);
    ifaddrs* addresses{};
    if (getifaddrs(&addresses) != 0)
        throw Failure{503, "camera_unavailable"};
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
        throw Failure{503, "camera_discovery_unavailable"};
    Json found = Json::array();
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
            matches(found, seen, parse(std::string(buffer.data(), static_cast<std::size_t>(n))), mid, key, site,
                    ip.get());
        } catch (const Failure&) {
            continue;
        }
    }
    return found;
}
} // namespace bacuri::vms::control
