#pragma once
// gRPC statuses of the error catalog: the status code follows from the cause, the message carries its code.
#include "errors/catalog.hpp"

#include <grpcpp/support/status.h>

#include <optional>
#include <string>

namespace bacuri::vms::platform
{
inline grpc::Status rpc_status(ErrorCode code)
{
    return {static_cast<grpc::StatusCode>(info(code).grpc), std::string{info(code).code}};
}

// Forward a public cause only when the remote service sends the matching catalog status.
inline std::optional<ErrorCode> rpc_problem(const grpc::Status& status)
{
    if (status.ok())
        return std::nullopt;
    const auto cause = parse_error(status.error_message());
    if (cause && info(*cause).problem && info(*cause).grpc == status.error_code())
        return cause;
    return std::nullopt;
}
} // namespace bacuri::vms::platform
