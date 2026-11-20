#include "control/api.hpp"
#include "control/recovery.hpp"
#include <sodium.h>
#include <iostream>
#include <print>

namespace
{
using namespace bacuri::vms::control;

void provision(const std::string& username, const std::string& role)
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
    } else if (action == "rebuild-archive" && argc == 2) {
        rebuild_archive(std::cout);
    } else if (action == "create-user" && argc == 4) {
        provision(argv[2], argv[3]);
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
        std::fputs("Usage: bacuri-vms-core --version | serve | migrate | backup-config | restore-config | "
                   "recovery-status | rebuild-archive | create-user USER ROLE | grant-point USER "
                   "POINT\n",
                   stderr);
        return 1;
    }
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
