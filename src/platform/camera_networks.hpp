#pragma once

#include <gio/gio.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace bacuri::vms::platform
{
// The networks where cameras may be reached. A camera address outside them is refused before any
// connection, and plain RTSP is allowed only for explicitly listed addresses.
class CameraNetworks
{
  public:
    struct LegacyRtsp {
        std::string addresses;
    };
    explicit CameraNetworks(const std::string& specification, const LegacyRtsp& legacy = {});
    // Networks from BACURI_CAMERA_NETWORKS and plain RTSP addresses from BACURI_LEGACY_RTSP_ADDRESSES.
    static CameraNetworks configured();
    const std::vector<std::string>& legacy_addresses() const
    {
        return legacy_;
    }
    std::vector<std::string> discovery_hosts() const;
    bool contains(GInetAddress* address) const;
    // The address to connect to: the URL with a resolved and permitted host, or nothing.
    std::optional<std::string> stream_url(const std::string& url) const;
    bool permits(const std::string& url) const;

  private:
    std::vector<std::string> legacy_;
    std::vector<std::shared_ptr<GInetAddressMask>> masks_;
};
} // namespace bacuri::vms::platform
