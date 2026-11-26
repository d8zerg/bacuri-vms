#pragma once

#include "domain/time.hpp"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace bacuri::vms::ingest
{

struct Endpoint {
    std::string url;
    std::string username;
    std::string password;
    friend bool operator==(const Endpoint&, const Endpoint&) = default;
};

enum class Profile { main, secondary };

struct VideoFormat {
    int width;
    int height;
};

enum class VideoCodec { h264, h265 };

struct Frame {
    std::vector<std::byte> access_unit;
    domain::ProjectedTime time;
    std::optional<std::uint32_t> rtp_timestamp;
    std::optional<std::uint64_t> ntp_ns;
    std::optional<std::uint64_t> pts_ns;
    bool keyframe;
    std::optional<std::uint64_t> duration_ns{};
    VideoCodec codec{VideoCodec::h264};
};

using FramePtr = std::shared_ptr<const Frame>;

struct Limits {
    std::size_t frames;
    std::size_t bytes;
};

constexpr Limits limits(Profile profile)
{
    constexpr auto mib = 1024uz * 1024;
    return profile == Profile::main ? Limits{64, 8 * mib} : Limits{8, mib};
}

struct QueueStats {
    std::size_t depth{}, bytes{}, peak_depth{}, peak_bytes{};
    std::uint64_t dropped{};
    std::optional<domain::UtcTime> loss_begin, recovered_at;
};

class FrameQueue
{
  public:
    explicit FrameQueue(Profile profile) : profile_{profile}, capacity_{limits(profile)} {}
    // Starts with the frames received since the latest keyframe, so a new consumer can decode at
    // once. These frames do not count against the capacity until they are taken.
    FrameQueue(Profile profile, const std::vector<FramePtr>& group);
    void push(const FramePtr& frame);
    void interrupt();
    [[nodiscard]] FramePtr try_pop();
    [[nodiscard]] FramePtr wait_pop(std::chrono::steady_clock::time_point deadline);
    [[nodiscard]] QueueStats stats() const;

  private:
    FramePtr pop_locked();
    const Profile profile_;
    const Limits capacity_;
    mutable std::mutex mutex_;
    std::condition_variable ready_;
    std::deque<FramePtr> frames_;
    QueueStats stats_;
    bool need_keyframe_{true};
    // Initial frames not yet taken. They extend the capacity until they are taken.
    std::size_t group_frames_{}, group_bytes_{};
};

struct StreamStats {
    std::uint64_t frames{}, bytes{}, keyframes{}, reconnects{}, gaps{};
    int width{}, height{};
    unsigned bitrate_kbps{};
    VideoCodec codec{VideoCodec::h264};
    std::string state{"connecting"};
    std::optional<domain::UtcTime> gap_begin, gap_end;
    std::optional<domain::ProjectedTime> time;
    std::optional<std::uint32_t> rtp_timestamp;
    std::optional<std::uint64_t> ntp_ns, pts_ns;
    std::vector<QueueStats> consumers;
};

struct StreamOptions {
    Profile profile;
    Endpoint endpoint;
    std::function<std::optional<std::string>(const std::string&)> stream_url;
    std::function<void(const StreamStats&)> report;
};

class Stream
{
  public:
    explicit Stream(StreamOptions options);
    Stream(const Stream&) = delete;
    Stream& operator=(const Stream&) = delete;
    ~Stream();
    void request_stop();
    [[nodiscard]] std::shared_ptr<FrameQueue> subscribe();
    [[nodiscard]] StreamStats stats() const;

  private:
    void run(const std::stop_token& stop);
    void receive(const FramePtr& frame, VideoFormat format, bool discontinuity);
    void interrupt(std::string state);
    void remember(const FramePtr& frame);
    void report() const;

    const StreamOptions options_;
    mutable std::mutex mutex_;
    StreamStats stats_;
    std::chrono::steady_clock::time_point sample_at_{};
    std::uint64_t sample_bytes_{};
    std::vector<std::weak_ptr<FrameQueue>> consumers_;
    // Frames since the latest keyframe. A group longer than the main queue capacity is not kept,
    // because live delivery could not accept it at once.
    std::vector<FramePtr> group_;
    std::size_t group_bytes_{};
    std::condition_variable_any retry_;
    std::jthread worker_;
};

} // namespace bacuri::vms::ingest
