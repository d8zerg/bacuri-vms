#include "services/control/transport/node_probe.hpp"
#include "application/control_api/http_json.hpp"
#include "modules/access/operations.hpp"
#include "media/lease.hpp"
#include "platform/rpc_status.hpp"

namespace bacuri::vms::control
{
namespace
{
Json call_probe(http::verb method, const v1::NodeProbeRequest& query)
{
    auto stub = v1::NodeAcceleration::NewStub(media::local_channel("BACURI_MEDIA_SOCKET"));
    grpc::ClientContext ctx;
    ctx.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds{2});
    v1::NodeProbeStatus out;
    auto status = method == http::verb::get    ? stub->ProbeStatus(&ctx, query, &out)
                  : method == http::verb::post ? stub->StartProbe(&ctx, query, &out)
                                               : stub->CancelProbe(&ctx, query, &out);
    if (!status.ok())
        throw Failure{platform::rpc_problem(status).value_or(ErrorCode::media_service_unavailable)};
    return Json::parse(out.status_json());
}
} // namespace
Json current_node_probe(const std::string& node, bool prepare)
{
    v1::NodeProbeRequest query;
    query.set_node_id(node);
    query.set_refresh_identity(!prepare);
    query.set_prepare_gpu(prepare);
    return call_probe(prepare ? http::verb::post : http::verb::get, query);
}
std::string authorize_node(postgresql::Database& db, const Sessions& sessions, const Request& req,
                           const std::string& site, const PathMatch& match, bool admin)
{
    const bool write = req.method() != http::verb::get;
    auto identity = sessions.authenticate(db, req, write, write);
    if (access::site_role(db, site, identity.user, access::RoleLock::shared) != "administrator" && admin)
        throw Failure{ErrorCode::forbidden};
    if (site != setting("BACURI_SITE_ID") || match.values.at(0) != setting("BACURI_NODE_ID"))
        throw Failure{ErrorCode::not_found};
    return identity.user;
}
void node_probe(const Request& req, Response& res, const Sessions& sessions, const std::string& site,
                const PathMatch& match)
{
    const auto& node = match.values.at(0);
    const auto probe_id = match.values.size() > 1 ? match.values.at(1) : std::string{};
    if (probe_id.empty())
        allow(req, res, {http::verb::get, http::verb::post});
    else
        allow(req, res, {http::verb::delete_});
    postgresql::Database db;
    db.exec("BEGIN");
    authorize_node(db, sessions, req, site, match, true);
    v1::NodeProbeRequest query;
    query.set_node_id(node);
    query.set_probe_id(probe_id);
    if (!req.body().empty()) {
        if (req.method() != http::verb::post)
            throw Failure{ErrorCode::invalid_request};
        auto body = object_body(req, {"reason"});
        auto reason = string_field(body, "reason", 32);
        if (reason != "administrator" && reason != "prepare_gpu")
            throw Failure{ErrorCode::invalid_request};
        query.set_prepare_gpu(reason == "prepare_gpu");
    }
    const auto result = call_probe(req.method(), query);
    db.exec("COMMIT");
    respond(res, result, req.method() == http::verb::post ? 202 : 200);
}
} // namespace bacuri::vms::control
