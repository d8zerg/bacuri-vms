#pragma once
#include <filesystem>
#include <string>
#include <vector>
namespace bacuri::vms::maintenance
{
namespace fs = std::filesystem;
using Args = std::vector<std::string>;
struct Temp {
    fs::path dir;
    explicit Temp(const fs::path& parent = "/var/tmp");
    Temp(const Temp&) = delete;
    ~Temp();
};
struct Io {
    fs::path input, output, cwd;
    bool allow_failure{};
};
int run(Args args, const Io& io = {});
std::string capture(Args args, bool optional = false);
void ensure(bool ok, const char* message);
std::string read(const fs::path& file);
void put(const fs::path& file, const std::string& value);
struct Package {
    std::string version;
    int schema{};
};
Package installed_package();
struct SignedKit {
    fs::path keyring, directory;
};
struct Bundle {
    Temp stage;
    std::string version;
    int schema{};
    explicit Bundle(const SignedKit& source);
    void install(bool downgrade = false) const;
};
void snapshot_cipher(const fs::path& key, const Io& io, bool encrypt);
void diagnose(const fs::path& destination);
} // namespace bacuri::vms::maintenance
