#include "media/lease.hpp"
#include "control/support.hpp"
#include <algorithm>
#include <cstdio>

namespace bacuri::vms::media
{
using namespace std::chrono_literals;
std::shared_ptr<grpc::Channel> local_channel(const char* setting_name)
{
    grpc::ChannelArguments args;
    args.SetMaxReceiveMessageSize(8 * 1024 * 1024 + 1024);
    return grpc::CreateCustomChannel("unix:" + control::setting(setting_name),
                                     grpc::experimental::LocalCredentials(UDS), args);
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
        if (expires_ != Clock::time_point{} && start >= expires_) {
            throw std::runtime_error("Media authorization expired");
        }
        const auto timeout = expires_ == Clock::time_point{} ? Clock::duration{1s}
                                                             : std::min(Clock::duration{1s}, expires_ - start);
        context.set_deadline(std::chrono::system_clock::now() + timeout);
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
        throw std::runtime_error("Media authorization expired");
    }
    return deadline;
}
void Lease::close_live() noexcept
{
    try {
        grpc::ClientContext context;
        context.set_deadline(std::chrono::system_clock::now() + 1s);
        google::protobuf::Empty closed;
        if (!authority_->CloseLive(&context, scope_, &closed).ok()) {
            std::fputs("Live session close deferred to expiry\n", stderr);
        }
    } catch (const std::exception&) {
        std::fputs("Cannot issue live session close RPC\n", stderr);
    }
}
} // namespace bacuri::vms::media
