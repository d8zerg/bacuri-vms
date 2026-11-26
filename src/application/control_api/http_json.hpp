#pragma once
#include "application/control_api/api.hpp"
#include <nlohmann/json.hpp>

namespace bacuri::vms::control
{
using Json = nlohmann::json;
struct HttpProblem : Failure {
    Json fields;
};

Json object_body(const Request& request, std::initializer_list<std::string_view> fields);
std::string string_field(const Json& object, std::string_view field, std::size_t limit, bool allow_empty = false);
void respond(Response& response, const Json& body, int status = 200);
void problem(Response&, const HttpProblem&);
} // namespace bacuri::vms::control
