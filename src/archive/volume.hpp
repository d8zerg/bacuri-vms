#pragma once
#include "archive/block_verification.hpp"
#include "archive/retention.hpp"

namespace bacuri::vms::archive
{
using InvalidBlock = std::function<void(const std::string&)>;
struct VolumeOptions {
    // Mount of the second archive copy. New blocks are copied there and damaged blocks are repaired from it.
    std::optional<fs::path> replica;
    // Receives each directory that fails verification while the journal is rebuilt from the directories.
    InvalidBlock invalid;
};
struct RecordedSound {
    std::int64_t start_us{};
    std::string_view pcm{};
};
struct AudioSlice {
    std::string block, hash;
    std::size_t offset, count;
};
// Owns the archive directory lock and publishes and recovers block files.
class Volume
{
  private:
    struct Lock {
        explicit Lock(const fs::path&);
        ~Lock();
        Lock(const Lock&) = delete;
        Lock& operator=(const Lock&) = delete;
        int fd;
    };
    fs::path root_;
    std::optional<fs::path> replica_;
    Lock lock_;
    std::mutex replica_mutex_;

  public:
    BlockJournal journal;
    VolumeRetention retention;
    BlockVerification verification;
    using MediaWriter = std::function<void(const fs::path&)>;
    using Observer = std::function<void(CommitStage)>;
    // Opening reads only metadata: it imports published directories absent from the journal,
    // completes interrupted removals and marks journal entries without a directory as missing.
    // Media of journaled blocks is verified by verify_next() or verify_all().
    Volume(const fs::path& root, Owner owner, VolumeOptions options = {});
    bool publish(const Block&, const MediaWriter&, const Observer& observer = {},
                 std::uintmax_t expected_bytes = 0, RecordedSound sound = {});
    std::string read_audio(const AudioSlice& slice);
    Json status() const
    {
        return archive_status(root_, replica_);
    }
    const Owner& owner() const
    {
        return journal.identity_.owner;
    }
    [[nodiscard]] std::int64_t generation() const noexcept
    {
        return journal.identity_.generation;
    }

  private:
    void reconcile(const InvalidBlock&);
    void recover_directory(const fs::directory_entry&, const InvalidBlock&);
};
} // namespace bacuri::vms::archive
