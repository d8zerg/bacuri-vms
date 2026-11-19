#pragma once
#include "control/api.hpp"
namespace bacuri::vms::control
{
void allow(const Request&, Response&, std::initializer_list<http::verb>);
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
