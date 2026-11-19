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
    grpc::Status RegisterGap(grpc::ServerContext*, const v1::RegisterGapRequest*,
                             v1::RegistrationResult*) override;
    grpc::Status RegisterBlock(grpc::ServerContext*, const v1::RegisterBlockRequest*,
                               v1::RegistrationResult*) override;

  private:
    archive::Owner owner_;
    std::unique_ptr<grpc::Server> server_;
};
} // namespace bacuri::vms::control
