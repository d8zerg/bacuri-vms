#pragma once
#include "control.grpc.pb.h"
#include <grpcpp/server.h>
namespace bacuri::vms::control
{
grpc::Status store_observation(const v1::SubmitObservationRequest&, v1::RegistrationResult&,
                               std::int64_t camera_revision);
class Observations final : public v1::ObservationIngest::Service
{
  public:
    Observations();
    ~Observations() override;
    grpc::Status SubmitObservation(grpc::ServerContext*, const v1::SubmitObservationRequest*,
                                   v1::RegistrationResult*) override;

  private:
    std::string site_, source_;
    std::unique_ptr<grpc::Server> server_;
};
} // namespace bacuri::vms::control
