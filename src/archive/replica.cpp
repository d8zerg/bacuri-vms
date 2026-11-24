#include "archive/replica.hpp"
#include "archive/mp4.hpp"
#include "control/support.hpp"
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstdlib>
#include <fstream>
#include <system_error>

namespace bacuri::vms::archive
{
namespace fs = std::filesystem;
namespace
{
void checked(int rc)
{
    if (rc < 0)
        throw std::system_error(errno, std::generic_category(), "Archive copy I/O");
}
struct Handle {
    int fd;
    Handle(const fs::path& path, int flags) : fd(open(path.c_str(), flags | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK))
    {
        checked(fd);
    }
    ~Handle()
    {
        close(fd);
    }
};
void sync(const fs::path& path, int flags)
{
    Handle file{path, flags};
    checked(fsync(file.fd));
}
void verify_file(const fs::path& path, const Json& spec)
{
    Handle file{path, O_RDONLY};
    struct stat info{};
    checked(fstat(file.fd, &info));
    if (!S_ISREG(info.st_mode) || info.st_size != spec.at("size_bytes").get<off_t>() ||
        spec.at("sha256") != digest_media(file.fd).sha256)
        throw std::runtime_error("Archive copy checksum mismatch");
}
} // namespace
std::optional<fs::path> replica_blocks(const fs::path& primary, const Owner& owner)
{
    const char* setting = std::getenv("BACURI_ARCHIVE_REPLICA_DIR");
    if (!setting || !*setting)
        return {};
    fs::path mount{setting};
    if (!control::canonical_id(owner.site) || !control::canonical_id(owner.node) || !mount.is_absolute() ||
        !fs::is_directory(fs::symlink_status(mount)) || mount == mount.root_path())
        throw std::runtime_error("Archive copy volume unavailable");
    auto a = fs::canonical(primary), b = fs::canonical(mount);
    const auto related = [](const auto& a, const auto& b) {
        auto r = a.lexically_relative(b);
        return !r.empty() && *r.begin() != "..";
    };
    if (related(a, b) || related(b, a))
        throw std::runtime_error("Archive volumes overlap");
    return b / ("bacuri-" + owner.site + "-" + owner.node);
}
bool verified_copy(const fs::path& dir, const Json& expected)
{
    try {
        if (!fs::is_directory(fs::symlink_status(dir)))
            return false;
        const auto path = dir / "manifest.json";
        if (!fs::is_regular_file(fs::symlink_status(path)) || fs::file_size(path) > manifest_limit)
            return false;
        std::ifstream input{path};
        if (Json::parse(input) != expected)
            return false;
        verify_file(dir / "media.mp4", expected.at("media"));
        if (expected.contains("audio"))
            verify_file(dir / "audio.pcm", expected.at("audio"));
        return true;
    } catch (const std::exception&) {
        return false;
    }
}
void remove_copy(const fs::path& dir)
{
    if (!fs::exists(fs::symlink_status(dir)))
        return;
    Handle parent{dir.parent_path(), O_RDONLY | O_DIRECTORY};
    Handle block{dir, O_RDONLY | O_DIRECTORY};
    for (const auto name : {"audio.pcm", "media.mp4", "manifest.json"})
        if (unlinkat(block.fd, name, 0) && errno != ENOENT)
            checked(-1);
    checked(unlinkat(parent.fd, dir.filename().c_str(), AT_REMOVEDIR));
    checked(fsync(parent.fd));
}
void restore_copy(const fs::path& from, const Json& expected, const fs::path& target)
{
    if (!verified_copy(from, expected))
        throw std::runtime_error("No verified archive copy");
    auto parent = target.parent_path();
    auto temp = parent / (".replicating-" + expected.at("block_id").get<std::string>());
    remove_copy(temp);
    if (!fs::create_directory(temp))
        throw std::runtime_error("Archive copy staging unavailable");
    try {
        for (const auto name : {"media.mp4", "manifest.json", "audio.pcm"}) {
            if (std::string_view{name} == "audio.pcm" && !expected.contains("audio"))
                continue;
            fs::copy_file(from / name, temp / name);
            sync(temp / name, O_RDONLY);
        }
        if (!verified_copy(temp, expected))
            throw std::runtime_error("Invalid archive copy");
        sync(temp, O_RDONLY | O_DIRECTORY);
        if (renameat2(AT_FDCWD, temp.c_str(), AT_FDCWD, target.c_str(), RENAME_NOREPLACE) < 0) {
            if (errno != EEXIST)
                checked(-1);
            checked(renameat2(AT_FDCWD, temp.c_str(), AT_FDCWD, target.c_str(), RENAME_EXCHANGE));
        }
        sync(parent, O_RDONLY | O_DIRECTORY);
        remove_copy(temp);
    } catch (...) {
        remove_copy(temp);
        throw;
    }
}
} // namespace bacuri::vms::archive
