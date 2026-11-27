#include "storage/postgresql/database.hpp"
#include <nlohmann/json.hpp>
#include "modules/inventory/operations.hpp"
#include "modules/events/operations.hpp"
#include "modules/events/configuration.hpp"
#include "storage/postgresql/configuration_snapshot.hpp"

namespace bacuri::vms::control::events
{
namespace
{
using Json = nlohmann::json;
Json audit_details(const std::monostate&)
{
    return Json::object();
}
Json audit_details(const PolicyRevision& p)
{
    return {{"policy", p.policy}, {"revision", p.revision}};
}
Json audit_details(const ManualDuration& d)
{
    return {{"seconds", d.seconds}};
}
Json audit_details(const StorageSettings& s)
{
    return {{"days", s.days}, {"reserve_percent", s.reserve_percent}};
}
Json audit_details(const FragmentExportAudit& e)
{
    return {{"id", e.id},
            {"interval", {{"start", e.start}, {"end", e.end}}},
            {"mode", e.mode},
            {"reason", e.reason},
            {"audio", e.audio}};
}
Json audit_details(const CaseExportAudit& e)
{
    return {{"case", e.case_id},
            {"revision", e.revision},
            {"mode", e.mode},
            {"interval", {{"start", e.start}, {"end", e.end}}}};
}
template <class T> std::string encode_audit(const T& details)
{
    return std::visit([](const auto& change) { return audit_details(change); }, details).dump();
}
} // namespace
void rule_configuration(postgresql::ConfigurationSnapshot& snapshot)
{
    snapshot.section("bacuri_events.video_rules");
}
std::optional<ArchiveAnchor> archive_anchor(postgresql::QuerySession& db, const std::string& site,
                                            const std::string& alert)
{
    auto r = db.exec("SELECT point_id,binding_revision,time_basis,observed_at_utc_us,uncertainty_us FROM "
                     "bacuri_events.alert_details WHERE site_id=$1 AND alert_id=$2",
                     {site, alert});
    if (!r.size())
        return {};
    return ArchiveAnchor{r.get(0), r.get(0, 1), r.get(0, 2), std::stoll(r.get(0, 3)), r.get(0, 4)};
}
void record_acceleration_change(postgresql::QuerySession& db, const std::string& site, const std::string& user,
                                const NodeAccelerationChange& change)
{
    const Json details{{"node_id", change.node},         {"previous_mode", change.previous.mode},
                       {"mode", change.next.mode},       {"previous_tasks", change.previous.gpu_tasks},
                       {"tasks", change.next.gpu_tasks}, {"revision", change.revision}};
    db.exec("INSERT INTO bacuri_events.alert_audit(site_id,initiator_id,actor_kind,action,outcome,reason,details) "
            "VALUES($1,$2,'user','node_acceleration','applied','acceleration_changed',$3::jsonb)",
            {site, user, details.dump()});
}
void record_camera_control(postgresql::Database& db, const std::string& site, const std::string& point,
                           const std::string& user, const std::string& outcome, const std::string& command,
                           const std::string& target)
{
    db.exec(
        "INSERT INTO bacuri_events.alert_audit(site_id,point_id,initiator_id,actor_kind,action,outcome,reason) "
        "VALUES($1,$2,$3,'user','camera_control',$4,$5)",
        {site, point, user, outcome, target.empty() ? command : command + " / " + target});
}
void record_recording_change(postgresql::QuerySession& db, const std::string& site, const std::string& point,
                             const std::string& user, const std::string& action, const std::string& change,
                             const RecordingChange& details)
{
    db.exec("INSERT INTO bacuri_events.alert_audit(site_id,point_id,initiator_id,actor_kind,action,outcome,reason,"
            "details) VALUES($1,nullif($2,'')::uuid,$3,'user',$4,'applied',$5,$6::jsonb)",
            {site, point, user, action, change, encode_audit(details)});
}
void record_export(postgresql::Database& db, const std::string& site, const std::string& point,
                   const std::string& user, const std::string& alert, const std::string& outcome,
                   const std::string& reason, const ExportAudit& details)
{
    if (!inventory::site_exists(db, site))
        return;
    db.exec("INSERT INTO "
            "bacuri_events.alert_audit(site_id,point_id,initiator_id,alert_id,actor_kind,action,outcome,reason,"
            "export_details) "
            "VALUES($1,$2,$3,nullif($4,'')::uuid,'user','export',$5,$6,$7::jsonb)",
            {site, point, user, alert, outcome, reason, encode_audit(details)});
}
} // namespace bacuri::vms::control::events
