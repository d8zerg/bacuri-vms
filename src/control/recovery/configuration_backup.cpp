#include "control/events/configuration.hpp"
#include "control/inventory/restore.hpp"
#include "control/access/restore.hpp"
#include "control/recordings/operations.hpp"
#include "control/recordings/configuration.hpp"
#include "control/maps/configuration.hpp"
#include "control/access/configuration.hpp"
#include "control/inventory/configuration.hpp"
#include "control/recovery/recovery.hpp"
#include "control/media_sessions/operations.hpp"
#include "control/database/database.hpp"
#include "control/support.hpp"

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
void visit_configuration(ConfigurationSnapshot& snapshot)
{
    inventory::site_configuration(snapshot);
    access::account_configuration(snapshot);
    inventory::camera_configuration(snapshot);
    access::permission_configuration(snapshot);
    maps::plan_configuration(snapshot);
    recordings::retention_configuration(snapshot);
    events::rule_configuration(snapshot);
    media_sessions::layout_configuration(snapshot);
}
std::string context(const std::string& site)
{
    return "configuration-backup/v1/" + site + "/" + setting("BACURI_NODE_ID");
}

Json snapshot(Database& db)
{
    ConfigurationSnapshot archive{db};
    visit_configuration(archive);
    return std::move(archive.contents);
}

void require_single_site(const Json& data, const std::string& site)
{
    const auto& sites = data.at("sites");
    if (sites.size() != 1 || sites.at(0).at("site_id") != site) {
        throw std::runtime_error("Expected the assigned single-site inventory");
    }
}

constexpr std::size_t maximum_ciphertext = 128uz * 1024 * 1024;
} // namespace

void backup_configuration(std::ostream& output)
{
    Database db;
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
    Database db;
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
        ConfigurationSnapshot archive{db, data};
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
