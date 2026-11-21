#include "control/api.hpp"
#include "control/webauthn.hpp"
#include "control/playback_time.hpp"
#include "archive/evidence.hpp"
#include "control/recovery.hpp"
#include "control/maps.hpp"
#include <sodium.h>
#include <iostream>
#include <print>
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
    db.exec("BEGIN");
    auto site = setting("BACURI_SITE_ID");
    db.exec("INSERT INTO sites VALUES ($1,'Local site','UTC') ON CONFLICT DO NOTHING", {site});
    if (initial) {
        db.exec("SELECT pg_advisory_xact_lock(hashtextextended($1,1))", {site});
        auto existing = db.exec("SELECT username FROM site_roles JOIN local_users USING(user_id) WHERE site_id=$1 "
                                "AND role='administrator'",
                                {site});
        if (existing.size()) {
            auto same = db.exec("SELECT FROM site_roles JOIN local_users USING(user_id) WHERE site_id=$1 AND "
                                "username=$2 AND role='administrator'",
                                {site, username});
            if (!same.size())
                throw std::invalid_argument("Site already has an administrator");
            db.exec("COMMIT");
            return;
        }
    }
    auto user = db.exec("INSERT INTO local_users(username,password_hash) VALUES ($1,$2) RETURNING user_id",
                        {username, password_hash(password)})
                    .get(0);
    db.exec("INSERT INTO site_roles VALUES ($1,$2,$3)", {user, site, role});
    db.exec("COMMIT");
}
} // namespace

int main(int argc, char* argv[])
try {
    using namespace bacuri::vms::control;
    if (argc == 2 && std::string_view{argv[1]} == "--version") {
        std::println("bacuri.vms {}", BACURI_VMS_VERSION);
        return 0;
    }
    if (sodium_init() < 0) {
        throw std::runtime_error("Cannot initialize cryptography");
    }
    std::string_view action = argc > 1 ? argv[1] : "";
    if (action == "serve" && argc == 2) {
        serve();
    } else if (action == "migrate" && argc == 2) {
        migrate();
    } else if (action == "backup-config" && argc == 2) {
        backup_configuration(std::cout);
    } else if (action == "restore-config" && argc == 2) {
        restore_configuration(std::cin);
    } else if (action == "recovery-status" && argc == 2) {
        Database db;
        auto row = db.exec("SELECT reason FROM archive_recovery WHERE site_id=$1", {setting("BACURI_SITE_ID")});
        std::cout << Json{{"archive", row.size() ? row.get(0) : "available"}}.dump() << '\n';
    } else if (action == "set-retention" && (argc == 3 || argc == 4)) {
        const auto days = integer(argv[2], 36500), reserve = argc == 4 ? integer(argv[3], 90) : 20;
        Database db;
        const auto site = setting("BACURI_SITE_ID");
        db.exec("BEGIN");
        db.exec("SELECT pg_advisory_xact_lock(hashtextextended($1,1))", {site});
        db.exec("SELECT bacuri_archive.advance_cutoff($1)", {site});
        db.exec("INSERT INTO archive_retention(site_id,days,reserve_percent) VALUES($1,$2,$3) "
                "ON CONFLICT(site_id) DO UPDATE SET days=excluded.days,reserve_percent=excluded.reserve_percent",
                {site, std::to_string(days), std::to_string(reserve)});
        db.exec("SELECT bacuri_archive.advance_cutoff($1)", {site});
        db.exec("COMMIT");
    } else if (action == "rebuild-archive" && argc == 2) {
        rebuild_archive(std::cout);
    } else if (action == "import-map" && argc == 3) {
        import_map(argv[2]);
    } else if (action == "initial-admin" && argc == 3) {
        provision(argv[2], "administrator", true);
        Database db;
        if (!db.exec("SELECT FROM webauthn_credentials JOIN local_users USING(user_id) WHERE username=$1",
                     {argv[2]})
                 .size())
            std::cout << enrollment_code(argv[2]) << '\n';
    } else if (action == "create-user" && argc == 4) {
        provision(argv[2], argv[3]);
    } else if (action == "export-public-key" && argc == 2) {
        std::cout << bacuri::vms::archive::export_public_key(load_key(setting("BACURI_EXPORT_KEY_FILE")),
                                                             setting("BACURI_SITE_ID"))
                         .dump()
                  << '\n';
    } else if ((action == "grant-export" && argc == 6) || (action == "revoke-export" && argc == 4)) {
        Database db;
        const auto site = setting("BACURI_SITE_ID");
        if (action == "revoke-export") {
            db.exec("DELETE FROM export_grants g USING local_users u WHERE u.username=$1 AND g.user_id=u.user_id "
                    "AND g.site_id=$2 AND g.point_id=$3",
                    {argv[2], site, argv[3]});
        } else {
            const auto a = parse_utc(argv[4]), b = parse_utc(argv[5]);
            if (b <= a)
                throw std::invalid_argument("Invalid export interval");
            auto grant = db.exec(
                "INSERT INTO export_grants(user_id,site_id,point_id,begin_us,end_us) "
                "SELECT v.user_id,v.site_id,v.point_id,$4,$5 FROM visible_points v JOIN local_users u "
                "USING(user_id) "
                "WHERE u.username=$1 AND v.site_id=$2 AND v.point_id=$3 ON CONFLICT(user_id,site_id,point_id) "
                "DO UPDATE SET begin_us=excluded.begin_us,end_us=excluded.end_us,grant_id=excluded.grant_id "
                "RETURNING grant_id",
                {argv[2], site, argv[3], std::to_string(a), std::to_string(b)});
            if (!grant.size())
                throw std::invalid_argument("User has no access to this point");
        }
    } else if (action == "recover-admin" && argc == 3) {
        std::cout << enrollment_code(argv[2], Enrollment::recovery) << '\n';
    } else if (action == "enroll-admin" && argc == 3) {
        std::cout << enrollment_code(argv[2]) << '\n';
    } else if (action == "revoke-point" && argc == 4) {
        Database db;
        db.exec("DELETE FROM point_grants g USING local_users u WHERE g.user_id=u.user_id AND u.username=$1 AND "
                "g.site_id=$2 AND g.point_id=$3",
                {argv[2], setting("BACURI_SITE_ID"), argv[3]});
    } else if (action == "grant-point" && argc == 4) {
        Database db;
        db.exec("BEGIN");
        const auto site = setting("BACURI_SITE_ID");
        auto user = db.exec(R"sql(
            SELECT u.user_id FROM local_users u JOIN site_roles r USING(user_id)
            WHERE u.username=$1 AND r.site_id=$2 AND r.role='operator' FOR SHARE
        )sql",
                            {argv[2], site});
        if (!user.size()) {
            throw std::runtime_error("Operator account not found on this site");
        }
        db.exec("INSERT INTO point_grants VALUES ($1,$2,$3) ON CONFLICT DO NOTHING", {user.get(0), site, argv[3]});
        db.exec("COMMIT");
    } else {
        std::fputs(
            "Usage: bacuri-vms-core --version | serve | migrate | backup-config | restore-config | "
            "recovery-status | set-retention DAYS [RESERVE_PERCENT] | rebuild-archive | import-map "
            "REGION.mbtiles | create-user USER ROLE | "
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
