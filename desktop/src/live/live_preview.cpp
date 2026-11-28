#include "live/live_preview.hpp"
#include "media/graphics_probe.hpp"
#include <algorithm>
#include <QJsonDocument>
#include <gst/gst.h>
#include <QQuickWindow>
#include <gst/sdp/sdp.h>
#define GST_USE_UNSTABLE_API
#include <gst/webrtc/webrtc.h>

namespace bacuri::vms::client
{
namespace
{
bool softwareOutput()
{
    static const bool software = probe_graphics().software;
    return software;
}
void offerReady(GstPromise* promise, gpointer data)
{
    auto* peer = GST_ELEMENT(data);
    const auto* reply = gst_promise_get_reply(promise);
    GstWebRTCSessionDescription* offer{};
    if (reply && gst_structure_get(reply, "offer", GST_TYPE_WEBRTC_SESSION_DESCRIPTION, &offer, nullptr)) {
        g_signal_emit_by_name(peer, "set-local-description", offer, nullptr);
        gst_webrtc_session_description_free(offer);
    }
    gst_promise_unref(promise);
}
} // namespace
LivePreview::~LivePreview()
{
    close();
}
void LivePreview::close()
{
    ++generation_;
    disconnect(rights_);
    disconnect(state_);
    poll_.stop();
    expiry_.stop();
    poll_.disconnect(this);
    expiry_.disconnect(this);
    playback_.reset();
    stream_ = nullptr;
    if (peer_) {
        gst_object_unref(peer_);
        peer_ = nullptr;
    }
    if (workspace_ && !session_.isEmpty())
        workspace_->releaseMedia(session_);
    session_.clear();
    point_.clear();
    sent_ = false;
    ready_ = false;
    emit changed();
}
void LivePreview::open(QObject* site, const QString& id)
{
    auto* workspace = qobject_cast<SiteAccess*>(site);
    if (id == point_ && workspace == workspace_)
        return;
    start(workspace, id, DecoderChoice::automatic);
}
void LivePreview::start(SiteAccess* workspace, const QString& id, DecoderChoice decoder)
{
    close();
    if (!workspace || !workspace->sessionActions() || !workspace->ready() || id.isEmpty() || !surface_ ||
        !window())
        return;
    decoder_ = decoder;
    const auto point = workspace->point(id);
    if (!point.value("can_preview").toBool() || point.value("connection") != "online")
        return;
    workspace_ = workspace;
    point_ = id;
    revision_ = point.value("access_revision");
    auto verify = [this] {
        const auto row = workspace_->point(point_);
        if (!workspace_->ready() || !row.value("can_preview").toBool() ||
            row.value("access_revision") != revision_ || row.value("connection") != "online")
            close();
    };
    rights_ = connect(workspace, &SiteAccess::pointsChanged, this, verify);
    state_ = connect(workspace, &SiteAccess::accessChanged, this, verify);
    const bool software = softwareOutput();
    const bool main = preferMain_ && !software;
    QJsonObject request{
        {"mode", "live"}, {"quality", main ? "main" : "auto"}, {"hevc", true}, {"accept_masked", true}};
    if (!main)
        request["cell_width"] = std::clamp(qRound(width()), 320, software ? 640 : 16384);
    auto* reply = workspace->pointRequest(id, "media-sessions", {}, request);
    if (!reply) {
        close();
        return;
    }
    connect(reply, &QNetworkReply::finished, workspace,
            [guard = QPointer<LivePreview>(this), workspace, reply, generation = generation_] {
                reply->deleteLater();
                const auto obj = QJsonDocument::fromJson(reply->readAll()).object();
                const auto session = obj.value("id").toString();
                if (!guard || guard->generation_ != generation) {
                    if (!session.isEmpty())
                        workspace->releaseMedia(session);
                    return;
                }
                if (reply->error() != QNetworkReply::NoError || session.isEmpty()) {
                    guard->close();
                    return;
                }
                guard->session_ = session;
                const auto remaining = QDateTime::currentDateTimeUtc().msecsTo(
                    QDateTime::fromString(obj.value("expires_at").toString(), Qt::ISODateWithMs));
                if (remaining <= 0) {
                    guard->close();
                    return;
                }
                guard->expiry_.setSingleShot(true);
                connect(&guard->expiry_, &QTimer::timeout, guard, &LivePreview::close);
                guard->expiry_.start(int(std::min<qint64>(remaining, INT_MAX)));
                guard->negotiate(obj.value("video").toObject().value("codec").toString());
            });
    emit changed();
}
void LivePreview::negotiate(const QString& codec)
{
    if (codec != "h264" && codec != "h265") {
        close();
        return;
    }
    const auto software = softwareOutput();
    auto stream =
        std::make_unique<VideoStream>(0, StreamOptions{.source = {},
                                                       .codec = codec,
                                                       .decoder = software ? DecoderChoice::software : decoder_,
                                                       .max_size = software ? QSize(640, 360) : QSize{},
                                                       .max_rate = software ? 15 : 0,
                                                       .transport = StreamSource::webrtc});
    stream->create();
    peer_ = stream->webRtcPeer();
    if (!peer_) {
        close();
        return;
    }
    stream->attach(surface_, window());
    stream->onFirstFrame([this, generation = generation_] {
        QMetaObject::invokeMethod(
            this,
            [this, generation] {
                if (generation == generation_) {
                    stream_->attachFrameOwner(this);
                    ready_ = true;
                    emit changed();
                }
            },
            Qt::QueuedConnection);
    });
    playback_ = std::make_unique<StreamGroup>(window(), 0);
    stream_ = &playback_->add(std::move(stream));
    auto caps = QString("application/x-rtp,media=video,clock-rate=(int)90000,encoding-name=%1,payload=(int)96")
                    .arg(codec.toUpper());
    if (codec == "h264")
        caps += ",packetization-mode=(string)1";
    auto* offerCaps = gst_caps_from_string(caps.toUtf8().constData());
    GstWebRTCRTPTransceiver* receiver{};
    g_signal_emit_by_name(peer_, "add-transceiver", GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_RECVONLY, offerCaps,
                          &receiver);
    gst_caps_unref(offerCaps);
    if (!receiver) {
        close();
        return;
    }
    gst_object_unref(receiver);
    playback_->start([this] {
        auto* promise = gst_promise_new_with_change_func(offerReady, gst_object_ref(peer_), gst_object_unref);
        g_signal_emit_by_name(peer_, "create-offer", nullptr, promise);
    });
    startup_.start();
    connect(&poll_, &QTimer::timeout, this, &LivePreview::tick);
    poll_.start(25);
}
void LivePreview::tick()
{
    if (!stream_->error().isEmpty() || (!ready() && startup_.elapsed() > 12000)) {
        if (decoder_ != DecoderChoice::software && stream_->hardwareActive()) {
            const auto id = point_;
            start(workspace_, id, DecoderChoice::software);
        } else
            close();
        return;
    }
    if (!sent_) {
        GstWebRTCICEGatheringState state{};
        g_object_get(peer_, "ice-gathering-state", &state, nullptr);
        if (state != GST_WEBRTC_ICE_GATHERING_STATE_COMPLETE)
            return;
        GstWebRTCSessionDescription* local{};
        g_object_get(peer_, "local-description", &local, nullptr);
        if (!local)
            return;
        auto* text = gst_sdp_message_as_text(local->sdp);
        auto* reply = workspace_->offerMedia(session_, QByteArray(text));
        g_free(text);
        gst_webrtc_session_description_free(local);
        if (!reply) {
            close();
            return;
        }
        sent_ = true;
        poll_.setInterval(250);
        connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
        connect(reply, &QNetworkReply::finished, this, [this, reply, generation = generation_] {
            if (generation_ != generation)
                return;
            GstSDPMessage* answer{};
            const auto sdp = reply->readAll();
            if (reply->error() != QNetworkReply::NoError || sdp.size() > 65536 ||
                gst_sdp_message_new_from_text(sdp.constData(), &answer) != GST_SDP_OK) {
                if (answer)
                    gst_sdp_message_free(answer);
                close();
                return;
            }
            auto* remote = gst_webrtc_session_description_new(GST_WEBRTC_SDP_TYPE_ANSWER, answer);
            g_signal_emit_by_name(peer_, "set-remote-description", remote, nullptr);
            gst_webrtc_session_description_free(remote);
        });
    }
}
QJsonObject LivePreview::report() const
{
    return stream_ ? stream_->report() : QJsonObject{};
}
} // namespace bacuri::vms::client
