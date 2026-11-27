#include "services/control/transport/node_probe.hpp"
#include "application/control_api/http_json.hpp"
#include "media/acceleration/settings.hpp"
#include "modules/inventory/operations.hpp"
#include "modules/events/operations.hpp"

namespace bacuri::vms::control
{
namespace
{
acceleration::Settings requested(const Request& req)
{
    const auto body = object_body(req, {"mode", "gpu_tasks"});
    acceleration::Settings settings{string_field(body, "mode", 16), {}};
    if (body.contains("gpu_tasks")) {
        const auto& tasks = body.at("gpu_tasks");
        if (settings.mode != "manual" || !tasks.is_array() || tasks.size() > 7)
            throw Failure{ErrorCode::invalid_request};
        for (const auto& task : tasks) {
            if (!task.is_string())
                throw Failure{ErrorCode::invalid_request};
            settings.gpu_tasks.push_back(task.get<std::string>());
        }
    }
    if (!acceleration::normalize(settings))
        throw Failure{ErrorCode::invalid_request};
    return settings;
}
} // namespace
void node_settings(const Request& req, Response& res, const Sessions& sessions, const std::string& site,
                   const PathMatch& path, bool preview)
{
    if (preview)
        allow(req, res, {http::verb::post});
    else
        allow(req, res, {http::verb::get, http::verb::put});
    const auto& node = path.values.at(0);
    const bool read = req.method() == http::verb::get;
    const bool apply = req.method() == http::verb::put;
    postgresql::Database db;
    db.exec("BEGIN");
    const auto user = authorize_node(db, sessions, req, site, path, !read);
    auto saved = inventory::node_acceleration(db, site, node, apply);
    auto candidate = read ? saved.settings : requested(req);
    if (apply) {
        const auto match = req[http::field::if_match];
        if (match.empty())
            throw Failure{ErrorCode::precondition_required};
        if (match != "\"" + std::to_string(saved.revision) + "\"")
            throw Failure{ErrorCode::precondition_failed};
    }
    Json probe{{"result", nullptr}, {"stale", false}};
    std::optional<ErrorCode> blocked;
    if (candidate.mode != "cpu_only") {
        try {
            const bool leaving_cpu = saved.settings.mode == "cpu_only";
            probe = current_node_probe(node, false);
            if (apply && leaving_cpu &&
                (probe.at("result").is_null() || probe.at("stale") == true ||
                 probe.at("job").at("state") != "completed" || !probe.at("job").value("prepared_for_gpu", false)))
                probe = current_node_probe(node, true);
            if (probe.at("result").is_null())
                blocked = ErrorCode::acceleration_probe_required;
            else if (probe.at("stale") == true)
                blocked = ErrorCode::acceleration_probe_stale;
            if (blocked && preview && leaving_cpu)
                current_node_probe(node, true);
        } catch (const Failure& failure) {
            if (!read || failure.error != ErrorCode::media_service_unavailable)
                throw;
            blocked = failure.error;
        }
    }
    auto result = acceleration::evaluate(candidate, probe);
    if (candidate.mode != "cpu_only" && !blocked && result.at("devices").empty())
        blocked = ErrorCode::acceleration_device_unavailable;
    if (blocked) {
        if (!read)
            throw Failure{*blocked};
        for (auto& decision : result["decisions"])
            if (decision.at("reason") != "not_selected")
                decision["reason"] = info(*blocked).code;
    } else if (!result.at("rejected_tasks").empty()) {
        blocked = ErrorCode::acceleration_budget_exceeded;
        if (apply)
            throw HttpProblem{{*blocked}, {{"rejected_tasks", result.at("rejected_tasks")}}};
    }
    if (apply && candidate != saved.settings) {
        inventory::save_node_acceleration(db, site, node, candidate);
        ++saved.revision;
        events::record_acceleration_change(db, site, user, {node, saved.settings, candidate, saved.revision});
    }
    result.update(Json{{"node_id", node},
                       {"revision", saved.revision},
                       {"mode", candidate.mode},
                       {"gpu_tasks", candidate.gpu_tasks},
                       {"applicable", !blocked},
                       {"issue", blocked ? Json(info(*blocked).code) : Json(nullptr)}});
    db.exec("COMMIT");
    res.set(http::field::etag, "\"" + std::to_string(saved.revision) + "\"");
    respond(res, result);
}
} // namespace bacuri::vms::control
