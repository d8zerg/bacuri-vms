#include "services/recorder/detector.hpp"
#include "services/recorder/sources.hpp"

#include <nlohmann/json.hpp>

#include <cstdio>

namespace bacuri::vms::ingest
{
namespace
{
using Json = nlohmann::json;

template <typename T> Json nullable(const std::optional<T>& value)
{
    return value ? Json(*value) : Json(nullptr);
}

Json micros(std::optional<domain::UtcTime> value)
{
    return value ? Json(value->time_since_epoch().count()) : Json(nullptr);
}

Json metrics(const StreamStats& stats)
{
    Json queues = Json::array();
    for (const auto& queue : stats.consumers) {
        queues.push_back({{"depth", queue.depth},
                          {"bytes", queue.bytes},
                          {"peak_depth", queue.peak_depth},
                          {"peak_bytes", queue.peak_bytes},
                          {"dropped", queue.dropped},
                          {"loss_begin_us", micros(queue.loss_begin)},
                          {"recovered_at_us", micros(queue.recovered_at)}});
    }
    Json result{{"state", stats.state},
                {"frames", stats.frames},
                {"bytes", stats.bytes},
                {"keyframes", stats.keyframes},
                {"codec", stats.frames ? Json(stats.codec == VideoCodec::h265 ? "h265" : "h264") : Json(nullptr)},
                {"width", stats.width},
                {"height", stats.height},
                {"reconnects", stats.reconnects},
                {"gaps", stats.gaps},
                {"queues", std::move(queues)},
                {"gap_begin_us", micros(stats.gap_begin)},
                {"gap_end_us", micros(stats.gap_end)},
                {"rtp_timestamp", nullable(stats.rtp_timestamp)},
                {"ntp_ns", nullable(stats.ntp_ns)},
                {"pts_ns", nullable(stats.pts_ns)}};
    if (stats.time) {
        const auto& time = *stats.time;
        result["received_us"] = micros(time.received_at());
        result["utc_us"] = micros(time.utc());
        result["basis"] = time.basis() == domain::TimeBasis::source_clock ? "source_clock" : "reception";
        result["uncertainty_us"] =
            nullable(time.uncertainty().transform([](auto duration) { return duration.count(); }));
        result["clock_epoch"] =
            nullable(time.clock_epoch().transform([](auto epoch) { return epoch.to_string(); }));
    }
    return result;
}
} // namespace

std::shared_ptr<FrameQueue> Sources::preview(const std::string& point, Profile profile)
{
    for (auto& [key, entry] : streams_) {
        if (key.profile == profile && key.point == point) {
            return entry.stream->subscribe();
        }
    }
    return {};
}

std::shared_ptr<CameraAudio> Sources::audio(const std::string& point, const std::string& device)
{
    for (auto& [key, entry] : streams_)
        if (key.point == point && key.device == device && key.profile == Profile::main)
            return entry.audio;
    return {};
}

v1::VideoProfiles Sources::video_profiles(const std::string& point) const
{
    v1::VideoProfiles out;
    for (const auto& [key, entry] : streams_) {
        if (key.point != point)
            continue;
        const auto stats = entry.stream->stats();
        if (stats.state != "receiving")
            continue;
        auto* p = out.add_profiles();
        p->set_main(key.profile == Profile::main);
        p->set_codec(stats.codec == VideoCodec::h265 ? "h265" : "h264");
        p->set_width(static_cast<std::uint32_t>(std::max(0, stats.width)));
        p->set_height(static_cast<std::uint32_t>(std::max(0, stats.height)));
        p->set_bitrate_kbps(stats.bitrate_kbps ? stats.bitrate_kbps : key.profile == Profile::main ? 4096 : 768);
    }
    return out;
}

v1::CameraIssues Sources::issues(const std::string& device) const
{
    v1::CameraIssues result;
    for (const auto& [key, entry] : streams_) {
        if (key.device != device)
            continue;
        const auto state = entry.stream->stats().state;
        auto issue = state == "certificate_error"      ? v1::STREAM_ISSUE_TLS
                     : state == "incompatible_profile" ? v1::STREAM_ISSUE_INCOMPATIBLE
                                                       : v1::STREAM_ISSUE_UNSPECIFIED;
        if (key.profile == Profile::main)
            result.set_main(issue);
        else
            result.set_secondary(issue);
    }
    return result;
}

v1::PointStates Sources::point_states()
{
    using namespace std::chrono_literals;
    // A receiving stream without frames for this long is reported offline before its RTSP timeout.
    constexpr auto stalled_after = 5s;
    const auto now = std::chrono::time_point_cast<domain::Microseconds>(std::chrono::system_clock::now());
    v1::PointStates result;
    for (const auto& [key, entry] : streams_) {
        if (key.profile != Profile::main)
            continue;
        const auto stats = entry.stream->stats();
        const auto rule = volume_.recording_rule(key.point);
        auto& point = *result.add_points();
        point.set_point_id(key.point);
        point.set_binding_revision(key.binding);
        if (stats.state == "receiving" && stats.time && now - stats.time->received_at() <= stalled_after)
            point.set_source(v1::SOURCE_STATE_RECEIVING);
        else if (stats.state == "connecting")
            point.set_source(v1::SOURCE_STATE_CONNECTING);
        else
            point.set_source(v1::SOURCE_STATE_OFFLINE);
        point.set_selection(!key.binding ? v1::RECORDING_SELECTION_UNBOUND
                            : !rule      ? v1::RECORDING_SELECTION_AWAITING_POLICY
                            : rule->selects(now.time_since_epoch().count(), key.binding)
                                ? v1::RECORDING_SELECTION_SELECTED
                            : rule->event             ? v1::RECORDING_SELECTION_WAITING_EVENT
                            : !rule->schedule.empty() ? v1::RECORDING_SELECTION_OUTSIDE_SCHEDULE
                            : rule->manual            ? v1::RECORDING_SELECTION_WAITING_MANUAL
                                                      : v1::RECORDING_SELECTION_DISABLED);
        point.set_publication_lag_us(archive::publication_lag(rule.get()).count());
    }
    return result;
}

archive::Json Sources::recording_states()
{
    auto result = Json::object();
    const auto states = point_states();
    for (const auto& point : states.points()) {
        const char* state = "disabled";
        if (point.selection() == v1::RECORDING_SELECTION_UNBOUND)
            state = "unbound";
        else if (point.source() != v1::SOURCE_STATE_RECEIVING)
            state = "source_offline";
        else {
            switch (point.selection()) {
            case v1::RECORDING_SELECTION_AWAITING_POLICY:
                state = "awaiting_policy";
                break;
            case v1::RECORDING_SELECTION_SELECTED:
                state = "selected";
                break;
            case v1::RECORDING_SELECTION_WAITING_EVENT:
                state = "waiting_event";
                break;
            case v1::RECORDING_SELECTION_OUTSIDE_SCHEDULE:
                state = "outside_schedule";
                break;
            case v1::RECORDING_SELECTION_WAITING_MANUAL:
                state = "waiting_manual";
                break;
            default:
                break;
            }
        }
        result[point.point_id()] = state;
    }
    return result;
}
Sources::~Sources()
{
    for (auto& [key, entry] : streams_) {
        entry.stream->request_stop();
    }
}

void Sources::apply(const v1::ApplyConfigurationRequest& assignment)
{
    std::map<Key, Endpoint> desired;
    for (const auto& camera : assignment.cameras()) {
        for (const auto profile : {Profile::main, Profile::secondary}) {
            desired.emplace(Key{camera.device_id(), camera.point_id(), profile, camera.channel_id(),
                                camera.main_profile_id(), camera.binding_revision()},
                            Endpoint{profile == Profile::main ? camera.main_url() : camera.secondary_url(),
                                     camera.credentials().username(), camera.credentials().password()});
        }
    }
    std::erase_if(streams_, [&desired](const auto& entry) {
        const auto replacement = desired.find(entry.first);
        return replacement == desired.end() || replacement->second != entry.second.endpoint;
    });
    for (const auto& [key, endpoint] : desired) {
        if (streams_.contains(key)) {
            continue;
        }
        auto report = [this, key, last_source = std::optional<domain::UtcTime>{},
                       last_loss = std::optional<domain::UtcTime>{}](const StreamStats& stats) mutable {
            if (key.profile == Profile::main && key.binding > 0) {
                auto record = [&](const auto& begin, const auto& end, auto& previous, domain::GapReason reason) {
                    if (begin && end && *end > *begin && end != previous) {
                        try {
                            volume_.report_gap({key.point, key.binding, begin->time_since_epoch().count(),
                                                end->time_since_epoch().count(),
                                                std::string{domain::name(reason)}});
                            previous = end;
                        } catch (const std::exception&) {
                            std::fputs("Archive gap journal unavailable\n", stderr);
                        }
                    }
                };
                record(stats.gap_begin, stats.gap_end, last_source, domain::GapReason::source_offline);
                if (!stats.consumers.empty()) {
                    const auto& writer = stats.consumers.front();
                    record(writer.loss_begin, writer.recovered_at, last_loss, domain::GapReason::overload);
                }
            }
            auto info = metrics(stats);
            info["event"] = "rtsp_ingest";
            info["device"] = key.device;
            info["point"] = key.point;
            info["profile"] = key.profile == Profile::main ? "main" : "secondary";
            std::fprintf(stderr, "%s\n", info.dump().c_str());
        };
        auto stream = std::make_unique<Stream>(StreamOptions{
            key.profile, endpoint, [this](const std::string& url) { return networks_.stream_url(url); },
            std::move(report)});
        std::unique_ptr<archive::Writer> writer;
        std::shared_ptr<CameraAudio> audio;
        if (key.profile == Profile::main) {
            auto pinned = networks_.stream_url(endpoint.url);
            if (pinned)
                audio = std::make_shared<CameraAudio>(Endpoint{*pinned, endpoint.username, endpoint.password});
            if (key.binding > 0) {
                archive::Origin origin{.point = key.point,
                                       .device = key.device,
                                       .channel = key.channel,
                                       .profile = key.main_profile,
                                       .binding = key.binding};
                writer = std::make_unique<archive::Writer>(volume_, std::move(origin), stream->subscribe(), audio);
            } else {
                std::fprintf(stderr, "archive awaiting binding for point %s\n", key.point.c_str());
            }
        }
        std::jthread detector;
        if (key.profile == Profile::main && key.binding > 0)
            detector = std::jthread{
                [this, ptr = stream.get(), point = key.point, binding = key.binding](const std::stop_token& stop) {
                    detect_motion(stop, *ptr, detectors_, volume_.owner(), point, binding);
                }};
        streams_.emplace(key, Entry{endpoint, audio, std::move(writer), std::move(stream), std::move(detector)});
    }
}

} // namespace bacuri::vms::ingest
