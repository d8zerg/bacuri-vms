#pragma once
#include "archive/manifest.hpp"
#include "storage/sqlite/sqlite.hpp"
#include <mutex>
#include <functional>

namespace bacuri::vms::archive
{
namespace fs = std::filesystem;
enum class CommitStage { media_synced, manifest_synced, directory_synced, renamed, parent_synced, journaled };
struct Gap {
    std::string point;
    std::int64_t binding, begin, end;
    std::string reason;
};
class Pending
{
  public:
    std::string directory;
    BlockManifest manifest;
    std::string unavailable;

  private:
    friend class BlockJournal;
    Pending(std::string directory, std::string stored, std::string unavailable);
    std::string stored_;
};
class BlockJournal
{
  public:
    void visit_inventory(const std::function<void(const BlockManifest&, const std::string&)>& visit);
    void confirm_inventory();
    void visit_gaps(const std::function<void(const Gap&)>& visit);
    void report_gap(const Gap&);
    std::optional<std::pair<std::int64_t, Gap>> pending_gap();
    void acknowledge_gap(std::int64_t id);
    std::optional<Pending> pending();
    std::vector<std::pair<std::int64_t, std::int64_t>>
    pending_intervals(const std::string& point, std::int64_t begin, std::int64_t end,
                      std::optional<std::int64_t> binding = {},
                      std::int64_t cutoff = std::numeric_limits<std::int64_t>::min()) const;
    void acknowledge(const Pending&, const std::string& rejection = {});

  private:
    friend class Volume;
    friend class VolumeRetention;
    friend class BlockVerification;
    BlockJournal(const fs::path& root, Owner owner);
    bool assign_owner();
    bool overlaps(const Block&);
    bool contains(const std::string& relative);
    void mark_missing_directories();
    void finish_open(bool require_manifests, bool assigned);
    void record(const std::string& relative, const BlockManifest& manifest);
    bool availability(const std::string& relative, const std::string& reason);
    void journal_published(const std::function<void(CommitStage)>& observer = {});
    const fs::path& root_;
    storage::Sqlite db_;
    Identity identity_;
    mutable std::mutex mutex_;
    std::optional<std::pair<std::string, BlockManifest>> published_;
};
} // namespace bacuri::vms::archive
