#pragma once
#include <set>
#include "archive/manifest.hpp"
#include <mutex>
#include "ingest/ingest.hpp"
namespace bacuri::vms::ingest
{
class DetectorPoints
{
  public:
    void replace(std::set<std::string> points)
    {
        std::scoped_lock lock{mutex_};
        points_ = std::move(points);
    }
    bool enabled(const std::string& point) const
    {
        std::scoped_lock lock{mutex_};
        return points_.contains(point);
    }

  private:
    mutable std::mutex mutex_;
    std::set<std::string> points_;
};
void detect_motion(const std::stop_token&, Stream&, const DetectorPoints&, const archive::Owner&,
                   const std::string& point, std::int64_t binding);
} // namespace bacuri::vms::ingest
