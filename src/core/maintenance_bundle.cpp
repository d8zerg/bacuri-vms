#include "maintenance.hpp"
#include <algorithm>
#include <array>
#include <sstream>
#include <unistd.h>
namespace bacuri::vms::maintenance
{
namespace
{
constexpr std::array package_names{"bacuri-vms", "bacuri-vms-basic"};
}
Package installed_package()
{
    std::istringstream rows{capture(
        {"/usr/bin/dpkg-query", "-W", "-f=${Package} ${db:Status-Status} ${Version}\n", "bacuri-vms*"}, true)};
    Package installed;
    std::string line;
    while (std::getline(rows, line)) {
        std::istringstream fields{line};
        Package row;
        std::string status;
        if (fields >> row.name >> status >> row.version && std::ranges::contains(package_names, row.name) &&
            status != "config-files" && status != "not-installed") {
            ensure(installed.name.empty(), "Multiple Bacuri VMS packages installed");
            installed = row;
        }
    }
    return installed;
}
Bundle::Bundle(const SignedKit& source)
{
    auto input = fs::canonical(source.directory), kit = stage.dir / "kit";
    fs::copy_file(source.keyring, stage.dir / "trust.gpg");
    fs::create_directory(kit);
    for (const auto& p : fs::recursive_directory_iterator(input)) {
        const auto target = kit / p.path().lexically_relative(input);
        if (fs::is_directory(p.symlink_status()))
            fs::create_directory(target);
        else if (fs::is_regular_file(p.symlink_status()))
            fs::copy_file(p.path(), target);
        else
            throw std::runtime_error("Unexpected entry in installation bundle");
    }
    run({"/usr/bin/gpgv", "--keyring", (stage.dir / "trust.gpg").string(), "SHA256SUMS.sig", "SHA256SUMS"},
        {.cwd = kit});
    run({"/usr/bin/sha256sum", "--strict", "--check", "--quiet", "SHA256SUMS"}, {.cwd = kit});
    std::istringstream stream{read(kit / "VERSION")};
    stream >> version;
    ensure(!version.empty(), "Missing package version");
    for (const auto* name : package_names) {
        const auto filename = std::string{name} + "_" + version + "_amd64.deb";
        if (fs::is_regular_file(kit / "repository" / filename)) {
            ensure(package.empty(), "Multiple product packages in signed kit");
            package = name;
        }
    }
    ensure(!package.empty(), "Missing product package in signed kit");
}
void Bundle::install(bool downgrade) const
{
    const auto& dir = stage.dir;
    fs::create_directory(dir / "lists");
    fs::create_directory(dir / "archives");
    auto sources = dir / "local.list";
    put(sources, "deb [signed-by=" + (dir / "trust.gpg").string() + "] file:" + (dir / "kit/repository").string() +
                     " ./\n");
    const auto readable =
        fs::perms::owner_write | fs::perms::owner_read | fs::perms::group_read | fs::perms::others_read;
    const auto traverse = fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec;
    for (const auto& entry : fs::recursive_directory_iterator(dir))
        fs::permissions(entry.path(), entry.is_directory() ? readable | traverse : readable);
    fs::permissions(dir, fs::perms::owner_all | traverse);
    Args apt{"/usr/bin/apt-get",
             "-o",
             "Dir::Etc::sourcelist=" + sources.string(),
             "-o",
             "Dir::Etc::sourceparts=-",
             "-o",
             "Dir::State::lists=" + (dir / "lists").string(),
             "-o",
             "Dir::Cache::archives=" + (dir / "archives").string(),
             "-o",
             "Acquire::Languages=none",
             "-o",
             "APT::Update::Error-Mode=any"};
    auto update = apt;
    update.push_back("update");
    run(update);
    if (downgrade)
        apt.push_back("--allow-downgrades");
    const auto current = installed_package();
    if (current.name.empty() || current.name == package) {
        apt.push_back("--no-remove");
    } else {
        // During a rename APT may remove only the previous package in the same transaction.
        apt.insert(apt.end(), {"-o", "DPkg::Tools::Options::/usr/bin/awk::Version=2", "-o",
                               "DPkg::Pre-Install-Pkgs::=/usr/bin/awk 'NR == 1 && $0 != \"VERSION 2\" {exit 1} "
                               "$5 == \"**REMOVE**\" && $1 != \"" +
                                   current.name + "\" {exit 1}'"});
    }
    apt.insert(apt.end(), {"--no-install-recommends", "-y", "install", package + "=" + version});
    setenv("DEBIAN_FRONTEND", "noninteractive", 1);
    run(apt);
}
} // namespace bacuri::vms::maintenance
