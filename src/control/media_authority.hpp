#pragma once
#include "control.grpc.pb.h"
#include <grpcpp/server.h>

namespace bacuri::vms::control
{
class MediaAuthority final : public v1::MediaAuthorization::Service
{
  public:
    MediaAuthority();
    MediaAuthority(const MediaAuthority&) = delete;
    MediaAuthority& operator=(const MediaAuthority&) = delete;
    ~MediaAuthority() override;
    grpc::Status CloseLive(grpc::ServerContext*, const v1::AuthorizeDeliveryRequest*,
                           google::protobuf::Empty*) override;
    grpc::Status AuthorizeDelivery(grpc::ServerContext*, const v1::AuthorizeDeliveryRequest*,
                                   v1::DeliveryLease*) override;

  private:
    std::unique_ptr<grpc::Server> server_;
};
} // namespace bacuri::vms::control
