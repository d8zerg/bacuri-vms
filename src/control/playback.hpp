#pragma once
#include "control/api.hpp"
namespace bacuri::vms::control
{
void allow(const Request&, Response&, std::initializer_list<http::verb>);
void copy_export_block(const std::string& session, const std::string& browser, const Json& source,
                       const std::filesystem::path& destination);
void export_fragment(const Request&, Response&, const Sessions&, const Key&, const std::string& site,
                     const std::string& point);
class Playback
{
  public:
    Playback(const Sessions& sessions, const Key& key) : sessions_{sessions}, key_{key} {}
    void point(const Request&, Response&, const std::string& site, const std::string& point, bool search) const;
    void session(const Request&, Response&, const std::string& id, std::string_view operation) const;
    void alert_archive(const Request&, Response&, const std::string& site, const std::string& id) const;

  private:
    const Sessions& sessions_;
    const Key& key_;
};
} // namespace bacuri::vms::control
