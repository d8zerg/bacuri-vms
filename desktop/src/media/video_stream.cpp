#include "media/video_stream.hpp"

#include "diagnostics/frame_stats.hpp"

#include <QCoreApplication>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRunnable>
#include <QThreadPool>

#include <gst/gst.h>

#include <cstdlib>
#include <map>
#include <string>
#include <string_view>
#include <utility>

namespace bacuri::vms::client
{
namespace
{
using Clock = std::chrono::steady_clock;
constexpr auto preroll_timeout = 10 * GST_SECOND;

double elapsed_ms(Clock::time_point since)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - since).count();
}

// Credentials of a camera come from the environment and never appear in reports.
QString camera_credentials()
{
    const char* user = std::getenv("BACURI_CAMERA_USER");
    const char* password = std::getenv("BACURI_CAMERA_PASSWORD");
    if (user == nullptr || password == nullptr)
        return {};
    return QStringLiteral(" user-id=\"%1\" user-pw=\"%2\"")
        .arg(QString::fromUtf8(user), QString::fromUtf8(password));
}

bool element_exists(const QString& name)
{
    auto* factory = gst_element_factory_find(name.toUtf8().constData());
    if (factory == nullptr)
        return false;
    gst_object_unref(factory);
    return true;
}

// Device contexts shared by all pipelines of the process. The first decoder that creates a CUDA or
// VA context, and the first GL display, publish it, and the following pipelines receive it instead
// of initializing the device again.
bool shared_context(std::string_view type)
{
    return type.starts_with("gst.cuda.") || type.starts_with("gst.va.") || type == "gst.gl.GLDisplay";
}

std::mutex& contexts_mutex()
{
    static std::mutex mutex;
    return mutex;
}

std::map<std::string, GstContext*>& contexts()
{
    static std::map<std::string, GstContext*> shared;
    return shared;
}

GstBusSyncReply share_context(GstBus*, GstMessage* message, gpointer)
{
    if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_NEED_CONTEXT) {
        const gchar* type{};
        if (gst_message_parse_context_type(message, &type) && shared_context(type)) {
            std::scoped_lock lock{contexts_mutex()};
            if (const auto found = contexts().find(type); found != contexts().end())
                gst_element_set_context(GST_ELEMENT(GST_MESSAGE_SRC(message)), found->second);
        }
    } else if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_HAVE_CONTEXT) {
        GstContext* context{};
        gst_message_parse_have_context(message, &context);
        const std::string type = gst_context_get_context_type(context);
        std::scoped_lock lock{contexts_mutex()};
        if (shared_context(type) && !contexts().contains(type))
            contexts().emplace(type, context);
        else
            gst_context_unref(context);
    }
    return GST_BUS_PASS;
}

bool settled(GstStateChangeReturn result)
{
    return result == GST_STATE_CHANGE_SUCCESS || result == GST_STATE_CHANGE_NO_PREROLL;
}
} // namespace

QString hardware_decoder(const QString& codec)
{
    for (const auto& name : {QStringLiteral("nv%1dec").arg(codec), QStringLiteral("va%1dec").arg(codec)}) {
        if (element_exists(name))
            return name;
    }
    return {};
}

struct VideoStream::Probe {
    static GstPadProbeReturn decoded(GstPad* pad, GstPadProbeInfo* info, gpointer data)
    {
        static_cast<VideoStream*>(data)->native_.receive(pad, gst_pad_probe_info_get_buffer(info));
        return GST_PAD_PROBE_OK;
    }
    static GstPadProbeReturn caps(GstPad*, GstPadProbeInfo* info, gpointer data)
    {
        auto* event = gst_pad_probe_info_get_event(info);
        if (GST_EVENT_TYPE(event) == GST_EVENT_CAPS) {
            GstCaps* caps{};
            gst_event_parse_caps(event, &caps);
            gchar* features = gst_caps_features_to_string(gst_caps_get_features(caps, 0));
            static_cast<VideoStream*>(data)->on_decoded_caps(QString::fromUtf8(features));
            g_free(features);
        }
        return GST_PAD_PROBE_OK;
    }

