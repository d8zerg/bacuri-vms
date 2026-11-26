#include "modules/access/session.hpp"

#include <gtest/gtest.h>

namespace bacuri::vms::control
{
TEST(AccessSession, LoginRequiresOneMatchingOriginBeforeReadingCredentials)
{
    const std::string site{"https://site.local"};
    const Sessions sessions{Key{}, site};
    Request request{http::verb::post, "/api/v1/session", 11};
    Response response;
    const auto expect_failure = [&](ErrorCode expected) {
        try {
            sessions.handle(request, response, "127.0.0.1");
            FAIL() << "Login accepted a request without credentials";
        } catch (const Failure& failure) {
            EXPECT_EQ(failure.error, expected);
        }
    };
    expect_failure(ErrorCode::forbidden);
    request.set(http::field::origin, "https://other.local");
    expect_failure(ErrorCode::forbidden);
    request.set(http::field::origin, site);
    request.insert(http::field::origin, site);
    expect_failure(ErrorCode::forbidden);
    request.set(http::field::origin, site);
    expect_failure(ErrorCode::invalid_request);
}
} // namespace bacuri::vms::control
