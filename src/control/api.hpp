#pragma once

#include "control/database.hpp"
#include "control/http.hpp"
#include "control/support.hpp"
#include <nlohmann/json.hpp>

namespace bacuri::vms::control
{
using Json = nlohmann::json;

struct Failure {
    int status;
    std::string code;
    Json fields = Json::object();
};

struct Identity {
    std::string user;
    std::string cookie;
};

Json object_body(const Request& request, std::initializer_list<std::string_view> fields);
std::string string_field(const Json& object, std::string_view field, std::size_t limit, bool allow_empty = false);
void respond(Response& response, const Json& body, int status = 200);

class Sessions
{
public:
    Sessions(Key key, std::string origin);
    void handle(const Request& request, Response& response, const std::string& peer) const;
    Identity authenticate(Database& db, const Request& request, bool mutation, bool activity = true) const;

private:
    void login(const Request& request, Response& response, const std::string& peer) const;
    void logout(const Request& request, Response& response) const;
    Key key_;
    std::string origin_;
    std::string dummy_hash_;
};

class CameraRegistry
{
public:
    CameraRegistry(const Sessions& sessions, const Key& key, CameraNetworks networks)
        : sessions_{sessions}, key_{key}, networks_{std::move(networks)}
    {
    }
    void points(const Request& request, Response& response, const std::string& site) const;
    void cameras(const Request& request, Response& response, const std::string& site, std::string device) const;

private:
    const Sessions& sessions_;
    const Key& key_;
    CameraNetworks networks_;
};

void serve();
} // namespace bacuri::vms::control
