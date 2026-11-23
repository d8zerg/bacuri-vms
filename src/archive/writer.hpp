#pragma once
#include "archive/volume.hpp"
#include "recorder/audio.hpp"
namespace bacuri::vms::archive
{
class Writer
{
  public:
    Writer(Volume&, Origin, std::shared_ptr<ingest::FrameQueue>, std::shared_ptr<ingest::CameraAudio> audio = {});

  private:
    void run(std::stop_token);
    void finish();
    Volume& volume_;
    Origin origin_;
    std::shared_ptr<ingest::FrameQueue> queue_;
    std::shared_ptr<ingest::CameraAudio> audio_;
    std::vector<ingest::FramePtr> frames_;
    std::size_t bytes_{};
    std::optional<Gap> storage_gap_;
    std::jthread worker_;
};
} // namespace bacuri::vms::archive
