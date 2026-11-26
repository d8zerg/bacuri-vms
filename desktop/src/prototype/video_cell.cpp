#include "prototype/video_cell.hpp"

#include "prototype/measure.hpp"

#include <QQuickItem>
#include <QQuickWindow>
#include <QRunnable>

#include <gst/gst.h>

#include <cstdlib>

namespace bacuri::desktop
{
namespace
{
using Clock = std::chrono::steady_clock;

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

bool live(const CellOptions& options)
{
    return options.source.startsWith(QStringLiteral("rtsp://"));
}

bool element_exists(const QString& name)
{
    auto* factory = gst_element_factory_find(name.toUtf8().constData());
    if (factory == nullptr)
        return false;
    gst_object_unref(factory);
    return true;
}

class SetPlaying : public QRunnable
{
  public:
    explicit SetPlaying(GstElement* pipeline) : pipeline_{static_cast<GstElement*>(gst_object_ref(pipeline))} {}
    SetPlaying(const SetPlaying&) = delete;
    SetPlaying& operator=(const SetPlaying&) = delete;
    ~SetPlaying() override
    {
        gst_object_unref(pipeline_);
    }
    void run() override
    {
        gst_element_set_state(pipeline_, GST_STATE_PLAYING);
    }

  private:
    GstElement* pipeline_;
};
} // namespace

struct VideoCell::Probe {
    static GstPadProbeReturn caps(GstPad*, GstPadProbeInfo* info, gpointer data)
    {
        auto* event = gst_pad_probe_info_get_event(info);
        if (GST_EVENT_TYPE(event) == GST_EVENT_CAPS) {
            GstCaps* caps{};
            gst_event_parse_caps(event, &caps);
            gchar* features = gst_caps_features_to_string(gst_caps_get_features(caps, 0));
            static_cast<VideoCell*>(data)->on_decoded_caps(QString::fromUtf8(features));
            g_free(features);
        }
        return GST_PAD_PROBE_OK;
    }

    // A frame that reached the sink before the flush of a seek is not the frame after the seek.
    static GstPadProbeReturn flush(GstPad*, GstPadProbeInfo* info, gpointer data)
    {
        if (GST_EVENT_TYPE(gst_pad_probe_info_get_event(info)) == GST_EVENT_FLUSH_STOP)
            static_cast<VideoCell*>(data)->on_flushed();
        return GST_PAD_PROBE_OK;
    }

