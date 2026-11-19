#pragma once
#include "archive/manifest.hpp"
#include "storage/sqlite.hpp"
#include <optional>
#include <functional>
#include <mutex>

namespace bacuri::vms::archive
{
namespace fs = std::filesystem;
Json archive_status(const fs::path& root);
enum class CommitStage { media_synced, manifest_synced, directory_synced, renamed, parent_synced, journaled };
struct Gap {
    std::string point;
    std::int64_t binding, begin, end;
    std::string reason;
};
struct Pending {
    std::string directory, manifest, unavailable;
};

class Volume
{
  public:
    using MediaWriter = std::function<void(const fs::path&)>;
    using Observer = std::function<void(CommitStage)>;
    Volume(const fs::path& root, Owner owner);
    void publish(const Block&, const MediaWriter&, const Observer& observer = {});
    void report_gap(const Gap&);
    std::optional<std::pair<std::int64_t, Gap>> pending_gap();
    void acknowledge_gap(std::int64_t id);
    const Owner& owner() const
    {
        return identity_.owner;
    }
    std::optional<Pending> pending();
    std::vector<std::pair<std::int64_t, std::int64_t>> pending_intervals(const std::string& point,
                                                                         std::int64_t begin, std::int64_t end,
                                                                         std::optional<std::int64_t> binding = {}) const;
    [[nodiscard]] std::int64_t generation() const noexcept
    {
        return identity_.generation;
    }
    void acknowledge(const Pending&, const std::string& rejection = {});

  private:
    struct Lock {
        explicit Lock(const fs::path&);
        ~Lock();
        Lock(const Lock&) = delete;
        Lock& operator=(const Lock&) = delete;
        int fd;
    };
    void reconcile();
    void recover_directory(const fs::directory_entry&);
    void record(const std::string& relative, const Json& manifest);
    void availability(const std::string& relative, const std::string& reason);
    fs::path root_;
    Lock lock_;
    storage::Sqlite db_;
    Identity identity_;
    std::mutex mutex_;
    std::optional<std::pair<std::string, Json>> published_;
    void journal_published(const Observer& observer = {});
};
} // namespace bacuri::vms::archive
