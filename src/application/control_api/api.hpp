#pragma once

#include "storage/postgresql/database.hpp"
#include "application/control_api/http.hpp"
#include "application/control_api/support.hpp"
#include "errors/catalog.hpp"
#include <nlohmann/json.hpp>

namespace bacuri::vms::control
{
using Json = nlohmann::json;

// A failure of a request: a cause of the error catalog and its parameters for the Problem response.
struct Failure {
    ErrorCode error;
    Json fields = Json::object();

    [[nodiscard]] int status() const
    {
        return info(error).http;
    }
    [[nodiscard]] std::string_view code() const
    {
        return info(error).code;
    }
};

Json object_body(const Request& request, std::initializer_list<std::string_view> fields);
std::string string_field(const Json& object, std::string_view field, std::size_t limit, bool allow_empty = false);
void allow(const Request&, Response&, std::initializer_list<http::verb>);
Response request_response();
void problem(Response&, const Failure&);
void respond(Response& response, const Json& body, int status = 200);

} // namespace bacuri::vms::control
