#pragma once
#include "control.grpc.pb.h"
#include <grpcpp/grpcpp.h>
#include <chrono>

namespace bacuri::vms::media
{
using Clock = std::chrono::steady_clock;
inline constexpr std::size_t chunk_size = 32uz * 1024;
std::shared_ptr<grpc::Channel> local_channel(const char* setting_name);

class Lease
{
  public:
    explicit Lease(v1::AuthorizeDeliveryRequest scope);
    // Every write must finish by this deadline, including writes blocked on backpressure.
    Clock::time_point deadline();

  private:
    v1::AuthorizeDeliveryRequest scope_;
    std::unique_ptr<v1::MediaAuthorization::Stub> authority_;
    Clock::time_point expires_{}, check_{};
};
} // namespace bacuri::vms::media
