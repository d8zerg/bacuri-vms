#include "application/export/package.hpp"
#include <semaphore>
#include "modules/access/operations.hpp"
#include "modules/recordings/operations.hpp"

namespace bacuri::vms::control
{
using namespace std::chrono_literals;
namespace
{
std::counting_semaphore<2> export_slots{2};
}
ExportSlot::ExportSlot()
{
    if (!export_slots.try_acquire())
        throw Failure{ErrorCode::rate_limited};
}
ExportSlot::~ExportSlot()
{
    export_slots.release();
}
void write_text(const fs::path& path, const std::string& value)
{
    std::ofstream out(path, std::ios::binary);
    out.exceptions(std::ios::failbit | std::ios::badbit);
    out << value;
    out.close();
}
ExportBody::ExportBody(std::shared_ptr<ExportSlot> capacity) : slot{std::move(capacity)}
{
    std::unique_ptr<char, decltype(&g_free)> temp{g_dir_make_tmp("bacuri-export-XXXXXX", nullptr), g_free};
    if (!temp)
        throw std::runtime_error("Cannot create temporary export directory");
    root = temp.get();
}
ExportBody::~ExportBody()
{
    std::error_code error;
    fs::remove_all(root, error);
}
std::chrono::steady_clock::time_point ExportBody::authorize()
{
    if (group_lease)
        return group_lease();
    const auto now = std::chrono::steady_clock::now();
    db.exec("BEGIN ISOLATION LEVEL REPEATABLE READ READ ONLY");
    const auto lease = access::export_lease(db, browser, grant, point, begin, end, derivative, regions, sound);
    const auto ms = lease ? std::min(lease->milliseconds,
                                     recordings::export_lifetime_ms(db, lease->site, lease->point, hashes))
                          : 0;
    db.exec("COMMIT");
    if (ms <= 0)
        throw Failure{ErrorCode::forbidden};
    return now + std::chrono::milliseconds{ms};
}
MediaChunk ExportBody::next()
{
    if (std::chrono::steady_clock::now() + 100ms >= deadline)
        deadline = authorize();
    constexpr std::streamsize chunk_size = 65536;
    std::string bytes(static_cast<std::size_t>(chunk_size), '\0');
    input.read(bytes.data(), chunk_size);
    bytes.resize(static_cast<std::size_t>(input.gcount()));
    return {std::move(bytes), deadline};
}
void pack(const fs::path& root, const std::vector<std::string>& clips)
{
    std::vector<std::string> args{"/usr/bin/tar", "--format=ustar", "-cf",         "../export.tar",
                                  "--",           "manifest.json",  "manifest.sig"};
    args.insert(args.end(), clips.begin(), clips.end());
    std::vector<char*> argv;
    argv.reserve(args.size());
    for (auto& arg : args)
        argv.push_back(arg.data());
    argv.push_back(nullptr);
    int status{};
    if (!g_spawn_sync((root / "data").c_str(), argv.data(), nullptr, G_SPAWN_DEFAULT, nullptr, nullptr, nullptr,
                      nullptr, &status, nullptr) ||
        !g_spawn_check_wait_status(status, nullptr))
        throw std::runtime_error("Cannot package export files");
}
} // namespace bacuri::vms::control
