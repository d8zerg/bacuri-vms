#include "modules/access/session.hpp"
#pragma once
#include "application/control_api/api.hpp"
namespace bacuri::vms::control
{
void copy_export_block(const std::string& session, const Json& source, const std::string& browser,
                       const std::filesystem::path& destination);
class Playback
{
  public:
    Playback(const Sessions& sessions, const Key& key) : sessions_{sessions}, key_{key} {}
    void layouts(const Request&, Response&, const std::string& site, const std::string& id) const;
    void point(const Request&, Response&, const std::string& site, const std::string& point, bool search) const;
    void session(const Request&, Response&, const std::string& id, std::string_view operation) const;
    void alert_archive(const Request&, Response&, const std::string& site, const std::string& id) const;

  private:
    const Sessions& sessions_;
    const Key& key_;
};
} // namespace bacuri::vms::control
