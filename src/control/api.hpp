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

Json object_body(const Request& request, std::initializer_list<std::string_view> fields);
std::string string_field(const Json& object, std::string_view field, std::size_t limit, bool allow_empty = false);
void allow(const Request&, Response&, std::initializer_list<http::verb>);
void respond(Response& response, const Json& body, int status = 200);

void serve();
} // namespace bacuri::vms::control
