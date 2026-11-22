#pragma once
#include "control/access/session.hpp"

namespace bacuri::vms::control
{
class CameraRegistry
{
  public:
    CameraRegistry(const Sessions& sessions, const Key& key, CameraNetworks networks)
        : sessions_{sessions}, key_{key}, networks_{std::move(networks)}
    {
    }
    void onvif(const Request& request, Response& response, const std::string& site, bool inspect) const;
    void issues(const Request&, Response&, const std::string& site, const std::string& device) const;
    void previous_recording(const Request&, Response&, const std::string& site, const std::string& device) const;
    void ptz(const Request&, Response&, const std::string& site, const std::string& point) const;
    void points(const Request& request, Response& response, const std::string& site) const;
    void cameras(const Request& request, Response& response, const std::string& site, std::string device) const;

  private:
    const Sessions& sessions_;
    const Key& key_;
    CameraNetworks networks_;
};

} // namespace bacuri::vms::control
