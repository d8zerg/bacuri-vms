#include "recorder/sources.hpp"

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
        p->set_width(stats.width);
        p->set_height(stats.height);
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

archive::Json Sources::recording_states()
{
    auto result = Json::object();
    for (const auto& [key, entry] : streams_) {
        if (key.profile != Profile::main)
            continue;
        auto stats = entry.stream->stats();
        auto rule = volume_.recording_rule(key.point);
        std::string state;
        if (!key.binding)
            state = "unbound";
        else if (stats.state != "receiving" || !stats.time)
            state = "source_offline";
        else if (!rule)
            state = "awaiting_policy";
        else if (rule->selects(stats.time->utc().time_since_epoch().count(), key.binding))
            state = "selected";
        else if (rule->event)
            state = "waiting_event";
        else if (!rule->schedule.empty())
            state = "outside_schedule";
        else if (rule->manual)
            state = "waiting_manual";
        else
            state = "disabled";
        result[key.point] = state;
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
                auto record = [&](const auto& begin, const auto& end, auto& previous, const char* reason) {
                    if (begin && end && *end > *begin && end != previous) {
                        try {
                            volume_.report_gap({key.point, key.binding, begin->time_since_epoch().count(),
                                                end->time_since_epoch().count(), reason});
                            previous = end;
                        } catch (const std::exception&) {
                            std::fputs("Archive gap journal unavailable\n", stderr);
                        }
                    }
                };
                record(stats.gap_begin, stats.gap_end, last_source, "source_offline");
                if (!stats.consumers.empty()) {
                    const auto& writer = stats.consumers.front();
                    record(writer.loss_begin, writer.recovered_at, last_loss, "overload");
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
        streams_.emplace(key, Entry{endpoint, audio, std::move(writer), std::move(stream)});
    }
}

} // namespace bacuri::vms::ingest
