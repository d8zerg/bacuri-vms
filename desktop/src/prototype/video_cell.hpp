#pragma once

#include <QJsonObject>
#include <QString>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>

using GstElement = struct _GstElement;
class QQuickItem;
class QQuickWindow;

namespace bacuri::desktop
{
struct CellOptions {
    QString source;  // A local MP4 file or an rtsp:// address.
    QString codec;   // h264 or h265.
    bool hardware{}; // NVDEC decoder instead of the libav software decoder.
    int rate_numerator{25};
    int rate_denominator{1};
};

// One video cell: a GStreamer pipeline from the source through the decoder into a qml6glsink
// that draws into a QML item. Probes record the decoder output memory, the frames that reach
// the sink and the first frame after start and after a seek.
class VideoCell
{
  public:
    VideoCell(int index, CellOptions options);
    VideoCell(const VideoCell&) = delete;
    VideoCell& operator=(const VideoCell&) = delete;
    ~VideoCell();

    // Creates the sink, which also registers the QML video item type.
    void create();
    void attach(QQuickItem* item);
    void start(QQuickWindow* window);
    void seek_to_frame(std::int64_t frame);
    QJsonObject report() const;
    QString error() const;

  private:
    struct Probe;
    void on_decoded_caps(const QString& features);
    void on_flushed();
    void on_frame(std::int64_t pts_ns);

    int index_;
    CellOptions options_;
    GstElement* pipeline_{};
    GstElement* sink_{};
    mutable std::mutex mutex_;
    QString decoder_features_;
    QString error_;
    std::int64_t frames_{};
    std::optional<std::int64_t> first_pts_;
    std::optional<std::chrono::steady_clock::time_point> started_;
    std::optional<double> first_frame_ms_;
    std::optional<std::chrono::steady_clock::time_point> seek_requested_;
    std::optional<std::int64_t> seek_target_;
    std::optional<std::int64_t> seek_pts_;
    std::optional<double> seek_ms_;
    std::atomic<bool> awaiting_seek_{};
};
} // namespace bacuri::desktop
