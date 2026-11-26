#include "control/inventory/operations.hpp"
#include "control/access/operations.hpp"
#include "control/recordings/operations.hpp"
#include "control/api.hpp"
#include "control/access/enrollment.hpp"
#include "control/access/oidc.hpp"
#include "control/playback_time.hpp"
#include "archive/evidence.hpp"
#include "control/recovery.hpp"
#include "control/maps.hpp"
#include "observability/metrics.hpp"
#include "product_version.hpp"
#include <sodium.h>
#include <iostream>
#include <charconv>

namespace
{
using namespace bacuri::vms::control;

int integer(std::string_view value, int maximum)
{
    int result{};
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), result);
    if (error != std::errc{} || end != value.data() + value.size() || result < 1 || result > maximum)
        throw std::invalid_argument("Invalid archive retention parameter");
    return result;
}

void provision(const std::string& username, const std::string& role, bool initial = false)
{
    std::string password;
    if (username.empty() || username.size() > 128 || (role != "administrator" && role != "operator") ||
        !std::getline(std::cin, password) || password.empty() || password.size() > 1024) {
        throw std::invalid_argument("Invalid account parameters");
    }
    Database db;
    const auto site = setting("BACURI_SITE_ID");
    db.exec("BEGIN");
    inventory::ensure_site(db, site);
    access::provision_user(db, site, {.username = username, .role = role}, password, initial);
    db.exec("COMMIT");
}
} // namespace

int main(int argc, char* argv[])
try {
    using namespace bacuri::vms::control;
    if (argc == 2 && std::string_view{argv[1]} == "--version") {
        std::cout << "bacuri.vms " << BACURI_VMS_VERSION << '\n';
        return 0;
    }
    if (sodium_init() < 0) {
        throw std::runtime_error("Cannot initialize cryptography");
    }
    std::string_view action = argc > 1 ? argv[1] : "";
    if (action == "serve" && argc == 2) {
        const auto metrics =
            bacuri::vms::observability::export_process_metrics(std::getenv("BACURI_METRICS_DIRECTORY"), "core");
        serve();
    } else if (action == "migrate" && argc == 2) {
        migrate();
    } else if (action == "backup-config" && argc == 2) {
        backup_configuration(std::cout);
    } else if (action == "restore-config" && argc == 2) {
        restore_configuration(std::cin);
    } else if (action == "recovery-status" && argc == 2) {
        Database db;
        auto status = recordings::recovery_status(db, setting("BACURI_SITE_ID"));
        std::cout << Json{{"archive", status}}.dump() << '\n';
    } else if (action == "set-retention" && (argc == 3 || argc == 4)) {
        const auto days = integer(argv[2], 36500), reserve = argc == 4 ? integer(argv[3], 90) : 20;
        Database db;
        const auto site = setting("BACURI_SITE_ID");
        recordings::set_retention(db, site, days, reserve);
    } else if (action == "rebuild-archive" && argc == 2) {
        rebuild_archive(std::cout);
    } else if (action == "import-map" && argc == 3) {
        import_map(argv[2]);
    } else if (action == "initial-admin" && argc == 3) {
        provision(argv[2], "administrator", true);
        Database db;
        if (!access::has_authenticator(db, argv[2]))
            std::cout << enrollment_code(argv[2]) << '\n';
    } else if (action == "create-user" && argc == 4) {
        provision(argv[2], argv[3]);
    } else if ((action == "configure-oidc" && argc == 5) || (action == "bind-identity" && argc == 5)) {
        Database db;
        const auto site = setting("BACURI_SITE_ID");
        db.exec("BEGIN");
        access::lock_policy(db, site);
        if (action == "configure-oidc") {
            std::string secret;
            if (!std::getline(std::cin, secret))
                throw std::invalid_argument("Expected client secret on stdin");
            access::configure_provider(db, load_key(setting("BACURI_KEY_FILE")), site,
                                       {{"enabled", true},
                                        {"issuer", argv[2]},
                                        {"client_id", argv[3]},
                                        {"admin_acr", argv[4]},
                                        {"client_secret", secret}});
        } else {
            access::bind_identity(db, site, argv[2], {{"issuer", argv[3]}, {"subject", argv[4]}});
        }
        db.exec("COMMIT");
    } else if (action == "export-public-key" && argc == 2) {
        std::cout << bacuri::vms::archive::export_public_key(load_key(setting("BACURI_EXPORT_KEY_FILE")),
                                                             setting("BACURI_SITE_ID"))
                         .dump()
                  << '\n';
    } else if ((action == "grant-export" && argc == 6) || (action == "revoke-export" && argc == 4)) {
        Database db;
        const auto site = setting("BACURI_SITE_ID");
        db.exec("BEGIN");
        access::lock_policy(db, site);
        if (action == "revoke-export") {
            access::revoke_export(db, {.site = site, .username = argv[2], .point = argv[3]});
        } else {
            const auto a = parse_utc(argv[4]), b = parse_utc(argv[5]);
            if (!inventory::lock_point(db, site, argv[3]))
                throw std::invalid_argument("User has no access to this point");
            access::grant_export(db, {.site = site, .username = argv[2], .point = argv[3]}, a, b);
        }
        db.exec("COMMIT");
    } else if (action == "recover-admin" && argc == 3) {
        std::cout << enrollment_code(argv[2], Enrollment::recovery) << '\n';
    } else if (action == "enroll-admin" && argc == 3) {
        std::cout << enrollment_code(argv[2]) << '\n';
    } else if (action == "revoke-point" && argc == 4) {
        Database db;
        const auto site = setting("BACURI_SITE_ID");
        db.exec("BEGIN");
        access::lock_policy(db, site);
        access::revoke_point(db, {.site = site, .username = argv[2], .point = argv[3]});
        db.exec("COMMIT");
    } else if (action == "grant-point" && argc == 4) {
        Database db;
        access::grant_point(db, {.site = setting("BACURI_SITE_ID"), .username = argv[2], .point = argv[3]});
    } else {
        std::fputs(
            "Usage: bacuri-vms-core --version | serve | migrate | backup-config | restore-config | "
            "recovery-status | set-retention DAYS [RESERVE_PERCENT] | rebuild-archive | import-map "
            "REGION.mbtiles | create-user USER ROLE | "
            "configure-oidc ISSUER CLIENT_ID ADMIN_ACR | bind-identity USER_ID ISSUER SUBJECT | "
            "enroll-admin USER | recover-admin USER | revoke-point USER POINT | grant-point USER "
            "POINT | grant-export USER POINT START_UTC END_UTC | revoke-export USER POINT | export-public-key\n",
            stderr);
        return 1;
    }
    return 0;
} catch (const bacuri::vms::control::Failure& error) {
    std::cerr << error.code << '\n';
    return 1;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
