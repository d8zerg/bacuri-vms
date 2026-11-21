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

struct Frame {
    std::vector<std::byte> h264;
    domain::ProjectedTime time;
    std::optional<std::uint32_t> rtp_timestamp;
    std::optional<std::uint64_t> ntp_ns;
    std::optional<std::uint64_t> pts_ns;
    bool keyframe;
    std::optional<std::uint64_t> duration_ns{};
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
};

struct StreamStats {
    std::uint64_t frames{}, bytes{}, keyframes{}, reconnects{}, gaps{};
    int width{}, height{};
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
    ~Stream();
    void request_stop();
    [[nodiscard]] std::shared_ptr<FrameQueue> subscribe();
    [[nodiscard]] StreamStats stats() const;

  private:
    void run(std::stop_token stop);
    void receive(const FramePtr& frame, VideoFormat format, bool discontinuity);
    void interrupt(std::string state);
    void report() const;

    const StreamOptions options_;
    mutable std::mutex mutex_;
    StreamStats stats_;
    std::vector<std::weak_ptr<FrameQueue>> consumers_;
    std::condition_variable_any retry_;
    std::jthread worker_;
};

} // namespace bacuri::vms::ingest
