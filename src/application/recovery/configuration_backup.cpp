#include "modules/events/configuration.hpp"
#include "modules/investigations/cases.hpp"
#include "modules/inventory/restore.hpp"
#include "modules/access/restore.hpp"
#include "modules/recordings/operations.hpp"
#include "modules/recordings/configuration.hpp"
#include "modules/maps/configuration.hpp"
#include "modules/access/configuration.hpp"
#include "modules/inventory/configuration.hpp"
#include "application/recovery/recovery.hpp"
#include "application/recovery/site.hpp"
#include "modules/media_sessions/operations.hpp"
#include "storage/postgresql/database.hpp"
#include "application/control_api/support.hpp"

#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <istream>
#include <ostream>

namespace bacuri::vms::control
{
namespace
{
using Json = nlohmann::json;
void visit_configuration(postgresql::ConfigurationSnapshot& snapshot)
{
    inventory::site_configuration(snapshot);
    access::account_configuration(snapshot);
    inventory::camera_configuration(snapshot);
    access::permission_configuration(snapshot);
    maps::plan_configuration(snapshot);
    recordings::retention_configuration(snapshot);
    investigations::case_configuration(snapshot);
    events::rule_configuration(snapshot);
    media_sessions::layout_configuration(snapshot);
}
std::string context(const std::string& site)
{
    return "configuration-backup/v1/" + site + "/" + setting("BACURI_NODE_ID");
}

Json snapshot(postgresql::Database& db)
{
    postgresql::ConfigurationSnapshot archive{db};
    visit_configuration(archive);
    return std::move(archive.contents);
}

constexpr std::size_t maximum_ciphertext = 128uz * 1024 * 1024;
} // namespace

void backup_configuration(std::ostream& output)
{
    postgresql::Database db;
    db.exec("BEGIN ISOLATION LEVEL REPEATABLE READ READ ONLY; SET LOCAL TIME ZONE 'UTC'");
    const auto site = setting("BACURI_SITE_ID");
    const auto data = snapshot(db);
    require_single_site(data, site);
    const auto encrypted = seal(load_key(setting("BACURI_KEY_FILE")), context(site), data.dump());
    if (encrypted.size() > maximum_ciphertext) {
        throw std::runtime_error("Configuration snapshot exceeds backup limit");
    }
    db.exec("COMMIT");
    output << encrypted << '\n' << std::flush;
    if (!output) {
        throw std::runtime_error("Cannot write configuration backup");
    }
}

void restore_configuration(std::istream& input)
{
    const auto site = setting("BACURI_SITE_ID");
    std::string encrypted;
    std::array<char, 8192> buffer{};
    while (input.read(buffer.data(), buffer.size()) || input.gcount() > 0) {
        encrypted.append(buffer.data(), static_cast<std::size_t>(input.gcount()));
        if (encrypted.size() > maximum_ciphertext + 1) {
            throw std::runtime_error("Configuration backup exceeds size limit");
        }
    }
    if (!input.eof() || encrypted.empty() || encrypted.back() != '\n') {
        throw std::runtime_error("Incomplete configuration backup");
    }
    encrypted.pop_back();
    auto data = Json::parse(unseal(encrypted, load_key(setting("BACURI_KEY_FILE")), context(site)));
    data.erase("webauthn_credentials");
    for (const auto* table :
         {"video_rules",         "investigations",         "archive_holds",        "held_blocks",
          "recording_policies",  "monitor_layouts",        "camera_templates",     "camera_batch_items",
          "credential_audit",    "camera_transport_audit", "floor_plans",          "point_placements",
          "archive_retention",   "export_grants",          "map_catalog_settings", "resource_groups",
          "group_points",        "scoped_grants",          "privacy_policies",     "identity_providers",
          "external_identities", "policy_versions",        "access_audit"}) {
        if (!data.contains(table))
            data[table] = Json::array();
    }
    for (auto& row : data.at("archive_retention"))
        if (!row.contains("revision"))
            row["revision"] = 1;
    for (auto& role : data.at("site_roles"))
        if (!role.contains("enabled"))
            role["enabled"] = true;
    require_single_site(data, site);
    postgresql::Database db;
    db.exec("BEGIN ISOLATION LEVEL SERIALIZABLE; SET LOCAL TIME ZONE 'UTC'");
    db.exec("SELECT pg_advisory_xact_lock(hashtextextended($1,1))", {site});
    const auto existing = snapshot(db);
    if (std::ranges::any_of(existing, [](const Json& rows) { return !rows.empty(); })) {
        auto configuration = existing;
        // Preserve newer audit entries and policy revisions on an already configured site.
        for (const auto* field : {"credential_audit", "access_audit", "policy_versions"})
            configuration[field] = data.at(field);
        if (configuration != data) {
            throw std::runtime_error("Restore requires a clean database or an identical inventory");
        }
    } else {
        db.exec("SET LOCAL bacuri.restoring='yes'");
        postgresql::ConfigurationSnapshot archive{db, data};
        visit_configuration(archive);
    }
    recordings::await_verification(db, site);
    // A repeated restore may target an intact database and a replacement recorder.
    db.exec(inventory::reset_recorder);
    db.exec(access::restore_search_path);
    db.exec(access::audit_restore);
    db.exec(access::reset_sessions);
    db.exec(access::reset_oidc_requests);
    db.exec("COMMIT");
}
} // namespace bacuri::vms::control
