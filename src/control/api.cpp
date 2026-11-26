// Request parsing and response helpers shared by the HTTP handlers of all modules.
#include "control/api.hpp"

#include <algorithm>

namespace bacuri::vms::control
{
void respond(Response& response, const Json& body, int status)
{
    response.result(static_cast<unsigned>(status));
    response.set(http::field::content_type, "application/json");
    response.body() = body.dump();
}

Json object_body(const Request& request, std::initializer_list<std::string_view> fields)
{
    const auto mime = request[http::field::content_type];
    if (mime.substr(0, mime.find(';')) != "application/json") {
        throw Failure{400, "invalid_request"};
    }
    auto object = Json::parse(request.body(), nullptr, false);
    if (!object.is_object()) {
        throw Failure{400, "invalid_request"};
    }
    for (const auto& [field, value] : object.items()) {
        if (std::ranges::find(fields, field) == fields.end()) {
            throw Failure{400, "invalid_request"};
        }
    }
    return object;
}

std::string string_field(const Json& object, std::string_view field, std::size_t limit, bool allow_empty)
{
    auto it = object.find(field);
    if (it == object.end() || !it->is_string()) {
        throw Failure{400, "invalid_request"};
    }
    const auto& value = it->get_ref<const std::string&>();
    const auto characters = std::ranges::count_if(value, [](unsigned char ch) { return (ch & 0xc0) != 0x80; });
    if ((!allow_empty && value.empty()) || static_cast<std::size_t>(characters) > limit || value.contains('\0')) {
        throw Failure{400, "invalid_request"};
    }
    return value;
}

void allow(const Request& req, Response& res, std::initializer_list<http::verb> methods)
{
    if (std::ranges::find(methods, req.method()) != methods.end()) {
        return;
    }
    std::string permitted;
    for (const auto method : methods) {
        if (!permitted.empty()) {
            permitted += ", ";
        }
        permitted += std::string{http::to_string(method)};
    }
    res.set(http::field::allow, permitted);
    throw Failure{405, "invalid_request"};
}
} // namespace bacuri::vms::control
