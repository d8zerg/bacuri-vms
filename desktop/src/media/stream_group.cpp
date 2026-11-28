#include "media/stream_group.hpp"

#include "diagnostics/frame_stats.hpp"

#include <QQuickWindow>
#include <QRunnable>
#include <QThreadPool>

#include <gst/gst.h>

#include <algorithm>
#include <semaphore>
#include <thread>

namespace bacuri::vms::client
{
namespace
{
using Clock = std::chrono::steady_clock;
// Time between scheduling the start and the base time of the grid, so every pipeline is already
// playing when its first frame is due.
constexpr std::int64_t start_margin_ns = 150'000'000;
// Streams prerolled or seeked at the same time. A flush of many hardware decoders at once holds the
// graphics driver and delays the interface frame by 0.2-0.3 s with 16 H.265 cells; four at a time keep
// the longest frame within 40 ms and start the grid sooner than all at once.
constexpr std::ptrdiff_t parallel_steps = 4;

double elapsed_ms(Clock::time_point since)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - since).count();
}
} // namespace

StreamGroup::StreamGroup(QQuickWindow* window, std::int64_t frame_ns, QObject* parent)
    : QObject{parent}, window_{window}, frame_ns_{frame_ns}, clock_{gst_system_clock_obtain()}
{
    sampler_.setInterval(200);
    connect(&sampler_, &QTimer::timeout, this, &StreamGroup::sample);
}

StreamGroup::~StreamGroup()
{
    gst_object_unref(clock_);
}

VideoStream& StreamGroup::add(std::unique_ptr<VideoStream> stream)
{
    auto& added = *streams_.emplace_back(std::move(stream));
    added.on_resume([this](VideoStream& resumed, std::int64_t position) {
        // A rebuilt stream joins the timeline: its running time zero is the clock time at which the
        // grid reaches the resume position.
        const auto base = base_for(position);
        QMetaObject::invokeMethod(
            this,
            [this, &resumed, base] {
                if (window_) {
                    window_->scheduleRenderJob(
                        QRunnable::create([this, &resumed, base] { resumed.play_at(clock_, base); }),
                        QQuickWindow::BeforeSynchronizingStage);
                }
            },
            Qt::QueuedConnection);
    });
    return added;
}

void StreamGroup::start(std::function<void()> liveStarted)
{
    if (started_ || !window_)
        return;
    started_ = true;
    operation_ = Clock::now();
    // The sinks need the graphics context of the window, which exists after its first frame.
    auto connection = std::make_shared<QMetaObject::Connection>();
    *connection = connect(
        window_, &QQuickWindow::frameSwapped, this, [this, connection, liveStarted = std::move(liveStarted)] {
            disconnect(*connection);
            if (std::ranges::all_of(streams_, [](const auto& stream) { return stream->webRtc(); })) {
                // Signaling supplies the first buffers, so WebRTC cannot preroll.
                for (const auto& stream : streams_)
                    stream->play_at(clock_, 0);
                if (liveStarted)
                    liveStarted();
                return;
            }
            QThreadPool::globalInstance()->start([this] { prepare(0, false, 0); });
        });
    window_->update();
}

void StreamGroup::seek_to_frame(std::int64_t frame)
{
    {
        std::scoped_lock lock{mutex_};
        playing_ = false;
    }
    operation_ = Clock::now();
    const auto position = frame * frame_ns_;
    QThreadPool::globalInstance()->start([this, position, frame] { prepare(position, true, frame); });
}

// Runs on a worker thread: brings every stream to the position, the first one alone at the start and
// the others at most parallel_steps at a time.
void StreamGroup::prepare(std::int64_t position_ns, bool seek, std::int64_t frame)
{
    const auto step = [seek, frame](VideoStream& stream) {
        if (seek && !stream.live())
            stream.seek_paused(frame);
        else if (!seek)
            stream.preroll();
    };
    std::size_t first = 0;
    if (!seek && !streams_.empty()) {
        step(*streams_.front());
        first = 1;
    }
    {
        std::counting_semaphore<parallel_steps> permits{parallel_steps};
        std::vector<std::jthread> workers;
        for (auto index = first; index < streams_.size(); ++index)
            workers.emplace_back([&step, &permits, stream = streams_[index].get()] {
                permits.acquire();
                step(*stream);
                permits.release();
            });
    }
    QMetaObject::invokeMethod(this, [this, position_ns] { play_all(position_ns); }, Qt::QueuedConnection);
}

void StreamGroup::play_all(std::int64_t position_ns)
{
    if (!window_)
        return;
    const auto base =
        static_cast<std::uint64_t>(static_cast<std::int64_t>(gst_clock_get_time(clock_)) + start_margin_ns);
    {
        std::scoped_lock lock{mutex_};
        base_ = base;
        position_ = position_ns;
        playing_ = true;
        (start_ms_ ? seek_ms_ : start_ms_) = elapsed_ms(operation_);
    }
    std::vector<VideoStream*> streams;
    for (auto& stream : streams_)
        streams.push_back(stream.get());
    window_->scheduleRenderJob(QRunnable::create([this, streams, base] {
                                   for (auto* stream : streams)
                                       stream->play_at(clock_, base);
                               }),
                               QQuickWindow::BeforeSynchronizingStage);
    window_->update();
    sampler_.start();
}

std::uint64_t StreamGroup::base_for(std::int64_t position_ns) const
{
    std::scoped_lock lock{mutex_};
    return static_cast<std::uint64_t>(static_cast<std::int64_t>(base_) + position_ns - position_);
}

// The spread of the frames shown in the cells at one moment, in frames, while the grid plays.
void StreamGroup::sample()
{
    {
        std::scoped_lock lock{mutex_};
        if (!playing_ || static_cast<std::int64_t>(gst_clock_get_time(clock_)) <
                             static_cast<std::int64_t>(base_) + 2 * start_margin_ns)
            return;
    }
    std::optional<std::int64_t> lowest;
    std::optional<std::int64_t> highest;
    for (const auto& stream : streams_) {
        if (stream->live())
            return;
        const auto pts = stream->last_pts();
        if (!pts)
            return;
        lowest = std::min(lowest.value_or(*pts), *pts);
        highest = std::max(highest.value_or(*pts), *pts);
    }
    if (lowest && highest) {
        std::scoped_lock lock{mutex_};
        spread_frames_.push_back(static_cast<double>(*highest - *lowest) / static_cast<double>(frame_ns_));
    }
}

QJsonObject StreamGroup::report() const
{
    std::scoped_lock lock{mutex_};
    const auto spread = distribution(spread_frames_);
    QJsonObject result{{"sync_spread_frames", QJsonObject{{"count", static_cast<qint64>(spread.count)},
                                                          {"p50", spread.p50},
                                                          {"p95", spread.p95},
                                                          {"max", spread.max}}}};
    if (start_ms_)
        result.insert("grid_start_ms", *start_ms_);
    if (seek_ms_)
        result.insert("grid_seek_ms", *seek_ms_);
    return result;
}
} // namespace bacuri::vms::client
