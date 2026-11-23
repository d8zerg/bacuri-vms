#pragma once
#include "recorder/ingest.hpp"
#include <atomic>
#include <map>
#include <functional>

namespace bacuri::vms::ingest
{
struct Sound {
    std::int64_t start_us{};
    std::string pcm;
};

class CameraAudio
{
  public:
    explicit CameraAudio(Endpoint endpoint);
    ~CameraAudio();
    Sound listen(std::int64_t after);
    void record(const std::string& owner, bool enable);
    Sound recorded(std::int64_t from, std::chrono::microseconds duration);
    void speak(const std::string& session, std::chrono::milliseconds ttl, const std::string& pcm);
    void mute(const std::string& session);

  private:
    using Clock = std::chrono::steady_clock;
    void capture(std::stop_token stop);
    void transmit(std::stop_token stop);
    struct Sample {
        Sound sound;
        bool record;
    };
    Endpoint endpoint_;
    std::mutex mutex_, talk_mutex_;
    std::condition_variable_any ready_, talk_ready_;
    std::deque<Sample> samples_;
    std::map<std::string, Clock::time_point> record_leases_;
    std::deque<std::string> speech_;
    std::string talk_session_;
    Clock::time_point listen_until_{}, record_until_{}, talk_until_{};
    bool output_ready_{}, output_failed_{}, input_failed_{};
    std::jthread capture_, transmit_;
};
} // namespace bacuri::vms::ingest