    static GstPadProbeReturn buffer(GstPad*, GstPadProbeInfo* info, gpointer data)
    {
        const auto* frame = gst_pad_probe_info_get_buffer(info);
        if (frame != nullptr && GST_BUFFER_PTS_IS_VALID(frame))
            static_cast<VideoCell*>(data)->on_frame(static_cast<std::int64_t>(GST_BUFFER_PTS(frame)));
        return GST_PAD_PROBE_OK;
    }
};

VideoCell::VideoCell(int index, CellOptions options) : index_{index}, options_{std::move(options)} {}

VideoCell::~VideoCell()
{
    release();
}

QString VideoCell::decoder_name(bool hardware) const
{
    return hardware ? QStringLiteral("nv%1dec").arg(options_.codec)
                    : QStringLiteral("avdec_%1").arg(options_.codec);
}

void VideoCell::release()
{
    if (bus_watch_ != 0) {
        g_source_remove(bus_watch_);
        bus_watch_ = 0;
    }
    if (pipeline_ != nullptr) {
        gst_element_set_state(pipeline_, GST_STATE_NULL);
        gst_object_unref(pipeline_);
        pipeline_ = nullptr;
    }
    if (sink_ != nullptr) {
        gst_object_unref(sink_);
        sink_ = nullptr;
    }
}

void VideoCell::create()
{
    hardware_available_ = element_exists(decoder_name(true));
    const bool hardware = options_.decoder == DecoderChoice::hardware ||
                          (options_.decoder == DecoderChoice::automatic && hardware_available_);
    if (options_.decoder == DecoderChoice::automatic && !hardware_available_)
        fallback_reason_ = QStringLiteral("hardware decoder unavailable at start");
    build(hardware);
}

void VideoCell::build(bool hardware)
{
    hardware_ = hardware;
    QString source;
    if (live(options_)) {
        source = QStringLiteral("rtspsrc location=\"%1\" protocols=tcp latency=0%2 ! rtp%3depay ! %3parse")
                     .arg(options_.source, camera_credentials(), options_.codec);
    } else {
        source =
            QStringLiteral("filesrc location=\"%1\" ! qtdemux ! %2parse").arg(options_.source, options_.codec);
    }
    const auto description =
        QStringLiteral("%1 ! %2 name=decoder ! glupload ! glcolorconvert ! qml6glsink name=sink sync=true")
            .arg(source, decoder_name(hardware));
    GError* failure{};
    pipeline_ = gst_parse_launch(description.toUtf8().constData(), &failure);
    if (failure != nullptr) {
        std::scoped_lock lock{mutex_};
        error_ = QString::fromUtf8(failure->message);
        g_error_free(failure);
        return;
    }
    sink_ = gst_bin_get_by_name(GST_BIN(pipeline_), "sink");
    if (auto* decoder = gst_bin_get_by_name(GST_BIN(pipeline_), "decoder")) {
        auto* pad = gst_element_get_static_pad(decoder, "src");
        gst_pad_add_probe(pad, GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM, &Probe::caps, this, nullptr);
        gst_object_unref(pad);
        gst_object_unref(decoder);
    }
    auto* pad = gst_element_get_static_pad(sink_, "sink");
    gst_pad_add_probe(pad, GST_PAD_PROBE_TYPE_BUFFER, &Probe::buffer, this, nullptr);
    gst_pad_add_probe(pad, GST_PAD_PROBE_TYPE_EVENT_FLUSH, &Probe::flush, this, nullptr);
    gst_object_unref(pad);
    auto* bus = gst_element_get_bus(pipeline_);
    bus_watch_ = gst_bus_add_watch(bus, reinterpret_cast<GstBusFunc>(&VideoCell::on_bus), this);
    gst_object_unref(bus);
    if (item_ != nullptr)
        g_object_set(sink_, "widget", item_, nullptr);
}

void VideoCell::attach(QQuickItem* item)
{
    item_ = item;
    if (sink_ != nullptr)
        g_object_set(sink_, "widget", item, nullptr);
}

void VideoCell::start(QQuickWindow* window)
{
    window_ = window;
    if (pipeline_ == nullptr)
        return;
    {
        std::scoped_lock lock{mutex_};
        if (!started_)
            started_ = Clock::now();
    }
    window->scheduleRenderJob(new SetPlaying(pipeline_), QQuickWindow::BeforeSynchronizingStage);
}

void VideoCell::seek_to_frame(std::int64_t frame)
{
    if (pipeline_ == nullptr)
        return;
    {
        std::scoped_lock lock{mutex_};
        seek_target_ = frame;
        seek_requested_ = Clock::now();
        seek_pts_.reset();
        seek_ms_.reset();
    }
    const auto target = frame * 1'000'000'000LL * options_.rate_denominator / options_.rate_numerator;
    gst_element_seek_simple(pipeline_, GST_FORMAT_TIME,
                            static_cast<GstSeekFlags>(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE), target);
}

void VideoCell::inject_decoder_fault()
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

int VideoCell::on_bus(GstBus*, GstMessage* message, void* data)
{
    auto* cell = static_cast<VideoCell*>(data);
    if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_ERROR) {
        GError* failure{};
        gst_message_parse_error(message, &failure, nullptr);
        const auto reason = QString::fromUtf8(failure->message);
        g_error_free(failure);
        if (cell->hardware_) {
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

void VideoCell::fall_back(const QString& reason)
{
    std::optional<std::int64_t> resume;
    {
        std::scoped_lock lock{mutex_};
        fallback_reason_ = reason;
        fallback_started_ = Clock::now();
        if (started_)
            fallback_at_ms_ = elapsed_ms(*started_);
        fallback_from_pts_ = last_pts_;
        resume = last_pts_;
        resume_waits_for_flush_ = !live(options_) && last_pts_.has_value();
    }
    release();
    build(false);
    if (pipeline_ == nullptr)
        return;
    // A recording continues from the last shown frame. Live video continues from the next frame.
    if (!live(options_) && resume) {
        gst_element_set_state(pipeline_, GST_STATE_PAUSED);
        gst_element_get_state(pipeline_, nullptr, nullptr, 5 * GST_SECOND);
        gst_element_seek_simple(pipeline_, GST_FORMAT_TIME,
                                static_cast<GstSeekFlags>(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE), *resume);
    }
    if (window_ != nullptr)
        window_->scheduleRenderJob(new SetPlaying(pipeline_), QQuickWindow::BeforeSynchronizingStage);
}

void VideoCell::on_decoded_caps(const QString& features)
{
    std::scoped_lock lock{mutex_};
    decoder_features_ = features;
}

void VideoCell::on_flushed()
{
    std::scoped_lock lock{mutex_};
    if (seek_requested_ && !seek_pts_)
        awaiting_seek_ = true;
    resume_waits_for_flush_ = false;
}

void VideoCell::on_frame(std::int64_t pts_ns)
{
    std::scoped_lock lock{mutex_};
    ++frames_;
    last_pts_ = pts_ns;
    if (!first_pts_) {
        first_pts_ = pts_ns;
        if (started_)
            first_frame_ms_ = elapsed_ms(*started_);
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

QString VideoCell::error() const
{
    std::scoped_lock lock{mutex_};
    return error_;
}

QJsonObject VideoCell::report() const
{
    std::scoped_lock lock{mutex_};
    const auto path = memory_path(decoder_features_.toStdString());
    const auto index = [this](std::int64_t pts) {
        return static_cast<qint64>(frame_index(pts, options_.rate_numerator, options_.rate_denominator));
    };
    QJsonObject result{
        {"cell", index_},
        {"decoder", decoder_name(hardware_)},
        {"hardware_available", hardware_available_},
        {"decoder_output",
         decoder_features_.isEmpty() ? QStringLiteral("memory:SystemMemory") : decoder_features_},
        {"system_memory_copies_per_frame", system_memory_copies(path)},
        {"frames", static_cast<qint64>(frames_)},
        {"error", error_},
    };
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
} // namespace bacuri::desktop
