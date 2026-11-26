#include "archive/volume_io.hpp"
#include <sys/stat.h>
#include <chrono>

namespace bacuri::vms::archive
{
void sync_path(const fs::path& path, int flags)
{
    File file{path, flags};
    system_check(fsync(file.fd));
}

static void unlink_if_present(int directory, const char* name, int flags = 0)
{
    if (unlinkat(directory, name, flags) < 0 && errno != ENOENT)
        system_check(-1);
}

void finish_removal(int blocks, const std::string& name)
{
    struct stat info{};
    if (fstatat(blocks, name.c_str(), &info, AT_SYMLINK_NOFOLLOW) < 0) {
        if (errno == ENOENT)
            return;
        system_check(-1);
    }
    if (!S_ISDIR(info.st_mode)) {
        unlink_if_present(blocks, name.c_str());
        return;
    }
    File dir{openat(blocks, name.c_str(), O_CLOEXEC | O_NOFOLLOW | O_DIRECTORY | O_RDONLY)};
    unlink_if_present(dir.fd, "audio.pcm");
    unlink_if_present(dir.fd, "media.mp4");
    unlink_if_present(dir.fd, "manifest.json");
    unlink_if_present(blocks, name.c_str(), AT_REMOVEDIR);
}

std::int64_t now_us()
{
    return std::chrono::time_point_cast<std::chrono::microseconds>(std::chrono::system_clock::now())
        .time_since_epoch()
        .count();
}

} // namespace bacuri::vms::archive
