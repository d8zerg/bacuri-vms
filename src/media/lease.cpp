#include "media/lease.hpp"
#include "control/support.hpp"
#include <algorithm>

namespace bacuri::vms::media
{
using namespace std::chrono_literals;
std::shared_ptr<grpc::Channel> local_channel(const char* setting_name)
{
    return grpc::CreateChannel("unix:" + control::setting(setting_name),
                               grpc::experimental::LocalCredentials(UDS));
}
Lease::Lease(v1::AuthorizeDeliveryRequest scope)
    : scope_{std::move(scope)}, authority_{v1::MediaAuthorization::NewStub(local_channel("BACURI_AUTH_SOCKET"))}
{
}
Clock::time_point Lease::deadline()
{
    const auto start = Clock::now();
    if (start >= check_ || start >= expires_) {
        grpc::ClientContext context;
        context.set_deadline(std::chrono::system_clock::now() + 1s);
        v1::DeliveryLease lease;
        const auto status = authority_->AuthorizeDelivery(&context, scope_, &lease);
        if (status.ok()) {
            expires_ = start + std::min(5000ms, std::chrono::milliseconds{lease.valid_for_ms()});
        } else if (status.error_code() != grpc::StatusCode::UNAVAILABLE &&
                   status.error_code() != grpc::StatusCode::DEADLINE_EXCEEDED) {
            expires_ = start;
        }
        check_ = start + 1s;
    }
    // Renew before the hard two-second check boundary so a healthy slow write can finish.
    const auto deadline = std::min(check_ + 1s, expires_);
    if (Clock::now() >= deadline) {
        throw std::runtime_error("Archive delivery authorization expired");
    }
    return deadline;
}
} // namespace bacuri::vms::media
