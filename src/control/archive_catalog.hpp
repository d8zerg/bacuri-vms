#pragma once
#include "archive/manifest.hpp"
#include "control.grpc.pb.h"
#include <grpcpp/server.h>

namespace bacuri::vms::control
{
class ArchiveCatalog final : public v1::ArchiveCatalog::Service
{
  public:
    explicit ArchiveCatalog(archive::Owner owner);
    ~ArchiveCatalog() override;
    grpc::Status AdvanceRetention(grpc::ServerContext*, const v1::RetentionQuery*, v1::RetentionPolicy*) override;
    grpc::Status ReportMotion(grpc::ServerContext*, const v1::SubmitObservationRequest*,
                              v1::RegistrationResult*) override;
    grpc::Status NextIndex(grpc::ServerContext*, const v1::RetentionQuery*, v1::IndexJob*) override;
    grpc::Status CommitIndex(grpc::ServerContext*, const v1::IndexResult*, google::protobuf::Empty*) override;
    grpc::Status RegisterGap(grpc::ServerContext*, const v1::RegisterGapRequest*,
                             v1::RegistrationResult*) override;
    grpc::Status RegisterBlock(grpc::ServerContext*, const v1::RegisterBlockRequest*,
                               v1::RegistrationResult*) override;

  private:
    archive::Owner owner_;
    std::unique_ptr<grpc::Server> server_;
};
} // namespace bacuri::vms::control
