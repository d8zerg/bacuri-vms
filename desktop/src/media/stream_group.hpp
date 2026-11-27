#pragma once

#include "media/video_stream.hpp"

#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QTimer>

#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

using GstClock = struct _GstClock;
class QQuickWindow;

namespace bacuri::vms::client
{
// The video cells of one grid on a common timeline. The group prerolls its streams on worker
// threads, the first one alone so that it publishes the device contexts and the others four at a
// time, and starts all of them on the system clock with one base time, so the same position is
// shown in every cell at the same moment. A seek pauses the grid, seeks the streams on worker
// threads four at a time and starts the grid again on the common timeline. A stream rebuilt after a decoder
// failure joins the timeline at its resume position. Live streams play as they arrive.
class StreamGroup : public QObject
{
    Q_OBJECT

  public:
    // frame_ns is the frame period of the sources, used to measure the spread in frames.
    StreamGroup(QQuickWindow* window, std::int64_t frame_ns, QObject* parent = nullptr);
    ~StreamGroup() override;

    VideoStream& add(std::unique_ptr<VideoStream> stream);
    const std::vector<std::unique_ptr<VideoStream>>& streams() const
    {
        return streams_;
    }
    // Prerolls and starts the grid after the window has drawn its first frame.
    void start();
    void seek_to_frame(std::int64_t frame);
    // Start and seek durations of the whole grid and the spread of shown frames between cells.
    QJsonObject report() const;

  private:
    void prepare(std::int64_t position_ns, bool seek, std::int64_t frame);
    void play_all(std::int64_t position_ns);
    std::uint64_t base_for(std::int64_t position_ns) const;
    void sample();

    QPointer<QQuickWindow> window_;
    std::vector<std::unique_ptr<VideoStream>> streams_;
    std::int64_t frame_ns_;
    GstClock* clock_{};
    mutable std::mutex mutex_;
    std::uint64_t base_{};
    std::int64_t position_{};
    bool playing_{};
    bool started_{};
    std::chrono::steady_clock::time_point operation_;
    std::optional<double> start_ms_;
    std::optional<double> seek_ms_;
    std::vector<double> spread_frames_;
    QTimer sampler_;
};
} // namespace bacuri::vms::client
