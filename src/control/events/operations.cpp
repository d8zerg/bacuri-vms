#include "control/database.hpp"
#include "control/inventory/operations.hpp"
#include "control/events/operations.hpp"

namespace bacuri::vms::control::events
{
std::optional<ArchiveAnchor> archive_anchor(Database& db, const std::string& site, const std::string& alert)
{
    auto r = db.exec("SELECT point_id,binding_revision,time_basis,observed_at_utc_us,uncertainty_us FROM "
                     "bacuri_events.alert_details WHERE site_id=$1 AND alert_id=$2",
                     {site, alert});
    if (!r.size())
        return {};
    return ArchiveAnchor{r.get(0), r.get(0, 1), r.get(0, 2), std::stoll(r.get(0, 3)), r.get(0, 4)};
}
void record_camera_control(Database& db, const std::string& site, const std::string& point,
                           const std::string& user, const std::string& outcome, const std::string& command,
                           const std::string& target)
{
    db.exec(
        "INSERT INTO bacuri_events.alert_audit(site_id,point_id,initiator_id,actor_kind,action,outcome,reason) "
        "VALUES($1,$2,$3,'user','camera_control',$4,$5)",
        {site, point, user, outcome, target.empty() ? command : command + " / " + target});
}
void record_export(Database& db, const std::string& site, const std::string& point, const std::string& user,
                   const std::string& alert, const std::string& outcome, const std::string& reason,
                   const std::string& details)
{
    if (!inventory::site_exists(db, site))
        return;
    db.exec("INSERT INTO "
            "bacuri_events.alert_audit(site_id,point_id,initiator_id,alert_id,actor_kind,action,outcome,reason,"
            "export_details) "
            "VALUES($1,$2,$3,nullif($4,'')::uuid,'user','export',$5,$6,$7::jsonb)",
            {site, point, user, alert, outcome, reason, details});
}
} // namespace bacuri::vms::control::events
