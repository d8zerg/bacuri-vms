#pragma once
#include "control.grpc.pb.h"
#include "media/acceleration/probe.hpp"
#include "platform/rpc_status.hpp"

namespace bacuri::vms::media
{
class ProbeService final : public v1::NodeAcceleration::Service
{
  public:
    ProbeService(std::string node, const std::filesystem::path& directory, std::atomic_uint& activity)
        : node_{std::move(node)}, probe_{node_, directory, activity}
    {
    }
    grpc::Status ProbeStatus(grpc::ServerContext*, const v1::NodeProbeRequest* request,
                             v1::NodeProbeStatus* reply) override
    {
        if (request->node_id() != node_)
            return platform::rpc_status(ErrorCode::not_found);
        reply->set_status_json(probe_.status(request->refresh_identity()).dump());
        return grpc::Status::OK;
    }
    grpc::Status StartProbe(grpc::ServerContext*, const v1::NodeProbeRequest* request,
                            v1::NodeProbeStatus* reply) override
    {
        if (request->node_id() != node_)
            return platform::rpc_status(ErrorCode::not_found);
        reply->set_status_json(probe_.start(request->prepare_gpu()).dump());
        return grpc::Status::OK;
    }
    grpc::Status CancelProbe(grpc::ServerContext*, const v1::NodeProbeRequest* request,
                             v1::NodeProbeStatus* reply) override
    {
        nlohmann::json status;
        if (request->node_id() != node_ || !probe_.cancel(request->probe_id(), status))
            return platform::rpc_status(ErrorCode::not_found);
        reply->set_status_json(status.dump());
        return grpc::Status::OK;
    }

  private:
    std::string node_;
    acceleration::Probe probe_;
};
} // namespace bacuri::vms::media
