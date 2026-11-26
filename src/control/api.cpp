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

Response request_response()
{
    Response res;
    std::unique_ptr<char, decltype(&g_free)> id{g_uuid_string_random(), g_free};
    res.set(http::field::cache_control, "no-store");
    res.set("X-Content-Type-Options", "nosniff");
    res.set("X-Request-Id", id.get());
    return res;
}

void problem(Response& res, const Failure& error)
{
    // A cause that the client may repeat after a pause tells it when.
    if (info(error.error).retry == Retry::later && !res.count(http::field::retry_after)) {
        res.set(http::field::retry_after, "1");
    }
    const std::string code{error.code()};
    Json body{{"status", error.status()},
              {"code", code},
              {"title", code},
              {"type", "urn:bacuri:vms:problem:" + code},
              {"request_id", std::string{res["X-Request-Id"]}}};
    body.update(error.fields);
    respond(res, body, error.status());
    res.set(http::field::content_type, "application/problem+json");
}

Json object_body(const Request& request, std::initializer_list<std::string_view> fields)
{
    const auto mime = request[http::field::content_type];
    if (mime.substr(0, mime.find(';')) != "application/json") {
        throw Failure{ErrorCode::invalid_request};
    }
    auto object = Json::parse(request.body(), nullptr, false);
    if (!object.is_object()) {
        throw Failure{ErrorCode::invalid_request};
    }
    for (const auto& [field, value] : object.items()) {
        if (std::ranges::find(fields, field) == fields.end()) {
            throw Failure{ErrorCode::invalid_request};
        }
    }
    return object;
}

std::string string_field(const Json& object, std::string_view field, std::size_t limit, bool allow_empty)
{
    auto it = object.find(field);
    if (it == object.end() || !it->is_string()) {
        throw Failure{ErrorCode::invalid_request};
    }
    const auto& value = it->get_ref<const std::string&>();
    const auto characters = std::ranges::count_if(value, [](unsigned char ch) { return (ch & 0xc0) != 0x80; });
    if ((!allow_empty && value.empty()) || static_cast<std::size_t>(characters) > limit || value.contains('\0')) {
        throw Failure{ErrorCode::invalid_request};
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
    throw Failure{ErrorCode::method_not_allowed};
}
} // namespace bacuri::vms::control
