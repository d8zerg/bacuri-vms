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
    struct Segment {
        std::vector<ingest::FramePtr> frames;
        std::size_t bytes;
        ingest::Sound audio;
        std::string policy;
        std::int64_t revision;
    };
    void finish();
    void drain(bool stopping);
    void publish(Segment segment, std::size_t first, std::size_t last);
    std::deque<Segment> buffered_;
    std::size_t buffered_bytes_{};
    std::shared_ptr<const CaptureRule> rule_;
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
