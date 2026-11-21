#include "control/support.hpp"
#include <openssl/pem.h>
#include <openssl/x509v3.h>
#include <sodium.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <map>
#include <grp.h>
#include <pwd.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>

#include <sys/wait.h>
#include <unistd.h>

namespace
{
namespace fs = std::filesystem;
using Env = std::map<std::string, std::string>;
const fs::path config = "/etc/bacuri-vms";
const std::string core = "/usr/lib/bacuri-vms/bacuri-vms-core";
void check(bool ok, const char* message)
{
    if (!ok)
        throw std::runtime_error(message);
}
void execute(std::vector<std::string> args)
{
    std::vector<char*> words;
    for (auto& a : args)
        words.push_back(a.data());
    words.push_back(nullptr);
    execv(words[0], words.data());
    _exit(127);
}
void run(std::vector<std::string> args)
{
    const auto pid = fork();
    check(pid >= 0, "Cannot start command");
    if (!pid)
        execute(std::move(args));
    int status{};
    check(waitpid(pid, &status, 0) == pid && WIFEXITED(status) && !WEXITSTATUS(status),
          "Installation command failed");
}
gid_t group(const std::string& name)
{
    auto g = getgrnam(name.c_str());
    check(g, "Missing system group");
    return g->gr_gid;
}
void directory(const fs::path& path, const std::string& user, gid_t gid, mode_t mode)
{
    fs::create_directory(path);
    auto u = getpwnam(user.c_str());
    check(u, "Missing system user");
    const auto uid = u->pw_uid;
    int fd = open(path.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    check(fd >= 0 && fchown(fd, uid, gid) == 0 && fchmod(fd, mode) == 0, "Cannot secure service directory");
    close(fd);
}
std::string read_file(const fs::path& path)
{
    std::ifstream f(path, std::ios::binary);
    check(f.is_open(), "Cannot read installation input");
    return {std::istreambuf_iterator<char>(f), {}};
}
void persist(const fs::path& path, const std::string& data, gid_t gid)
{
    if (fs::exists(path)) {
        check(read_file(path) == data, "Installed configuration differs from requested configuration");
        return;
    }
    auto tmp = path.string() + ".XXXXXX";
    int fd = mkstemp(tmp.data());
    check(fd >= 0, "Cannot create configuration file");
    try {
        check(fchown(fd, 0, gid) == 0 && fchmod(fd, 0640) == 0, "Cannot secure configuration file");
        for (std::size_t pos = 0; pos < data.size();) {
            auto n = write(fd, data.data() + pos, data.size() - pos);
            check(n > 0, "Cannot write configuration");
            pos += n;
        }
        check(fsync(fd) == 0 && link(tmp.c_str(), path.c_str()) == 0, "Cannot publish configuration");
    } catch (...) {
        close(fd);
        unlink(tmp.c_str());
        throw;
    }
    close(fd);
    unlink(tmp.c_str());
}
std::string identifier(const std::string& name)
{
    auto path = config / name;
    if (!fs::exists(path)) {
        std::unique_ptr<char, decltype(&g_free)> id{g_uuid_string_random(), g_free};
        persist(path, id.get(), group("root"));
    }
    auto id = read_file(path);
    check(bacuri::vms::control::canonical_id(id), "Invalid installation identity");
    return id;
}
void envfile(const std::string& role, const Env& env)
{
    std::string text;
    for (const auto& [k, v] : env) {
        check(v.find_first_of("\r\n\"'\\ \t") == v.npos, "Invalid service setting");
        text += k + "=" + v + "\n";
    }
    persist(config / (role + ".env"), text, group("bacuri-" + role));
}
void admin(std::vector<std::string> args)
{
    std::ifstream f(config / "core.env");
    check(f.is_open(), "Run setup before administrative commands");
    for (std::string line; std::getline(f, line);) {
        auto i = line.find('=');
        check(i != line.npos, "Invalid service environment");
        check(setenv(line.substr(0, i).c_str(), line.substr(i + 1).c_str(), 1) == 0,
              "Cannot set service environment");
    }
    auto u = getpwnam("bacuri-core");
    check(u, "Missing service identity");
    auto uid = u->pw_uid;
    auto gid = u->pw_gid;
    check(initgroups("bacuri-core", gid) == 0 && setgid(gid) == 0 && setuid(uid) == 0,
          "Cannot select service identity");
    fs::current_path("/usr/share/bacuri-vms");
    args.insert(args.begin(), core);
    execute(std::move(args));
}
void setup(char** v)
{
    const std::string host = v[2], user = v[3], net = v[6];
    check(host.size() <= 253 &&
              std::regex_match(
                  host, std::regex("([a-z0-9]([a-z0-9-]{0,61}[a-z0-9])?\\.)+[a-z]([a-z0-9-]{0,61}[a-z0-9])?")),
          "Use a DNS name for WebAuthn");
    bacuri::vms::control::CameraNetworks::LegacyRtsp legacy{};
    bacuri::vms::control::CameraNetworks networks(net, legacy);
    run({"/usr/bin/findmnt", "--noheadings", "--mountpoint", "/srv/bacuri-archive", "--types", "ext4", "--output",
         "FSTYPE"});
    struct stat volume{}, root{};
    check(stat("/srv/bacuri-archive", &volume) == 0 && stat("/", &root) == 0 && volume.st_dev != root.st_dev,
          "Expected a separate archive filesystem");
    const auto close_file = [](FILE* f) { std::fclose(f); };
    std::unique_ptr<FILE, decltype(close_file)> cf(fopen(v[4], "r"), close_file), kf(fopen(v[5], "r"), close_file);
    check(cf && kf, "Cannot read TLS inputs");
    std::unique_ptr<X509, decltype(&X509_free)> cert(PEM_read_X509(cf.get(), nullptr, nullptr, nullptr),
                                                     X509_free);
    std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> priv(
        PEM_read_PrivateKey(kf.get(), nullptr, nullptr, nullptr), EVP_PKEY_free);
    check(cert && priv &&
              X509_check_host(cert.get(), host.data(), host.size(), X509_CHECK_FLAG_NEVER_CHECK_SUBJECT,
                              nullptr) == 1 &&
              X509_check_private_key(cert.get(), priv.get()) == 1 &&
              X509_cmp_current_time(X509_get0_notBefore(cert.get())) < 0 &&
              X509_cmp_current_time(X509_get0_notAfter(cert.get())) > 0,
          "Invalid TLS certificate, DNS name or private key");
    directory(config, "root", group("root"), 0755);
    int lock = open((config / "setup.lock").c_str(), O_CREAT | O_NOFOLLOW | O_CLOEXEC | O_RDWR, 0600);
    check(lock >= 0 && flock(lock, LOCK_EX | LOCK_NB) == 0, "Installation busy");
    for (const std::string role : {"core", "recorder"}) {
        auto dir = config / role;
        directory(dir, "root", group("bacuri-" + role), 0750);
        for (const std::string key : {"configuration.key", "export.key"}) {
            if (key == "export.key" && role != "core")
                continue;
            if (!fs::exists(dir / key))
                persist(dir / key, bacuri::vms::control::token() + "\n", group("bacuri-" + role));
        }
    }
    persist(config / "core/tls.crt", read_file(v[4]), group("bacuri-core"));
    persist(config / "core/tls.key", read_file(v[5]), group("bacuri-core"));
    directory("/srv/bacuri-archive/recorder", "bacuri-recorder", group("bacuri-archive"), 02750);
    directory("/srv/bacuri-archive/recorder/blocks", "bacuri-recorder", group("bacuri-archive"), 02750);
    const std::string base = "/run/bacuri-vms/";
    Env common{{"BACURI_SITE_ID", identifier("site-id")},
               {"BACURI_NODE_ID", identifier("node-id")},
               {"BACURI_CAMERA_NETWORKS", net},
               {"BACURI_SOCKET", base + "recorder/control.sock"},
               {"BACURI_CATALOG_SOCKET", base + "core/catalog.sock"}};
    Env media{{"BACURI_ARCHIVE_DIR", "/srv/bacuri-archive/recorder"},
              {"BACURI_PREVIEW_SOCKET", base + "recorder/preview.sock"},
              {"BACURI_AUTH_SOCKET", base + "core/auth.sock"},
              {"BACURI_MEDIA_SOCKET", base + "media/media.sock"}};
    envfile("media", media);
    Env recorder = common;
    recorder.insert(*media.find("BACURI_ARCHIVE_DIR"));
    recorder.insert(*media.find("BACURI_PREVIEW_SOCKET"));
    recorder["BACURI_KEY_FILE"] = (config / "recorder/configuration.key").string();
    recorder["BACURI_RECORDER_DB"] = "/var/lib/bacuri-recorder/configuration.db";
    envfile("recorder", recorder);
    Env server = common;
    server.insert(*media.find("BACURI_AUTH_SOCKET"));
    server.insert(*media.find("BACURI_MEDIA_SOCKET"));
    server.insert({{"PGHOST", "/var/run/postgresql"},
                   {"PGDATABASE", "bacuri_vms"},
                   {"PGUSER", "bacuri-core"},
                   {"BACURI_OBSERVATION_SOURCE", identifier("observation-id")},
                   {"BACURI_OBSERVATION_SOCKET", base + "core/observations.sock"},
                   {"BACURI_LISTEN", "0.0.0.0"},
                   {"BACURI_PORT", "8443"},
                   {"BACURI_ORIGIN", "https://" + host + ":8443"},
                   {"BACURI_TLS_CERT", (config / "core/tls.crt").string()},
                   {"BACURI_TLS_KEY", (config / "core/tls.key").string()},
                   {"BACURI_KEY_FILE", (config / "core/configuration.key").string()},
                   {"BACURI_EXPORT_KEY_FILE", (config / "core/export.key").string()}});
    envfile("core", server);
    run({"/usr/bin/systemctl", "start", "postgresql.service"});
    run({"/usr/sbin/runuser", "-u", "postgres", "--", "/usr/bin/psql", "-X", "-v", "ON_ERROR_STOP=1", "-d",
         "postgres", "-f", "/usr/share/bacuri-vms/bootstrap.sql"});
    run({"/usr/sbin/bacuri-basic", "admin", "migrate"});
    run({"/usr/sbin/bacuri-basic", "admin", "initial-admin", user});
    run({"/usr/bin/systemctl", "enable", "--now", "bacuri-core.service", "bacuri-recorder.service",
         "bacuri-media.service", "bacuri-diagnostics.timer", "bacuri-metrics.socket"});
}
} // namespace
int main(int argc, char** argv)
try {
    check(geteuid() == 0 && sodium_init() >= 0, "Local root privileges required");
    if (argc == 7 && std::string_view(argv[1]) == "setup")
        setup(argv);
    else if (argc >= 3 && std::string_view(argv[1]) == "admin")
        admin({argv + 2, argv + argc});
    else
        throw std::invalid_argument("Usage: bacuri-basic setup DNS_NAME ADMIN CERT_CHAIN PRIVATE_KEY CAMERA_CIDRS "
                                    "| admin COMMAND [ARG...]");
    return 0;
} catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
}
