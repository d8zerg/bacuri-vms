#pragma once
#include "recording/plan.hpp"
#include "archive/volume.hpp"
#include "ingest/audio.hpp"
#include <chrono>
namespace bacuri::vms::archive
{
// A block closes at the next keyframe. Without a keyframe it closes at these limits and the
// following frames are discarded until a keyframe arrives.
inline constexpr std::size_t max_block_frames = 900;
inline constexpr std::size_t max_block_bytes = 32uz * 1024 * 1024;
inline constexpr std::chrono::seconds max_block_duration{15};
// Event and manual recording keep blocks this long after the pre-event interval before publishing them.
inline constexpr std::chrono::seconds event_buffer_margin{3};
// Longest expected delay between a received frame and its published block under this rule.
std::chrono::microseconds publication_lag(const recording::CaptureRule* rule);

struct ReplayResult {
    unsigned blocks{};
    std::int64_t duration_us{};
    // copied, partial, no_frames, timeout or quota.
    std::string outcome;
    // End of the newest copied block, or zero when nothing was copied.
    std::int64_t covered_until_us{};
};
// A replay request copies at most this long. A longer copy is reported as partial.
inline constexpr std::chrono::seconds replay_time_budget{30};
struct ReplayRequest {
    Origin origin;
    std::string recording;
    ingest::Endpoint endpoint;
    std::int64_t begin{}, end{}, offset{}, uncertainty{};
};
ReplayResult recover_gap(Volume&, const ReplayRequest&, const std::function<bool()>& cancelled);

class Writer
{
  public:
    Writer(Volume&, const recording::RecordingPlan&, Origin, std::shared_ptr<ingest::FrameQueue>,
           std::shared_ptr<ingest::CameraAudio> audio = {});

  private:
    void run(const std::stop_token&);
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
    void report_discarded();
    std::deque<Segment> buffered_;
    std::size_t buffered_bytes_{};
    std::shared_ptr<const recording::CaptureRule> rule_;
    Volume& volume_;
    const recording::RecordingPlan& plan_;
    Origin origin_;
    std::shared_ptr<ingest::FrameQueue> queue_;
    std::shared_ptr<ingest::CameraAudio> audio_;
    std::vector<ingest::FramePtr> frames_;
    std::size_t bytes_{};
    std::optional<Gap> storage_gap_;
    // Frames discarded after a block reached its limits without a keyframe.
    std::optional<Gap> discarded_;
    std::jthread worker_;
};
} // namespace bacuri::vms::archive
