#pragma once
#include "archive/recording_policy.hpp"
#include <map>
#include <set>
#include "storage/sqlite.hpp"
#include <optional>
#include <functional>
#include <mutex>
#include <limits>

namespace bacuri::vms::archive
{
namespace fs = std::filesystem;
Json archive_status(const fs::path& root);
enum class CommitStage { media_synced, manifest_synced, directory_synced, renamed, parent_synced, journaled };
enum class RetirementStage { authorized, renamed, removed };
struct RetentionPolicy {
    std::int64_t cutoff_us;
    int days;
    int reserve_percent;
};

struct Gap {
    std::string point;
    std::int64_t binding, begin, end;
    std::string reason;
};
struct Pending {
    std::string directory, manifest, unavailable;
};

struct RecordedSound {
    std::int64_t start_us{};
    std::string_view pcm{};
};
struct AudioSlice {
    std::string block, hash;
    std::size_t offset, count;
};

class Volume
{
  public:
    using MediaWriter = std::function<void(const fs::path&)>;
    using Observer = std::function<void(CommitStage)>;
    using InvalidBlock = std::function<void(const std::string&)>;
    Volume(const fs::path& root, Owner owner, const InvalidBlock& invalid = {});
    void visit_inventory(const std::function<void(const Json&, const std::string&)>& visit);
    bool publish(const Block&, const MediaWriter&, const Observer& observer = {},
                 std::uintmax_t expected_bytes = 0, RecordedSound sound = {});
    std::string read_audio(const AudioSlice& slice);
    using RetirementObserver = std::function<void(RetirementStage)>;
    void apply_retention(const RetentionPolicy& policy, const RetirementObserver& observer = {});
    void update_recording_plan(const std::string& payload);
    std::shared_ptr<const CaptureRule> recording_rule(const std::string& point);
    std::map<std::string, std::int64_t> point_cutoffs();
    void protect_blocks(const std::vector<std::string>& ids);
    void remember_cutoffs(const std::map<std::string, std::int64_t>& cutoffs);
    Json status() const
    {
        return archive_status(root_);
    }
    void reclaim_authorized();
    bool scrub_copy();
    void set_detectors(std::set<std::string> value)
    {
        std::scoped_lock lock{plan_mutex_};
        detectors_ = std::move(value);
    }
    bool detector_enabled(const std::string& point)
    {
        std::scoped_lock lock{plan_mutex_};
        return detectors_.contains(point);
    }
    std::int64_t retired_through();
    void confirm_inventory();
    void visit_gaps(const std::function<void(const Gap&)>& visit);
    void report_gap(const Gap&);
    std::optional<std::pair<std::int64_t, Gap>> pending_gap();
    void acknowledge_gap(std::int64_t id);
    const Owner& owner() const
    {
        return identity_.owner;
    }
    std::optional<Pending> pending();
    std::vector<std::pair<std::int64_t, std::int64_t>>
    pending_intervals(const std::string& point, std::int64_t begin, std::int64_t end,
                      std::optional<std::int64_t> binding = {},
                      std::int64_t cutoff = std::numeric_limits<std::int64_t>::min()) const;
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
    void reconcile(const InvalidBlock&);
    void recover_directory(const fs::directory_entry&, const InvalidBlock&);
    void record(const std::string& relative, const Json& manifest);
    void availability(const std::string& relative, const std::string& reason);
    fs::path root_;
    Lock lock_;
    storage::Sqlite db_;
    Identity identity_;
    std::mutex mutex_, plan_mutex_, replica_mutex_;
    std::set<std::string> detectors_;
    std::string plan_payload_;
    std::map<std::string, std::shared_ptr<const CaptureRule>> recording_rules_;
    std::optional<std::pair<std::string, Json>> published_;
    void reap(const RetirementObserver& observer);
    void require_space(std::uintmax_t expected_bytes, std::uintmax_t staged_bytes);
    std::string_view last_capacity_state_;
    void report_capacity(const fs::space_info& space, std::uintmax_t blocked);
    int reserve_percent_{20};
    void journal_published(const Observer& observer = {});
};
} // namespace bacuri::vms::archive
