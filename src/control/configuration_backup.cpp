#include "control/recovery.hpp"
#include "control/database.hpp"
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
// Table order follows inventory foreign keys. Session tokens and delivery attempts are transient.
constexpr std::array tables{"sites",
                            "local_users",
                            "observation_points",
                            "devices",
                            "channels",
                            "stream_profiles",
                            "point_bindings",
                            "configuration_versions",
                            "camera_registry",
                            "registration_requests",
                            "site_roles",
                            "point_grants",
                            "configuration_audit",
                            "camera_transport_audit",
                            "configuration_progress",
                            "recorder_delivery"};

std::string context(const std::string& site)
{
    return "configuration-backup/v1/" + site + "/" + setting("BACURI_NODE_ID");
}

Json snapshot(Database& db)
{
    Json result = Json::object();
    for (const std::string_view name : tables) {
        // On a new recorder the desired configuration must be applied and acknowledged again.
        std::string source{name};
        if (name == "configuration_progress") {
            source = "(SELECT site_id,device_id,desired_revision,NULL::bigint AS applied_revision,"
                     "'pending' AS application_state,NULL::text AS failure_code FROM configuration_progress)";
        } else if (name == "recorder_delivery") {
            source = "(SELECT site_id,desired_revision,0::bigint AS decided_revision FROM recorder_delivery)";
        }
        const auto query =
            "SELECT coalesce(jsonb_agg(to_jsonb(t) ORDER BY to_jsonb(t)),'[]') FROM " + source + " t";
        result[name] = Json::parse(db.exec(query.c_str()).get(0));
    }
    return result;
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
    if (!data.contains("camera_transport_audit"))
        data["camera_transport_audit"] = Json::array();
    require_single_site(data, site);
    Database db;
    db.exec("BEGIN ISOLATION LEVEL SERIALIZABLE; SET LOCAL TIME ZONE 'UTC'");
    db.exec("SELECT pg_advisory_xact_lock(hashtextextended($1,1))", {site});
    const auto existing = snapshot(db);
    if (std::ranges::any_of(existing, [](const Json& rows) { return !rows.empty(); })) {
        if (existing != data) {
            throw std::runtime_error("Restore requires a clean database or an identical inventory");
        }
    } else {
        for (const std::string name : tables) {
            const auto query =
                "INSERT INTO " + name + " SELECT * FROM jsonb_populate_recordset(NULL::" + name + ",$1::jsonb)";
            db.exec(query.c_str(), {data.at(name).dump()});
        }
    }
    db.exec("INSERT INTO archive_recovery VALUES($1,'awaiting_verification') ON CONFLICT(site_id) "
            "DO UPDATE SET reason=excluded.reason",
            {site});
    // A repeated restore may target an intact database and a replacement recorder.
    db.exec(
        "DELETE FROM recorder_attempt; UPDATE recorder_delivery SET decided_revision=0; "
        "UPDATE configuration_progress SET applied_revision=NULL,application_state='pending',failure_code=NULL; "
        "DELETE FROM browser_sessions");
    db.exec("COMMIT");
}
} // namespace bacuri::vms::control
