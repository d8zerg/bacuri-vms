// Recordings stored on the camera and their replay addresses.
#include "application/control_api/http_json.hpp"
#include "modules/inventory/onvif.hpp"
#include "modules/inventory/onvif/soap.hpp"

namespace bacuri::vms::control
{
using namespace onvif;

EdgeArchive read_edge_archive(const CameraNetworks& networks, const CameraControl& control,
                              const CameraCredentials& credentials, const Key& key, EdgeSelection* selected)
{
    auto client = control_client(networks, control, credentials);
    EdgeArchive result;
    auto services = client.call(required_setting(control.device_service), device, "GetServices",
                                "<tds:IncludeCapability>false</tds:IncludeCapability>");
    std::map<std::string_view, std::string> endpoints;
    auto* root = response(services, device, "GetServicesResponse");
    if (!root)
        throw Failure{ErrorCode::camera_protocol_error};
    for (auto* item = root->children; item; item = item->next) {
        if (!named(item, device, "Service"))
            continue;
        auto ns = value(child(item, device, "Namespace"));
        for (auto expected : {recording, search, replay}) {
            if (ns != expected)
                continue;
            const auto address = value(child(item, device, "XAddr"));
            if (!address.starts_with("https://"))
                continue;
            auto uri = https_uri(address);
            if (!g_ascii_strcasecmp(client.host.c_str(), g_uri_get_host(uri.get())))
                endpoints[expected] = address;
        }
    }
    if (!endpoints.contains(recording) || !endpoints.contains(search) || !endpoints.contains(replay))
        return result;
    result.supported = true;
    auto list = client.call(endpoints.at(recording), recording, "GetRecordings");
    auto* body = response(list, recording, "GetRecordingsResponse");
    if (!body)
        throw Failure{ErrorCode::camera_protocol_error};
    std::set<std::string> seen;
    for (auto* item = body->children; item; item = item->next) {
        if (!named(item, recording, "RecordingItem"))
            continue;
        auto token = value(child(item, recording, "RecordingToken"));
        if (seen.size() >= 64 || token.size() > 256 || !seen.insert(token).second)
            throw Failure{ErrorCode::camera_protocol_error};
        auto info = client.call(endpoints.at(search), search, "GetRecordingInformation",
                                "<tse:RecordingToken>" + escape(token) + "</tse:RecordingToken>");
        auto* record =
            child(response(info, search, "GetRecordingInformationResponse"), search, "RecordingInformation");
        if (!record)
            throw Failure{ErrorCode::camera_protocol_error};
        std::vector<EdgeTrack> tracks;
        for (auto* track = record->children; track; track = track->next) {
            if (!named(track, schema, "Track"))
                continue;
            auto kind = value(child(track, schema, "TrackType"));
            if (kind != "Video" && kind != "Audio" && kind != "Metadata")
                continue;
            std::optional<std::pair<std::int64_t, std::int64_t>> bounds;
            auto* from = child(track, schema, "DataFrom");
            auto* to = child(track, schema, "DataTo");
            if (from && to) {
                Timestamp a, b;
                if (!ProtoTime::FromString(value(from), &a) || !ProtoTime::FromString(value(to), &b))
                    throw Failure{ErrorCode::camera_protocol_error};
                if (ProtoTime::TimestampToMicroseconds(a) < ProtoTime::TimestampToMicroseconds(b))
                    bounds =
                        std::pair{ProtoTime::TimestampToMicroseconds(a), ProtoTime::TimestampToMicroseconds(b)};
            }
            if (tracks.size() >= 64)
                throw Failure{ErrorCode::camera_protocol_error};
            tracks.push_back({kind, bounds});
        }
        const auto id = fingerprint(key, "edge-recording",
                                    Json::array({client.host, required_setting(control.source), token}).dump());
        auto* origin = child(record, schema, "Source");
        auto* name = child(origin, schema, "Name");
        const auto label = name ? value(name).substr(0, 256) : std::string{};
        result.recordings.push_back({id, label, tracks});
        if (selected && selected->id == id) {
            if (std::ranges::count_if(tracks, [](const auto& t) { return t.kind == "Video"; }) != 1)
                throw Failure{ErrorCode::edge_recording_unavailable};
            const auto reply =
                client.call(endpoints.at(replay), replay, "GetReplayUri",
                            "<trp:StreamSetup><tt:Stream>RTP-Unicast</tt:Stream><tt:Transport><tt:Protocol>RTSP</"
                            "tt:Protocol></tt:Transport></trp:StreamSetup>"
                            "<trp:RecordingToken>" +
                                escape(token) + "</trp:RecordingToken>");
            const auto uri = value(child(response(reply, replay, "GetReplayUriResponse"), replay, "Uri"));
            Uri parsed{g_uri_parse(uri.c_str(), G_URI_FLAGS_ENCODED, nullptr), g_uri_unref};
            if (!parsed || !g_uri_get_host(parsed.get()) ||
                g_ascii_strcasecmp(client.host.c_str(), g_uri_get_host(parsed.get())) || !networks.permits(uri))
                throw Failure{ErrorCode::camera_transport_denied};
            selected->uri = uri;
            const auto before = g_get_real_time();
            const auto clock =
                client.call(required_setting(control.device_service), device, "GetSystemDateAndTime");
            const auto after = g_get_real_time();
            auto* utc =
                child(child(response(clock, device, "GetSystemDateAndTimeResponse"), device, "SystemDateAndTime"),
                      schema, "UTCDateTime");
            auto number = [&](const char* part, const char* field) {
                const auto v = value(child(child(utc, schema, part), schema, field));
                if (v.size() > 4 || v.find_first_not_of("0123456789") != v.npos)
                    throw Failure{ErrorCode::camera_protocol_error};
                return std::stoi(v);
            };
            std::unique_ptr<GDateTime, decltype(&g_date_time_unref)> time{
                g_date_time_new_utc(number("Date", "Year"), number("Date", "Month"), number("Date", "Day"),
                                    number("Time", "Hour"), number("Time", "Minute"), number("Time", "Second")),
                g_date_time_unref};
            if (!time || after < before || after - before > 4000000)
                throw Failure{ErrorCode::camera_clock_uncertain};
            selected->offset_us =
                before + (after - before) / 2 - (g_date_time_to_unix(time.get()) * G_USEC_PER_SEC + 500000);
            selected->uncertainty_us = 500000 + (after - before + 1) / 2;
            // Do not shift UTC-aligned cameras by the arbitrary phase of a whole-second clock report.
            if (std::abs(selected->offset_us) <= selected->uncertainty_us) {
                selected->uncertainty_us += std::abs(selected->offset_us);
                selected->offset_us = 0;
            }
        }
    }
    return result;
}
} // namespace bacuri::vms::control