    // A frame that reached the sink before the flush of a seek is not the frame after the seek.
    static GstPadProbeReturn flush(GstPad*, GstPadProbeInfo* info, gpointer data)
    {
        if (GST_EVENT_TYPE(gst_pad_probe_info_get_event(info)) == GST_EVENT_FLUSH_STOP)
            static_cast<VideoStream*>(data)->on_flushed();
        return GST_PAD_PROBE_OK;
    }

    static GstPadProbeReturn buffer(GstPad*, GstPadProbeInfo* info, gpointer data)
    {
        const auto* frame = gst_pad_probe_info_get_buffer(info);
        if (frame != nullptr && GST_BUFFER_PTS_IS_VALID(frame))
            static_cast<VideoStream*>(data)->on_frame(static_cast<std::int64_t>(GST_BUFFER_PTS(frame)));
        return GST_PAD_PROBE_OK;
    }
};

VideoStream::VideoStream(int index, StreamOptions options) : index_{index}, options_{std::move(options)} {}

VideoStream::~VideoStream()
{
    release();
}

bool VideoStream::live() const
{
    return webRtc() || options_.source.startsWith(QStringLiteral("rtsp://"));
}

QString VideoStream::decoder_name(bool hardware) const
{
    return hardware ? hardware_name_ : QStringLiteral("avdec_%1").arg(options_.codec);
}

void VideoStream::release()
{
    if (bus_watch_ != 0) {
        g_source_remove(bus_watch_);
        bus_watch_ = 0;
    }
    if (pipeline_ != nullptr) {
        gst_element_set_state(pipeline_, GST_STATE_NULL);
        native_.clear();
        gst_object_unref(pipeline_);
        pipeline_ = nullptr;
    }
    if (sink_ != nullptr) {
        gst_object_unref(sink_);
        sink_ = nullptr;
    }
}

bool VideoStream::registerVideoItem()
{
    gst_init(nullptr, nullptr);
    auto* sink = gst_element_factory_make("qml6glsink", nullptr);
    if (!sink)
        return false;
    gst_object_unref(sink);
    return true;
}

GstElement* VideoStream::webRtcPeer() const
{
    return webRtc() && pipeline_ ? gst_bin_get_by_name(GST_BIN(pipeline_), "peer") : nullptr;
}

void VideoStream::onFirstFrame(std::function<void()> handler)
{
    std::scoped_lock lock{mutex_};
    firstFrame_ = std::move(handler);
}

void VideoStream::create()
{
    hardware_name_ = hardware_decoder(options_.codec);
    const bool available = !hardware_name_.isEmpty();
    const bool hardware = options_.decoder != DecoderChoice::software && available;
    if (options_.decoder != DecoderChoice::software && !available)
        fallback_reason_ = QStringLiteral("hardware decoder unavailable at start");
    {
        std::scoped_lock lock{mutex_};
        created_ = Clock::now();
    }
    build(hardware);
}

