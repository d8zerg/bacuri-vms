#pragma once
#include "control.grpc.pb.h"
#include <atomic>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <thread>

namespace bacuri::vms::media
{
class LiveDelivery final : public v1::LiveDelivery::Service
{
  public:
    explicit LiveDelivery(std::atomic_uint& activity) : activity_{activity} {}
    grpc::Status Negotiate(grpc::ServerContext*, const v1::LiveOffer*, v1::LiveAnswer*) override;
    grpc::Status Close(grpc::ServerContext*, const v1::LiveSessionRef*, google::protobuf::Empty*) override;

  private:
    std::atomic_uint& activity_;
    struct Peer {
        std::string browser;
        std::shared_future<void> stopped;
        std::jthread worker;
    };
    std::mutex mutex_;
    std::map<std::string, std::unique_ptr<Peer>> peers_;
};
} // namespace bacuri::vms::media
