#pragma once
#include <cstdint>
#include <cerrno>
#include <system_error>
#include <filesystem>
#include <fcntl.h>
#include <unistd.h>

namespace bacuri::vms::archive
{
namespace fs = std::filesystem;
inline void system_check(int result)
{
    if (result < 0) {
        throw std::system_error(errno, std::generic_category(), "Archive volume I/O");
    }
}
class File
{
  public:
    File(const fs::path& path, int flags) : File(open(path.c_str(), flags | O_CLOEXEC | O_NOFOLLOW, 0600)) {}
    explicit File(int descriptor) : fd{descriptor}
    {
        system_check(fd);
    }
    ~File()
    {
        close(fd);
    }
    File(const File&) = delete;
    File& operator=(const File&) = delete;
    int fd;
};
void sync_path(const fs::path& path, int flags);
void finish_removal(int blocks, const std::string& name);
std::int64_t now_us();
} // namespace bacuri::vms::archive
