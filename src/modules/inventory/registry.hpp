#pragma once
#include "modules/access/session.hpp"

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
    void templates(const Request&, Response&, const std::string& site, const std::string& id) const;
    void batch(const Request&, Response&, const std::string& site, const std::string& batch,
               const std::string& item, std::string_view action) const;
    void issues(const Request&, Response&, const std::string& site, const std::string& device) const;
    void edge_archive(const Request&, Response&, const std::string& site, const std::string& device) const;
    void previous_recording(const Request&, Response&, const std::string& site, const std::string& device) const;
    void audio(const Request&, Response&, const std::string& site, const std::string& point) const;
    void ptz(const Request&, Response&, const std::string& site, const std::string& point) const;
    void preset(const std::string& site, const std::string& point, std::int64_t binding, const std::string& user,
                const std::string& request, const std::string& target) const;
    void points(const Request& request, Response& response, const std::string& site) const;
    void cameras(const Request& request, Response& response, const std::string& site, std::string device) const;

  private:
    static Json camera(postgresql::Database&, const std::string& site, const std::string& device);
    static std::string site_role(postgresql::Database&, const Identity&, const std::string& site);
    void dispatch_ptz(const postgresql::Rows&, const std::string& site, const std::string& point,
                      const std::string& user, const std::string& request_key, const std::string& command,
                      const std::string& target, const std::string& role) const;
    const Sessions& sessions_;
    const Key& key_;
    CameraNetworks networks_;
};

} // namespace bacuri::vms::control
