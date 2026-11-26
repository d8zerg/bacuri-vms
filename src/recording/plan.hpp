#pragma once
#include "recording/policy.hpp"
#include "storage/sqlite/sqlite.hpp"
#include <map>
#include <mutex>
namespace bacuri::vms::recording
{
class RecordingPlan
{
  public:
    explicit RecordingPlan(const std::filesystem::path& archive_root);
    void update(std::string payload);
    std::shared_ptr<const CaptureRule> rule(const std::string& point) const;

  private:
    storage::Sqlite db_;
    mutable std::mutex mutex_;
    std::map<std::string, std::shared_ptr<const CaptureRule>> rules_;
    std::string payload_;
};
} // namespace bacuri::vms::recording
