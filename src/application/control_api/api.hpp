#pragma once

#include "storage/postgresql/database.hpp"
#include "application/control_api/http.hpp"
#include "application/control_api/support.hpp"
#include "errors/catalog.hpp"

namespace bacuri::vms::control
{
// Shared error code for a failed operation.
struct Failure {
    ErrorCode error;

    [[nodiscard]] int status() const
    {
        return info(error).http;
    }
    [[nodiscard]] std::string_view code() const
    {
        return info(error).code;
    }
};

void allow(const Request&, Response&, std::initializer_list<http::verb>);
Response request_response();
void problem(Response&, const Failure&);

} // namespace bacuri::vms::control