void VideoStream::build(bool hardware)
{
    hardware_ = hardware;
    QString source;
    if (webRtc()) {
        source =
            QStringLiteral("webrtcbin name=peer bundle-policy=max-bundle peer. ! queue ! rtp%1depay ! %1parse")
                .arg(options_.codec);
    } else if (live()) {
        source = QStringLiteral("rtspsrc location=\"%1\" protocols=tcp latency=0%2 ! rtp%3depay ! %3parse")
                     .arg(options_.source, camera_credentials(), options_.codec);
    } else {
        source =
            QStringLiteral("filesrc location=\"%1\" ! qtdemux ! %2parse").arg(options_.source, options_.codec);
    }
    // A software decoded picture is reduced to the limits before it is uploaded for drawing.
    QString limits;
    if (!hardware && options_.max_rate > 0)
        limits += QStringLiteral(" ! videorate drop-only=true max-rate=%1").arg(options_.max_rate);
    if (!hardware && options_.max_size.isValid())
        limits += QStringLiteral(" ! videoscale ! video/x-raw,width=[1,%1],height=[1,%2]")
                      .arg(options_.max_size.width())
                      .arg(options_.max_size.height());
    const auto description =
        QStringLiteral("%1 ! %2 name=decoder%3 ! glupload ! glcolorconvert ! qml6glsink name=sink sync=true")
            .arg(source, decoder_name(hardware), limits);
    GError* failure{};
    pipeline_ = gst_parse_launch(description.toUtf8().constData(), &failure);
    if (failure != nullptr) {
        std::scoped_lock lock{mutex_};
        error_ = QString::fromUtf8(failure->message);
        g_error_free(failure);
        if (pipeline_)
            gst_object_unref(std::exchange(pipeline_, nullptr));
        return;
    }
    sink_ = gst_bin_get_by_name(GST_BIN(pipeline_), "sink");
    if (auto* decoder = gst_bin_get_by_name(GST_BIN(pipeline_), "decoder")) {
        auto* pad = gst_element_get_static_pad(decoder, "src");
        gst_pad_add_probe(pad, GST_PAD_PROBE_TYPE_BUFFER, &Probe::decoded, this, nullptr);
        gst_pad_add_probe(pad, GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM, &Probe::caps, this, nullptr);
        gst_object_unref(pad);
        gst_object_unref(decoder);
    }
    auto* pad = gst_element_get_static_pad(sink_, "sink");
    gst_pad_add_probe(pad, GST_PAD_PROBE_TYPE_BUFFER, &Probe::buffer, this, nullptr);
    gst_pad_add_probe(pad, GST_PAD_PROBE_TYPE_EVENT_FLUSH, &Probe::flush, this, nullptr);
    gst_object_unref(pad);
    auto* bus = gst_element_get_bus(pipeline_);
    gst_bus_set_sync_handler(bus, &share_context, nullptr, nullptr);
    bus_watch_ = gst_bus_add_watch(bus, reinterpret_cast<GstBusFunc>(&VideoStream::on_bus), this);
    gst_object_unref(bus);
    if (item_ != nullptr)
        g_object_set(sink_, "widget", item_, nullptr);
}

void VideoStream::attach(QQuickItem* item, QQuickWindow* window)
{
    item_ = item;
    native_.attach(item);
    window_ = window;
    if (sink_ != nullptr)
        g_object_set(sink_, "widget", item, nullptr);
}

void VideoStream::attachFrameOwner(QQuickItem* item)
{
    native_.attach(item);
}

void VideoStream::on_resume(Resume resume)
{
    resume_ = std::move(resume);
}

bool VideoStream::preroll()
{
    if (pipeline_ == nullptr)
        return false;
    if (gst_element_set_state(pipeline_, GST_STATE_PAUSED) == GST_STATE_CHANGE_FAILURE)
        return false;
    return settled(gst_element_get_state(pipeline_, nullptr, nullptr, preroll_timeout));
}

bool VideoStream::seek_paused(std::int64_t frame)
{
    if (pipeline_ == nullptr || live())
        return false;
    {
        std::scoped_lock lock{mutex_};
        seek_target_ = frame;
        seek_requested_ = Clock::now();
        seek_pts_.reset();
        seek_ms_.reset();
    }
    const auto target = frame * 1'000'000'000LL * options_.rate_denominator / options_.rate_numerator;
    gst_element_set_state(pipeline_, GST_STATE_PAUSED);
    gst_element_seek_simple(pipeline_, GST_FORMAT_TIME,
                            static_cast<GstSeekFlags>(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE), target);
    return settled(gst_element_get_state(pipeline_, nullptr, nullptr, preroll_timeout));
}

