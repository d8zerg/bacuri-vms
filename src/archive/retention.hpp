#pragma once
#include "archive/block_journal.hpp"
#include <map>

namespace bacuri::vms::archive
{
enum class RetirementStage { authorized, renamed, removed };
enum class CapacityState { normal, low_space, recording_stopped };
struct ReplicaStatus {
    std::int64_t verified{}, failed{}, pending{}, last_checked_us{};
    bool online{};
};
struct ArchiveStatus {
    std::int64_t committed{}, pending{}, missing{}, corrupt{}, rejected{}, removed_blocks{};
    std::uintmax_t media_bytes{}, capacity_bytes{}, available_bytes{}, reserve_bytes{};
    int reserve_percent{};
    CapacityState state{};
    std::optional<int> retention_days;
    std::optional<std::int64_t> last_commit_utc_us, cutoff_utc_us;
    std::optional<double> write_bytes_per_second, capacity_depth_seconds, estimated_depth_seconds,
        remaining_seconds;
    std::optional<ReplicaStatus> replica;
};
ArchiveStatus archive_status(const fs::path& root, const std::optional<fs::path>& replica = {});
struct RetentionPolicy {
    std::int64_t cutoff_us;
    int days;
    int reserve_percent;
};
CapacityState capacity_state(int percent, const fs::space_info& space, std::uintmax_t blocked);
class VolumeRetention
{
  public:
    ~VolumeRetention() = default;
    VolumeRetention(const VolumeRetention&) = delete;
    VolumeRetention& operator=(const VolumeRetention&) = delete;
    CapacityState capacity() const;
    using RetirementObserver = std::function<void(RetirementStage)>;
    void apply_retention(const RetentionPolicy& policy, const RetirementObserver& observer = {});
    std::map<std::string, std::int64_t> point_cutoffs();
    void protect_blocks(const std::vector<std::string>& ids);
    void remember_cutoffs(const std::map<std::string, std::int64_t>& cutoffs);
    void reclaim_authorized();
    std::int64_t retired_through();

  private:
    friend class Volume;
    VolumeRetention(BlockJournal& journal, std::mutex& replica_mutex, int directory, bool replicated);
    BlockJournal& journal_;
    std::mutex& replica_mutex_;
    int directory_;
    bool replicated_;
    void reap(const RetirementObserver& observer);
    void require_space(std::uintmax_t expected_bytes, std::uintmax_t staged_bytes);
    std::optional<CapacityState> last_capacity_state_;
    void report_capacity(const fs::space_info& space, std::uintmax_t blocked);
    int reserve_percent_{20};
};
} // namespace bacuri::vms::archive
