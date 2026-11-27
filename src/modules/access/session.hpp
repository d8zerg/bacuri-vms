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
    // A signed-in browser issues a one-time code for a desktop client on the same computer.
    void desktop_code(const Request&, Response&) const;
    // A desktop client exchanges the code and its PKCE verifier for its own session.
    void desktop_session(const Request&, Response&) const;
    // The request presents a desktop session token in its Authorization header.
    static bool desktop_request(const Request& request);
    Identity authenticate(postgresql::QuerySession& db, const Request& request, bool mutation,
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