void VideoStream::play_at(GstClock* clock, std::uint64_t base)
{
    if (pipeline_ == nullptr)
        return;
    gst_pipeline_use_clock(GST_PIPELINE(pipeline_), clock);
    // A live source stamps its frames by their arrival, so it starts on its own base time and plays as
    // the frames arrive. A recorded source starts on the base time of the grid.
    if (!live()) {
        gst_element_set_start_time(pipeline_, GST_CLOCK_TIME_NONE);
        gst_element_set_base_time(pipeline_, base);
    }
    if (gst_element_set_state(pipeline_, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
        std::scoped_lock lock{mutex_};
        error_ = QStringLiteral("Could not start video pipeline");
    }
}

void VideoStream::inject_decoder_fault()
{
    if (pipeline_ == nullptr || !hardware_)
        return;
    auto* decoder = gst_bin_get_by_name(GST_BIN(pipeline_), "decoder");
    GError* failure =
        g_error_new_literal(GST_RESOURCE_ERROR, GST_RESOURCE_ERROR_FAILED, "Injected decoder failure");
    gst_element_post_message(decoder, gst_message_new_error(GST_OBJECT(decoder), failure, "injected"));
    g_error_free(failure);
    gst_object_unref(decoder);
}

int VideoStream::on_bus(GstBus*, GstMessage* message, void* data)
{
    auto* cell = static_cast<VideoStream*>(data);
    if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_ERROR) {
        GError* failure{};
        gst_message_parse_error(message, &failure, nullptr);
        const auto reason = QString::fromUtf8(failure->message);
        g_error_free(failure);
        if (cell->hardware_ && !cell->webRtc()) {
            // The watch belongs to the pipeline that fall_back replaces.
            cell->bus_watch_ = 0;
            cell->fall_back(reason);
            return G_SOURCE_REMOVE;
        }
        std::scoped_lock lock{cell->mutex_};
        cell->error_ = reason;
    }
    return G_SOURCE_CONTINUE;
}

void VideoStream::fall_back(const QString& reason)
{
    std::optional<std::int64_t> resume;
    {
        std::scoped_lock lock{mutex_};
        fallback_reason_ = reason;
        fallback_started_ = Clock::now();
        if (created_)
            fallback_at_ms_ = elapsed_ms(*created_);
        fallback_from_pts_ = last_pts_;
        resume = last_pts_;
        resume_waits_for_flush_ = !live() && last_pts_.has_value();
    }
    // The failed pipeline leaves the item at once and stops on a worker thread. The new pipeline is
    // created and attached here, its blocking start and seek run on the worker thread as well.
    if (bus_watch_ != 0) {
        g_source_remove(bus_watch_);
        bus_watch_ = 0;
    }
    auto* failed = std::exchange(pipeline_, nullptr);
    auto* failed_sink = std::exchange(sink_, nullptr);
    if (failed_sink != nullptr)
        g_object_set(failed_sink, "widget", nullptr, nullptr);
    build(false);
    const bool recording = !live() && resume.has_value();
    const auto position = resume.value_or(0);
    QThreadPool::globalInstance()->start([this, failed, failed_sink, recording, position] {
        if (failed != nullptr) {
            gst_element_set_state(failed, GST_STATE_NULL);
            gst_object_unref(failed);
        }
        if (failed_sink != nullptr)
            gst_object_unref(failed_sink);
        if (pipeline_ == nullptr || !preroll())
            return;
        // A recording continues from the last shown frame. Live video continues from the next frame.
        if (recording) {
            gst_element_seek_simple(pipeline_, GST_FORMAT_TIME,
                                    static_cast<GstSeekFlags>(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE),
                                    position);
            gst_element_get_state(pipeline_, nullptr, nullptr, preroll_timeout);
        }
        if (resume_) {
            resume_(*this, recording ? position : 0);
            return;
        }
        auto* pipeline = static_cast<GstElement*>(gst_object_ref(pipeline_));
        QMetaObject::invokeMethod(
            QCoreApplication::instance(),
            [window = window_, pipeline] {
                if (window) {
                    window->scheduleRenderJob(
                        QRunnable::create([pipeline] { gst_element_set_state(pipeline, GST_STATE_PLAYING); }),
                        QQuickWindow::BeforeSynchronizingStage);
                }
                QThreadPool::globalInstance()->start([pipeline] { gst_object_unref(pipeline); });
            },
            Qt::QueuedConnection);
    });
}

