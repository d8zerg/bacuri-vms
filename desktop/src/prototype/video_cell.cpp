#include "prototype/video_cell.hpp"

#include "prototype/measure.hpp"

#include <QQuickItem>
#include <QQuickWindow>
#include <QRunnable>
#include <QUrl>

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

QString description(const CellOptions& options)
{
    const auto& codec = options.codec;
    const auto decoder =
        options.hardware ? QStringLiteral("nv%1dec").arg(codec) : QStringLiteral("avdec_%1").arg(codec);
    QString source;
    if (options.source.startsWith(QStringLiteral("rtsp://"))) {
        source = QStringLiteral("rtspsrc location=\"%1\" protocols=tcp latency=0%2 ! rtp%3depay ! %3parse")
                     .arg(options.source, camera_credentials(), codec);
    } else {
        source = QStringLiteral("filesrc location=\"%1\" ! qtdemux ! %2parse").arg(options.source, codec);
    }
    return QStringLiteral("%1 ! %2 name=decoder ! glupload ! glcolorconvert ! qml6glsink name=sink sync=true")
        .arg(source, decoder);
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
    if (pipeline_ != nullptr) {
        gst_element_set_state(pipeline_, GST_STATE_NULL);
        gst_object_unref(pipeline_);
    }
    if (sink_ != nullptr)
        gst_object_unref(sink_);
}

void VideoCell::create()
{
    GError* failure{};
    pipeline_ = gst_parse_launch(description(options_).toUtf8().constData(), &failure);
    if (failure != nullptr) {
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
}

void VideoCell::attach(QQuickItem* item)
{
    if (sink_ != nullptr)
        g_object_set(sink_, "widget", item, nullptr);
}

void VideoCell::start(QQuickWindow* window)
{
    if (pipeline_ == nullptr)
        return;
    {
        std::scoped_lock lock{mutex_};
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
}

void VideoCell::on_frame(std::int64_t pts_ns)
{
    std::scoped_lock lock{mutex_};
    ++frames_;
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
}

QString VideoCell::error() const
{
    std::scoped_lock lock{mutex_};
    return error_;
}

QJsonObject VideoCell::report() const
{
    QString bus_error;
    if (pipeline_ != nullptr) {
        auto* bus = gst_element_get_bus(pipeline_);
        while (auto* message = gst_bus_pop_filtered(bus, GST_MESSAGE_ERROR)) {
            GError* failure{};
            gst_message_parse_error(message, &failure, nullptr);
            bus_error = QString::fromUtf8(failure->message);
            g_error_free(failure);
            gst_message_unref(message);
        }
        gst_object_unref(bus);
    }
    std::scoped_lock lock{mutex_};
    const auto path = memory_path(decoder_features_.toStdString());
    const auto index = [this](std::int64_t pts) {
        return static_cast<qint64>(frame_index(pts, options_.rate_numerator, options_.rate_denominator));
    };
    QJsonObject result{
        {"cell", index_},
        {"decoder", options_.hardware ? QStringLiteral("nv%1dec").arg(options_.codec)
                                      : QStringLiteral("avdec_%1").arg(options_.codec)},
        {"decoder_output",
         decoder_features_.isEmpty() ? QStringLiteral("memory:SystemMemory") : decoder_features_},
        {"system_memory_copies_per_frame", system_memory_copies(path)},
        {"frames", static_cast<qint64>(frames_)},
        {"error", error_.isEmpty() ? bus_error : error_},
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
    return result;
}
} // namespace bacuri::desktop
