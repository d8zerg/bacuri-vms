#pragma once
#include "media/native_frame.hpp"

#include <QJsonObject>
#include <QPointer>
#include <QSize>
#include <QString>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>

using GstElement = struct _GstElement;
using GstBus = struct _GstBus;
using GstMessage = struct _GstMessage;
using GstClock = struct _GstClock;
class QQuickItem;
class QQuickWindow;

namespace bacuri::vms::client
{
enum class StreamSource { file_or_rtsp, webrtc };

enum class DecoderChoice { automatic, hardware, software };

struct StreamOptions {
    QString source; // A local MP4 file or an rtsp:// address.
    QString codec;  // h264 or h265.
    DecoderChoice decoder{DecoderChoice::automatic};
    int rate_numerator{25};
    int rate_denominator{1};
    // Limits of a software decoded stream, such as on a station without a GPU: the largest picture
    // and the highest frame rate passed to drawing. Empty and zero mean no limit.
    QSize max_size{};
    int max_rate{};
    StreamSource transport{StreamSource::file_or_rtsp};
};

// The hardware decoder of a codec on this station: NVDEC, then VA-API, or empty without one.
QString hardware_decoder(const QString& codec);

// One video cell: a GStreamer pipeline from the source through the decoder into a qml6glsink
// that draws into a QML item. Probes record the decoder output memory, the frames that reach
// the sink and the first frame after a start, a seek and a fallback.
//
// The pipeline is created and attached on the GUI thread. Its state changes, which may block until
// the decoder and the sink are ready, are called on worker threads by the stream group, and the
// start on a common clock runs on the render thread. Pipelines share the CUDA, VA and GL contexts
// of the process, so the decoders of a grid do not initialize the device one by one.
//
// When the hardware decoder fails during playback the cell rebuilds its pipeline with the software
// decoder, continues from the last shown frame and joins the common clock of the group again.
class VideoStream
{
  public:
    // Places a rebuilt pipeline on the common timeline: called with the stream on a worker thread
    // after the pipeline has prerolled at the resume position.
    using Resume = std::function<void(VideoStream&, std::int64_t position_ns)>;

    VideoStream(int index, StreamOptions options);
    VideoStream(const VideoStream&) = delete;
    VideoStream& operator=(const VideoStream&) = delete;
    ~VideoStream();

    // Creates the pipeline and its sink, which also registers the QML video item type.
    void create();
    void attach(QQuickItem* item, QQuickWindow* window);
    void attachFrameOwner(QQuickItem* item);
    static bool registerVideoItem();
    GstElement* webRtcPeer() const;
    void onFirstFrame(std::function<void()> handler);
    bool hardwareActive() const
    {
        return hardware_;
    }
    bool webRtc() const
    {
        return options_.transport == StreamSource::webrtc;
    }
    void on_resume(Resume resume);

    // Blocking steps for a worker thread. preroll and seek_paused wait until the first frame of
    // the new position reached the sink, or the timeout passed for a live source.
    bool preroll();
    bool seek_paused(std::int64_t frame);
    // Starts playback so that running time zero is the clock time base. Called on the render thread.
    void play_at(GstClock* clock, std::uint64_t base);

    // Reports a failure from the hardware decoder, as a lost device would.
    void inject_decoder_fault();
    bool live() const;
    // Presentation time of the last frame that reached the sink.
    std::optional<std::int64_t> last_pts() const;
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

    NativeFrame native_;
    int index_;
    StreamOptions options_;
    QString hardware_name_;
    GstElement* pipeline_{};
    GstElement* sink_{};
    unsigned bus_watch_{};
    QQuickItem* item_{};
    QPointer<QQuickWindow> window_;
    Resume resume_;
    std::function<void()> firstFrame_;
    bool hardware_{};
    mutable std::mutex mutex_;
    QString decoder_features_;
    QString error_;
    std::int64_t frames_{};
    std::optional<std::int64_t> last_pts_;
    std::optional<std::int64_t> first_pts_;
    std::optional<std::chrono::steady_clock::time_point> created_;
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
} // namespace bacuri::vms::client
