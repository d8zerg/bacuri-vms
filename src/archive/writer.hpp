#pragma once
#include "archive/volume.hpp"
#include "recorder/ingest.hpp"
namespace bacuri::vms::archive
{
class Writer
{
  public:
    Writer(Volume&, Origin, std::shared_ptr<ingest::FrameQueue>);

  private:
    void run(std::stop_token);
    void finish();
    Volume& volume_;
    Origin origin_;
    std::shared_ptr<ingest::FrameQueue> queue_;
    std::vector<ingest::FramePtr> frames_;
    std::size_t bytes_{};
    std::jthread worker_;
};
} // namespace bacuri::vms::archive