void VideoStream::on_decoded_caps(const QString& features)
{
    std::scoped_lock lock{mutex_};
    decoder_features_ = features;
}

void VideoStream::on_flushed()
{
    std::scoped_lock lock{mutex_};
    if (seek_requested_ && !seek_pts_)
        awaiting_seek_ = true;
    resume_waits_for_flush_ = false;
}

void VideoStream::on_frame(std::int64_t pts_ns)
{
    std::scoped_lock lock{mutex_};
    ++frames_;
    last_pts_ = pts_ns;
    if (!first_pts_) {
        first_pts_ = pts_ns;
        if (created_)
            first_frame_ms_ = elapsed_ms(*created_);
        if (auto notify = std::exchange(firstFrame_, {}))
            notify();
    }
    if (awaiting_seek_.exchange(false)) {
        seek_pts_ = pts_ns;
        if (seek_requested_)
            seek_ms_ = elapsed_ms(*seek_requested_);
    }
    if (fallback_started_ && !resumed_pts_ && !resume_waits_for_flush_) {
        resumed_pts_ = pts_ns;
        resumed_after_ms_ = elapsed_ms(*fallback_started_);
    }
}

std::optional<std::int64_t> VideoStream::last_pts() const
{
    std::scoped_lock lock{mutex_};
    return last_pts_;
}

QString VideoStream::error() const
{
    std::scoped_lock lock{mutex_};
    return error_;
}

QJsonObject VideoStream::report() const
{
    std::scoped_lock lock{mutex_};
    const auto path = memory_path(decoder_features_.toStdString());
    const auto index = [this](std::int64_t pts) {
        return static_cast<qint64>(frame_index(pts, options_.rate_numerator, options_.rate_denominator));
    };
    QJsonObject result{
        {"cell", index_},
        {"decoder", decoder_name(hardware_)},
        {"hardware_decoder", hardware_name_},
        {"hardware_available", !hardware_name_.isEmpty()},
        {"decoder_output",
         decoder_features_.isEmpty() ? QStringLiteral("memory:SystemMemory") : decoder_features_},
        {"system_memory_copies_per_frame", system_memory_copies(path)},
        {"frames", static_cast<qint64>(frames_)},
        {"error", error_},
    };
    if (!hardware_ && (options_.max_size.isValid() || options_.max_rate > 0)) {
        result.insert("software_limits", QJsonObject{{"max_width", options_.max_size.width()},
                                                     {"max_height", options_.max_size.height()},
                                                     {"max_rate", options_.max_rate}});
    }
    if (first_pts_)
        result.insert("first_frame", index(*first_pts_));
    if (first_frame_ms_)
        result.insert("first_frame_ms", *first_frame_ms_);
    if (seek_target_)
        result.insert("seek_target", static_cast<qint64>(*seek_target_));
    if (seek_pts_)
        result.insert("seek_frame", index(*seek_pts_));
    if (seek_ms_)
        result.insert("seek_ms", *seek_ms_);
    if (!fallback_reason_.isEmpty()) {
        QJsonObject fallback{{"reason", fallback_reason_}};
        if (fallback_at_ms_)
            fallback.insert("at_ms", *fallback_at_ms_);
        if (fallback_from_pts_)
            fallback.insert("last_frame_before", index(*fallback_from_pts_));
        if (resumed_pts_)
            fallback.insert("first_frame_after", index(*resumed_pts_));
        if (resumed_after_ms_)
            fallback.insert("resumed_after_ms", *resumed_after_ms_);
        result.insert("fallback", fallback);
    }
    return result;
}
} // namespace bacuri::vms::client
