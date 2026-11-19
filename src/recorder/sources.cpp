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
        auto stream = std::make_unique<Stream>(
            StreamOptions{key.profile, endpoint,
                          [this](const std::string& url) { return networks_.permits(url); }, std::move(report)});
        std::unique_ptr<archive::Writer> writer;
        if (key.profile == Profile::main) {
            if (key.binding > 0) {
                archive::Origin origin{.point = key.point,
                                       .device = key.device,
                                       .channel = key.channel,
                                       .profile = key.main_profile,
                                       .binding = key.binding};
                writer = std::make_unique<archive::Writer>(volume_, std::move(origin), stream->subscribe());
            } else {
                std::fprintf(stderr, "archive awaiting binding for point %s\n", key.point.c_str());
            }
        }
        streams_.emplace(key, Entry{endpoint, std::move(writer), std::move(stream)});
    }
}

} // namespace bacuri::vms::ingest
