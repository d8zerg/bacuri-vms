#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

namespace
{
namespace fs = std::filesystem;
void run(std::vector<std::string> args)
{
    std::vector<char*> argv;
    for (auto& s : args)
        argv.push_back(s.data());
    argv.push_back(nullptr);
    auto pid = fork();
    if (pid == 0) {
        execv(argv[0], argv.data());
        _exit(127);
    }
    int s{};
    if (pid < 0 || waitpid(pid, &s, 0) != pid || !WIFEXITED(s) || WEXITSTATUS(s))
        throw std::runtime_error("Command failed: " + args.front());
}
struct Staging {
    fs::path dir;
    Staging()
    {
        char name[] = "/var/tmp/bacuri-install-XXXXXX";
        if (!mkdtemp(name))
            throw std::runtime_error("Cannot stage package");
        dir = name;
    }
    ~Staging()
    {
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
};
} // namespace
int main(int argc, char** argv)
try {
    if (argc != 3 || geteuid() != 0)
        throw std::invalid_argument("Usage as root: bacuri-install TRUSTED_KEYRING BUNDLE_DIRECTORY");
    const auto trusted = fs::canonical(argv[1]), input = fs::canonical(argv[2]);
    Staging stage;
    fs::copy_file(trusted, stage.dir / "trust.gpg");
    fs::create_directory(stage.dir / "kit");
    // Stage regular files before verification so package installation uses the verified bytes.
    for (const auto& p : fs::recursive_directory_iterator(input)) {
        const auto target = stage.dir / "kit" / p.path().lexically_relative(input);
        if (fs::is_directory(p.symlink_status()))
            fs::create_directory(target);
        else if (fs::is_regular_file(p.symlink_status()))
            fs::copy_file(p.path(), target);
        else
            throw std::runtime_error("Unexpected entry in installation bundle");
    }
    fs::current_path(stage.dir / "kit");
    run({"/usr/bin/gpgv", "--keyring", (stage.dir / "trust.gpg").string(), "SHA256SUMS.sig", "SHA256SUMS"});
    run({"/usr/bin/sha256sum", "--strict", "--check", "--quiet", "SHA256SUMS"});
    // Only the independently trusted repository may participate in dependency resolution.
    fs::create_directory(stage.dir / "lists");
    fs::create_directory(stage.dir / "archives");
    auto sources = stage.dir / "local.list";
    std::ofstream(sources) << "deb [signed-by=" << (stage.dir / "trust.gpg").string()
                           << "] file:" << (stage.dir / "kit/repository").string() << " ./\n";
    const auto public_read =
        fs::perms::owner_read | fs::perms::owner_write | fs::perms::group_read | fs::perms::others_read;
    const auto traverse = fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec;
    for (const auto& entry : fs::recursive_directory_iterator(stage.dir)) {
        fs::permissions(entry.path(), entry.is_directory() ? public_read | traverse : public_read);
    }
    fs::permissions(stage.dir, fs::perms::owner_all | fs::perms::group_exec | fs::perms::others_exec);
    std::vector<std::string> apt{"/usr/bin/apt-get",
                                 "-o",
                                 "Dir::Etc::sourcelist=" + sources.string(),
                                 "-o",
                                 "Dir::Etc::sourceparts=-",
                                 "-o",
                                 "Dir::State::lists=" + (stage.dir / "lists").string(),
                                 "-o",
                                 "Dir::Cache::archives=" + (stage.dir / "archives").string(),
                                 "-o",
                                 "Acquire::Languages=none",
                                 "-o",
                                 "APT::Update::Error-Mode=any"};
    auto update = apt;
    update.push_back("update");
    run(update);
    std::ifstream version{"VERSION"};
    std::string release;
    version >> release;
    if (release.empty())
        throw std::runtime_error("Missing package version");
    apt.insert(apt.end(),
               {"--no-install-recommends", "--no-remove", "-y", "install", "bacuri-vms-basic=" + release});
    setenv("DEBIAN_FRONTEND", "noninteractive", 1);
    run(apt);
    return 0;
} catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
}
