#include "maintenance.hpp"
#include <charconv>
#include <sstream>
#include <unistd.h>
namespace bacuri::vms::maintenance
{
namespace
{
int package_schema(const std::string& field)
{
    int schema{};
    const auto end = field.data() + field.size();
    const auto result = std::from_chars(field.data(), end, schema);
    ensure(result.ec == std::errc{} && result.ptr == end && schema > 0,
           "Missing or invalid Bacuri-Schema in package");
    return schema;
}
} // namespace
Package installed_package()
{
    std::istringstream fields{capture(
        {"/usr/bin/dpkg-query", "-W", "-f=${db:Status-Status}\n${Version}\n${Bacuri-Schema}", "bacuri-vms"},
        true)};
    std::string status, version, schema;
    if (!std::getline(fields, status) || status == "config-files" || status == "not-installed")
        return {};
    ensure(std::getline(fields, version) && !version.empty() && std::getline(fields, schema),
           "Incomplete metadata in installed Bacuri VMS package");
    return {version, package_schema(schema)};
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
    std::string target;
    ensure(static_cast<bool>(stream >> version >> target),
           "Missing package version or target in installation bundle");
    const auto host =
        capture({"/bin/sh", "-ec",
                 R"(. /etc/os-release; printf '%s:%s:' "$ID" "$VERSION_ID"; dpkg --print-architecture)"});
    ensure(target == host, "Installation bundle targets a different Debian release or architecture");
    const auto product = kit / "repository" / ("bacuri-vms_" + version + "_amd64.deb");
    ensure(fs::is_regular_file(product), "Missing product package in signed kit");
    schema = package_schema(capture({"/usr/bin/dpkg-deb", "-f", product.string(), "Bacuri-Schema"}));
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
    update.emplace_back("update");
    run(update);
    if (downgrade)
        apt.emplace_back("--allow-downgrades");
    apt.insert(apt.end(), {"--no-remove", "--no-install-recommends", "-y", "install", "bacuri-vms=" + version});
    setenv("DEBIAN_FRONTEND", "noninteractive", 1);
    run(apt);
}
} // namespace bacuri::vms::maintenance
