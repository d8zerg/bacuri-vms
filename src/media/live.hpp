#pragma once
#include "control.grpc.pb.h"
#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <thread>

namespace bacuri::vms::media
{
class LiveDelivery final : public v1::LiveDelivery::Service
{
  public:
    grpc::Status Negotiate(grpc::ServerContext*, const v1::LiveOffer*, v1::LiveAnswer*) override;

  private:
    struct Peer {
        std::atomic_bool done{};
        std::jthread worker;
    };
    std::mutex mutex_;
    std::map<std::string, std::unique_ptr<Peer>> peers_;
};
} // namespace bacuri::vms::media
