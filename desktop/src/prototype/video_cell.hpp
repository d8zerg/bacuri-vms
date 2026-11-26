#pragma once

#include <QJsonObject>
#include <QString>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>

using GstElement = struct _GstElement;
using GstBus = struct _GstBus;
using GstMessage = struct _GstMessage;
class QQuickItem;
class QQuickWindow;

namespace bacuri::desktop
{
enum class DecoderChoice { automatic, hardware, software };

struct CellOptions {
    QString source; // A local MP4 file or an rtsp:// address.
    QString codec;  // h264 or h265.
    DecoderChoice decoder{DecoderChoice::automatic};
    int rate_numerator{25};
    int rate_denominator{1};
};

// One video cell: a GStreamer pipeline from the source through the decoder into a qml6glsink
// that draws into a QML item. Probes record the decoder output memory, the frames that reach
// the sink and the first frame after start and after a seek.
//
// When the hardware decoder is unavailable the cell starts with the software decoder. When the
// hardware decoder fails during playback the cell rebuilds its pipeline with the software
// decoder and continues from the last shown frame, so the window, the layout and the other
// cells keep working.
class VideoCell
{
  public:
    VideoCell(int index, CellOptions options);
    VideoCell(const VideoCell&) = delete;
    VideoCell& operator=(const VideoCell&) = delete;
    ~VideoCell();

    // Creates the pipeline and its sink, which also registers the QML video item type.
    void create();
    void attach(QQuickItem* item);
    void start(QQuickWindow* window);
    void seek_to_frame(std::int64_t frame);
    // Reports a failure from the hardware decoder, as a lost device would.
    void inject_decoder_fault();
    QJsonObject report() const;
    QString error() const;

  private:
    struct Probe;
    static int on_bus(GstBus* bus, GstMessage* message, void* data);
    void build(bool hardware);
    void release();
    void fall_back(const QString& reason);
    void on_decoded_caps(const QString& features);
    void on_flushed();
    void on_frame(std::int64_t pts_ns);
    QString decoder_name(bool hardware) const;

    int index_;
    CellOptions options_;
    GstElement* pipeline_{};
    GstElement* sink_{};
    unsigned bus_watch_{};
    QQuickItem* item_{};
    QQuickWindow* window_{};
    bool hardware_{};
    bool hardware_available_{};
    mutable std::mutex mutex_;
    QString decoder_features_;
    QString error_;
    std::int64_t frames_{};
    std::optional<std::int64_t> last_pts_;
    std::optional<std::int64_t> first_pts_;
    std::optional<std::chrono::steady_clock::time_point> started_;
    std::optional<double> first_frame_ms_;
    std::optional<std::chrono::steady_clock::time_point> seek_requested_;
    std::optional<std::int64_t> seek_target_;
    std::optional<std::int64_t> seek_pts_;
    std::optional<double> seek_ms_;
    std::atomic<bool> awaiting_seek_{};
    QString fallback_reason_;
    std::optional<std::chrono::steady_clock::time_point> fallback_started_;
    std::optional<double> fallback_at_ms_;
    std::optional<std::int64_t> fallback_from_pts_;
    std::optional<double> resumed_after_ms_;
    std::optional<std::int64_t> resumed_pts_;
    bool resume_waits_for_flush_{};
};
} // namespace bacuri::desktop
