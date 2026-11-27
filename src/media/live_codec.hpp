#pragma once
#include "media/acceleration/codec_task.hpp"
#include "media/privacy.hpp"
#include <chrono>
#include <deque>
#include <functional>
#include <memory>
#include <optional>

namespace bacuri::vms::media
{
// Codec changes keep the transport, its clock and the last published frame.
class LiveCodec
{
  public:
    using Output = std::function<void(std::string_view, bool, GstClockTime, const std::string&)>;
    enum class Format { h264, gray };
    LiveCodec(std::string codec, PrivacyMask mask, Format format = Format::h264);
    ~LiveCodec();
    LiveCodec(const LiveCodec&) = delete;
    LiveCodec& operator=(const LiveCodec&) = delete;
    void push(std::string_view bytes, bool keyframe, GstClockTime pts, const Output&);
    void poll(const Output&);
    void finish(const Output&);

  private:
    struct Pipeline;
    struct Frame {
        std::string bytes;
        bool keyframe;
        GstClockTime pts;
    };
    void pump(const Output&, bool finish);
    void drain(const Output&, GstClockTime wait);
    void trim();
    const std::string codec_;
    const bool analysis_;
    PrivacyMask mask_;
    acceleration::CodecTask task_;
    acceleration::CodecPath path_;
    std::unique_ptr<Pipeline> pipe_;
    std::deque<Frame> history_;
    std::optional<std::size_t> cpu_from_;
    std::size_t first_{}, published_{}, fed_{}, bytes_{};
    std::chrono::steady_clock::time_point progress_{};
};
} // namespace bacuri::vms::media
