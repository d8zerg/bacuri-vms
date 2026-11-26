#pragma once
#include "application/control_api/api.hpp"

namespace bacuri::vms::control
{
struct Identity {
    std::string user;
    std::string cookie;
};

class Sessions
{
  public:
    Sessions(Key key, std::string origin);
    void handle(const Request& request, Response& response, const std::string& peer) const;
    void corporate(const Request&, Response&, std::string_view path) const;
    void factor(const Request&, Response&) const;
    Identity authenticate(postgresql::Database& db, const Request& request, bool mutation,
                          bool activity = true) const;

  private:
    void issue(postgresql::Database&, const Request&, Response&, const std::string& user,
               const std::string& site = {}, const std::string& refresh = {}) const;
    void login(const Request& request, Response& response, const std::string& peer) const;
    void logout(const Request& request, Response& response) const;
    Key key_;
    std::string origin_;
    std::string dummy_hash_;
};

} // namespace bacuri::vms::control
