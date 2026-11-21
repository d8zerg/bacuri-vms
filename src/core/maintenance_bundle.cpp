#include "maintenance.hpp"
#include <sstream>
#include <unistd.h>
namespace bacuri::vms::maintenance
{
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
    apt.insert(apt.end(),
               {"--no-install-recommends", "--no-remove", "-y", "install", "bacuri-vms-basic=" + version});
    setenv("DEBIAN_FRONTEND", "noninteractive", 1);
    run(apt);
}
} // namespace bacuri::vms::maintenance
