#include "control/inventory/restore.hpp"
#include "control/access/restore.hpp"
#include "maintenance.hpp"
#include "control/database.hpp"
#include "control/recovery.hpp"
#include "control/support.hpp"
#include "storage/sqlite.hpp"
#include <nlohmann/json.hpp>
#include <sodium.h>
#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#include <iostream>

namespace bacuri::vms::maintenance
{
namespace
{
using Json = nlohmann::json;
const fs::path config = "/etc/bacuri-vms", archive = "/srv/bacuri-archive/recorder",
               state = "/var/lib/bacuri-maintenance", pending = state / "pending";
const std::array<std::string, 3> units{"bacuri-core.service", "bacuri-media.service", "bacuri-recorder.service"};
Args postgres(const std::string& command)
{
    return {"/usr/sbin/runuser", "-u", "postgres", "--", "/usr/bin/" + command};
}
Args psql(const std::string& db)
{
    auto args = postgres("psql");
    args.insert(args.end(), {"-XqAt", "-v", "ON_ERROR_STOP=1", "-d", db});
    return args;
}
std::string sql(const std::string& query)
{
    auto args = psql("bacuri_vms");
    args.insert(args.end(), {"-c", query});
    return capture(args);
}
int schema_for(std::string v)
{
    if (v.ends_with("~qualification"))
        v.resize(v.size() - 14);
    if (v == "0.1.0-1")
        return 14;
    if (v == "0.1.1-1")
        return 15;
    if (v == "0.1.2-1")
        return 16;
    if (v == "0.1.3-1")
        return 17;
    if (v == "0.1.4-1")
        return 18;
    throw std::runtime_error("Unsupported Bacuri VMS package version");
}
const char* version_query(int schema)
{
    return schema < 16 ? control::legacy_schema_version_query : control::schema_version_query;
}
void sync_path(const fs::path& file)
{
    int fd = open(file.c_str(), O_RDONLY | O_CLOEXEC);
    ensure(fd >= 0 && fsync(fd) == 0, "Cannot sync maintenance state");
    close(fd);
}
void write_state(const Json& op)
{
    put(state / "next", op.dump());
    sync_path(state / "next");
    fs::rename(state / "next", pending);
    sync_path(state);
}
void service(const std::string& action)
{
    Args words{"/usr/bin/systemctl", action};
    words.insert(words.end(), units.begin(), units.end());
    run(words);
}
void fence(const Json& op)
{
    for (const auto& unit : units) {
        auto dir = fs::path("/etc/systemd/system") / (unit + ".d");
        fs::create_directories(dir);
        put(dir / "bacuri-maintenance.conf", "[Unit]\nConditionPathExists=!/var/lib/bacuri-maintenance/pending\n");
        const auto dropin = dir / "bacuri-maintenance.conf";
        fs::permissions(dropin, fs::perms::owner_read | fs::perms::owner_write | fs::perms::group_read |
                                    fs::perms::others_read);
        sync_path(dropin);
        sync_path(dir);
    }
    sync_path("/etc/systemd/system");
    write_state(op);
    run({"/usr/bin/systemctl", "daemon-reload"});
    service("stop");
}
void finish(const Json& op)
{
    service("enable");
    fs::remove(pending);
    sync_path(state);
    try {
        service("start");
        for (const auto& unit : units)
            run({"/usr/bin/systemctl", "is-active", "--quiet", unit});
    } catch (...) {
        fence(op);
        throw;
    }
}
void volume(const Json& info)
{
    run({"/usr/bin/findmnt", "-n", "--mountpoint", "/srv/bacuri-archive", "--types", "ext4"},
        {.output = "/dev/null"});
    storage::Sqlite db{archive / "journal.db", storage::OpenMode::read_only};
    auto row = db.query("SELECT site_id,node_id FROM volume_owner");
    ensure(sqlite3_step(row.get()) == SQLITE_ROW &&
               info.at("site") == reinterpret_cast<const char*>(sqlite3_column_text(row.get(), 0)) &&
               info.at("node") == reinterpret_cast<const char*>(sqlite3_column_text(row.get(), 1)),
           "Archive identity differs from snapshot");
}
Json identity(const std::string& version)
{
    struct stat st{};
    ensure(lstat(archive.c_str(), &st) == 0 && S_ISDIR(st.st_mode), "Missing archive directory");
    return {{"format", 1},
            {"archive_uid", st.st_uid},
            {"archive_gid", st.st_gid},
            {"site", read(config / "site-id")},
            {"node", read(config / "node-id")},
            {"version", version},
            {"schema", schema_for(version)}};
}
void space(std::uintmax_t size, const fs::path& file)
{
    ensure(fs::space(file).available > size + 16uz * 1024 * 1024,
           "Insufficient free space for database and snapshot");
}
void validate_recovery_key(const fs::path& key)
{
    struct stat st{};
    ensure(lstat(key.c_str(), &st) == 0 && S_ISREG(st.st_mode) && st.st_uid == 0 && (st.st_mode & 0077) == 0 &&
               st.st_size == 32,
           "Expected a private root-owned 32-byte recovery key");
    auto relative = fs::canonical(key).lexically_relative(config);
    ensure(!relative.empty() && *relative.begin() == "..",
           "Store the recovery key outside the service configuration");
}
void backup(const fs::path& file, const Json& info, const fs::path& key)
{
    validate_recovery_key(key);
    ensure(!fs::exists(file), "Snapshot already exists");
    volume(info);
    ensure(sql(version_query(info.at("schema"))) == std::to_string(info.at("schema").get<int>()),
           "Source database version differs from package");
    auto bytes = std::stoull(sql("SELECT pg_database_size(current_database())"));
    space(4 * bytes, "/var/tmp");
    space(2 * bytes, file.parent_path());
    Temp tmp;
    auto dump = postgres("pg_dump");
    dump.insert(dump.end(), {"--no-owner", "--no-privileges", "--dbname=bacuri_vms"});
    run(dump, {.output = tmp.dir / "database.sql"});
    run({"/usr/bin/tar", "-cf", (tmp.dir / "identity.tar").string(), "-C", "/etc", "bacuri-vms"});
    put(tmp.dir / "snapshot.json", info.dump());
    run({"/usr/bin/tar", "-cf", "snapshot.tar", "identity.tar", "database.sql", "snapshot.json"},
        {.cwd = tmp.dir});
    auto part = file.string() + ".XXXXXX";
    int fd = mkstemp(part.data());
    ensure(fd >= 0, "Cannot create snapshot");
    close(fd);
    try {
        snapshot_cipher(key, {.input = tmp.dir / "snapshot.tar", .output = part}, true);
        sync_path(part);
        ensure(link(part.c_str(), file.c_str()) == 0, "Cannot publish snapshot");
        sync_path(file.parent_path());
    } catch (...) {
        unlink(part.c_str());
        throw;
    }
    unlink(part.c_str());
}
Json open_snapshot(const fs::path& file, const fs::path& key, const Temp& tmp)
{
    space(3 * fs::file_size(file), tmp.dir);
    snapshot_cipher(key, {.input = file, .output = tmp.dir / "snapshot.tar"}, false);
    run({"/usr/bin/tar", "-xf", "snapshot.tar", "database.sql", "identity.tar", "snapshot.json"},
        {.cwd = tmp.dir});
    auto info = Json::parse(read(tmp.dir / "snapshot.json"));
    ensure(info.at("format") == 1 && info.at("schema") == schema_for(info.at("version")) &&
               control::canonical_id(info.at("site").get<std::string>()) &&
               control::canonical_id(info.at("node").get<std::string>()),
           "Invalid recovery snapshot");
    volume(info);
    return info;
}
void reconcile(const Json& info)
{
    // Peer-authenticate the database connection as core, then release root and access the volume as recorder.
    auto* core = getpwnam("bacuri-core");
    ensure(core, "Missing core account");
    auto core_uid = core->pw_uid;
    auto* recorder = getpwnam("bacuri-recorder");
    ensure(recorder, "Missing recorder account");
    auto uid = recorder->pw_uid;
    auto gid = recorder->pw_gid;
    setenv("PGHOST", "/var/run/postgresql", 1);
    setenv("PGUSER", "bacuri-core", 1);
    setenv("PGDATABASE", "bacuri_vms", 1);
    setenv("BACURI_ARCHIVE_DIR", archive.c_str(), 1);
    setenv("BACURI_SITE_ID", info.at("site").get<std::string>().c_str(), 1);
    setenv("BACURI_NODE_ID", info.at("node").get<std::string>().c_str(), 1);
    ensure(seteuid(core_uid) == 0, "Cannot select database identity");
    control::Database db;
    ensure(seteuid(0) == 0 && initgroups("bacuri-recorder", gid) == 0 && setgid(gid) == 0 && setuid(uid) == 0,
           "Cannot select archive identity");
    control::rebuild_archive(db, std::cout,
                             info.at("schema") < 16 ? control::StorageLayout::legacy
                                                    : control::StorageLayout::module_schemas);
}
void restore_files(const Temp& tmp, const Json& info)
{
    Temp stage("/etc");
    run({"/usr/bin/tar", "-xf", (tmp.dir / "identity.tar").string(), "--same-owner", "-C", stage.dir.string()});
    const auto saved = stage.dir / "bacuri-vms";
    ensure(info.at("site") == read(saved / "site-id") && info.at("node") == read(saved / "node-id"),
           "Snapshot identity differs from manifest");
    if (fs::exists(config)) {
        ensure(renameat2(AT_FDCWD, saved.c_str(), AT_FDCWD, config.c_str(), RENAME_EXCHANGE) == 0,
               "Cannot replace service identity");
    } else
        fs::rename(saved, config);
    sync_path("/etc");
    const auto* recorder = getpwnam("bacuri-recorder");
    ensure(recorder, "Missing recorder account");
    const auto uid = recorder->pw_uid;
    const auto* group = getgrnam("bacuri-archive");
    ensure(group, "Missing archive group");
    const auto gid = group->gr_gid;
    if (uid != info.at("archive_uid").get<uid_t>() || gid != info.at("archive_gid").get<gid_t>()) {
        struct stat root{};
        ensure(lstat(archive.c_str(), &root) == 0, "Missing archive");
        auto ownership = [&](const fs::path& p) {
            struct stat st{};
            ensure(lstat(p.c_str(), &st) == 0 && st.st_dev == root.st_dev &&
                       (S_ISREG(st.st_mode) || S_ISDIR(st.st_mode)),
                   "Unexpected archive entry");
            ensure(lchown(p.c_str(), st.st_uid == info.at("archive_uid").get<uid_t>() ? uid : st.st_uid,
                          st.st_gid == info.at("archive_gid").get<gid_t>() ? gid : st.st_gid) == 0,
                   "Cannot restore archive ownership");
        };
        for (const auto& entry : fs::recursive_directory_iterator(archive))
            ownership(entry.path());
        ownership(archive);
    }
}
void load_snapshot(const Temp& tmp, const Json& info)
{
    run({"/usr/bin/systemctl", "start", "postgresql.service"});
    auto bootstrap = psql("postgres");
    bootstrap.insert(bootstrap.end(), {"-f", "/usr/share/bacuri-vms/bootstrap.sql"});
    run(bootstrap);
    auto args = psql("bacuri_vms");
    using namespace bacuri::vms::control;
    std::string reset = info.at("schema") < 16 ? access::legacy_restore_search_path : access::restore_search_path;
    reset += access::reset_sessions;
    reset += inventory::reset_recorder;
    if (info.at("schema") >= 15)
        reset += access::audit_restore;
    args.insert(args.end(), {"--single-transaction", "-c",
                             "DROP SCHEMA IF EXISTS "
                             "bacuri_events,bacuri_inventory,bacuri_archive,bacuri_access,bacuri_maps,bacuri_"
                             "media_sessions,bacuri_meta CASCADE; SET ROLE \"bacuri-core\";",
                             "-f", "-", "-c", reset});
    run(args, {.input = tmp.dir / "database.sql"});
    restore_files(tmp, info);
    ensure(info.at("site") == read(config / "site-id") && info.at("node") == read(config / "node-id"),
           "Restored identity differs from manifest");
    for (const auto* suffix : {"", "-wal", "-shm", "-journal"})
        fs::remove(fs::path("/var/lib/bacuri-recorder") / ("configuration.db" + std::string(suffix)));
    run({"/proc/self/exe", "reconcile"});
}
void operate(const std::string& action, char** v)
{
    auto file = fs::absolute(v[4]).lexically_normal(), key = fs::absolute(v[5]);
    Bundle bundle({.keyring = v[2], .directory = v[3]});
    int target = schema_for(bundle.version);
    Json op{{"action", action}, {"version", bundle.version}, {"file", file.string()}};
    bool resumed = fs::exists(pending);
    auto previous = resumed ? Json::parse(read(pending)) : Json(nullptr);
    if (resumed)
        ensure(previous == op || (action == "rollback" && previous.at("action") == "upgrade" &&
                                  previous.at("file") == file.string()),
               "Another maintenance operation is pending");
    ensure(fs::is_directory(file.parent_path()), "Snapshot directory does not exist");
    validate_recovery_key(key);
    if (action == "upgrade") {
        ensure(target == 18, "Unsupported upgrade target");
        if (!resumed || !fs::exists(file)) {
            const auto current = installed_package().version;
            const int source = schema_for(current);
            ensure(source < target && sql(version_query(source)) == std::to_string(source) && !fs::exists(file),
                   "Expected installed predecessor and unused backup path");
            auto info = identity(current);
            volume(info);
            const auto bytes = std::stoull(sql("SELECT pg_database_size(current_database())"));
            space(4 * bytes, "/var/tmp");
            space(2 * bytes, file.parent_path());
            fence(op);
            backup(file, info, key);
        } else {
            Temp tmp;
            auto info = open_snapshot(file, key, tmp);
            ensure(identity(info.at("version")) == info && info.at("schema") < target, "Wrong upgrade checkpoint");
            fence(op);
        }
        bundle.install();
        run({bundle.package == "bacuri-vms" ? "/usr/sbin/bacuri-vms" : "/usr/sbin/bacuri-basic", "admin",
             "migrate"});
        ensure(sql(version_query(target)) == std::to_string(target), "Migration did not complete");
    } else {
        Temp tmp;
        auto info = open_snapshot(file, key, tmp);
        ensure(info.at("version") == bundle.version, "Snapshot requires the matching signed package");
        if (fs::exists(config / "site-id")) {
            ensure(info.at("site") == read(config / "site-id") && info.at("node") == read(config / "node-id") &&
                       (action == "rollback" || resumed),
                   "Restore requires a clean installation or the same site and node");
            ensure(resumed || schema_for(installed_package().version) >= target, "Unsupported rollback source");
        } else {
            ensure(action == "restore", "Rollback requires an existing installation");
            run({"/usr/bin/systemctl", "start", "postgresql.service"});
            auto list = psql("postgres");
            list.insert(list.end(), {"-c", "SELECT count(*) FROM pg_database WHERE datname='bacuri_vms'"});
            if (!resumed && capture(list) != "0")
                ensure(sql("SELECT count(*) FROM pg_namespace WHERE nspname IN "
                           "('bacuri_events','bacuri_inventory','bacuri_archive','bacuri_access','bacuri_maps','"
                           "bacuri_media_sessions','bacuri_meta')") == "0",
                       "Restore requires an empty application database");
        }
        fence(op);
        if (target == 14 && fs::exists("/usr/lib/systemd/system/bacuri-metrics.socket")) {
            run({"/usr/bin/systemctl", "disable", "--now", "bacuri-metrics.socket", "bacuri-diagnostics.timer"});
            run({"/usr/bin/systemctl", "stop", "bacuri-diagnostics.service"});
        }
        bundle.install(action == "rollback");
        load_snapshot(tmp, info);
    }
    if (target >= 15)
        run({"/usr/bin/systemctl", "enable", "--now", "bacuri-diagnostics.timer", "bacuri-metrics.socket"});
    finish(op);
}
} // namespace
int command(int argc, char** v)
{
    std::string action = argc > 1 ? v[1] : "";
    const fs::path metrics = "/run/bacuri-vms/metrics.prom";
    if (action == "scrape" && argc == 2) {
        char line[1024]{};
        std::cin.getline(line, sizeof(line));
        std::string_view request{line};
        bool found = (request == "GET /metrics HTTP/1.1\r" || request == "GET /metrics HTTP/1.0\r");
        std::size_t total = 0;
        while (std::cin.getline(line, sizeof(line)) && std::string_view(line) != "\r") {
            total += std::cin.gcount();
            if (total > 8192)
                return 1;
        }
        bool ready = fs::is_regular_file(metrics) &&
                     fs::file_time_type::clock::now() - fs::last_write_time(metrics) < std::chrono::minutes(1);
        auto contents = found && ready ? read(metrics) : std::string{};
        std::cout << "HTTP/1.1 "
                  << (!found  ? "404 Not Found"
                      : ready ? "200 OK"
                              : "503 Service Unavailable")
                  << "\r\nContent-Type: text/plain; version=0.0.4\r\nConnection: close\r\nContent-Length: "
                  << contents.size() << "\r\n\r\n"
                  << contents;
        return 0;
    }
    ensure(geteuid() == 0 && sodium_init() >= 0, "Local root privileges required");
    umask(0077);
    if (action == "sample" && argc == 2) {
        Temp tmp;
        diagnose(tmp.dir / "sample");
        auto dest = metrics.parent_path() / "metrics.next";
        fs::copy_file(tmp.dir / "sample/metrics.prom", dest, fs::copy_options::overwrite_existing);
        fs::permissions(dest, fs::perms::owner_read | fs::perms::owner_write | fs::perms::group_read |
                                  fs::perms::others_read);
        fs::rename(dest, metrics);
        return 0;
    }
    if (action == "diagnose" && argc == 3) {
        diagnose(v[2]);
        return 0;
    }
    if (action == "reconcile" && argc == 2) {
        ensure(fs::exists(pending), "Reconciliation requires stopped services");
        reconcile(identity(installed_package().version));
        return 0;
    }
    int lock = open("/run/lock/bacuri-maintenance", O_RDWR | O_CREAT | O_NOFOLLOW | O_CLOEXEC, 0600);
    ensure(lock >= 0 && flock(lock, LOCK_EX | LOCK_NB) == 0, "Another maintenance command is active");
    fs::create_directories(state);
    if (action == "backup" && argc == 4) {
        ensure(!fs::exists(pending), "Maintenance pending");
        backup(fs::absolute(v[2]), identity(installed_package().version), v[3]);
    } else if ((action == "upgrade" || action == "rollback" || action == "restore") && argc == 6)
        operate(action, v);
    else
        throw std::invalid_argument("Usage: bacuri-maintain backup SNAPSHOT KEY | upgrade|rollback|restore "
                                    "TRUSTED_KEYRING KIT SNAPSHOT KEY | diagnose NEW_DIRECTORY");
    return 0;
}
} // namespace bacuri::vms::maintenance
int main(int argc, char** argv)
try {
    return bacuri::vms::maintenance::command(argc, argv);
} catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
}
