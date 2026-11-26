#include "platform/rpc_status.hpp"
#include <gtest/gtest.h>

namespace bacuri::vms::platform
{
TEST(RpcStatus, ForwardsOnlyPublicCausesWithTheirCatalogStatus)
{
    for (const auto& entry : error_catalog) {
        SCOPED_TRACE(entry.code);
        const auto cause = parse_error(entry.code);
        if (!cause)
            FAIL() << "Unknown catalog entry";
        const auto status = rpc_status(*cause);
        EXPECT_EQ(status.error_message(), entry.code);
        EXPECT_EQ(status.error_code(), entry.grpc);
        EXPECT_EQ(rpc_problem(status), entry.problem ? cause : std::nullopt);
    }
    EXPECT_FALSE(rpc_problem(grpc::Status::OK));
    EXPECT_FALSE(rpc_problem({grpc::StatusCode::UNAVAILABLE, "connection refused"}));
    EXPECT_FALSE(rpc_problem({grpc::StatusCode::UNAVAILABLE, "video_session_limit"}));
}
} // namespace bacuri::vms::platform
