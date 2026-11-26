#include "archive/manifest_json.hpp"
#include "archive/manifest.hpp"

#include <sodium.h>

#include <algorithm>
#include <array>
#include <stdexcept>
#include <limits>

namespace bacuri::vms::archive
{
namespace
{
void require(bool condition)
{
    if (!condition) {
        throw std::invalid_argument("Invalid archive manifest");
    }
}
std::int64_t number(const Json& value)
{
    require(value.is_number_integer() &&
            (!value.is_number_unsigned() || value.get<std::uint64_t>() <= static_cast<std::uint64_t>(INT64_MAX)));
    return value.get<std::int64_t>();
}
} // namespace

std::string uuid()
{
    if (sodium_init() < 0)
        throw std::runtime_error("Cannot initialize the random number generator");
    std::array<unsigned char, 16> bytes{};
    randombytes_buf(bytes.data(), bytes.size());
    // Version 4 and the variant of RFC 9562.
    bytes[6] = static_cast<unsigned char>((bytes[6] & 0x0f) | 0x40);
    bytes[8] = static_cast<unsigned char>((bytes[8] & 0x3f) | 0x80);
    constexpr std::string_view digits{"0123456789abcdef"};
    std::string text;
    text.reserve(36);
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10)
            text.push_back('-');
        text.push_back(digits[bytes[i] >> 4]);
        text.push_back(digits[bytes[i] & 0x0f]);
    }
    return text;
}

namespace
{
template <class T> T manifest_id(const std::string& value)
{
    auto id = T::parse(value);
    if (!id || id->to_string() != value)
        throw std::invalid_argument("Invalid archive identifier");
    return *id;
}

bool is_digest(const std::string& text)
{
    return text.size() == 64 && text.find_first_not_of("0123456789abcdef") == text.npos;
}

domain::ProjectedTime read_time(const Json& time, domain::UtcTime begin)
{
    using namespace domain;
    require(time.is_object() && time.size() == 5);
    const UtcTime received{Microseconds{number(time.at("received_at_utc_us"))}};
    std::optional<SourceTime> source;
    if (!time.at("source_time_us").is_null())
        source = SourceTime{Microseconds{number(time.at("source_time_us"))}};
    if (time.at("basis") == "reception") {
        require(received == begin && time.at("clock_epoch_id").is_null() && time.at("uncertainty_us").is_null());
        return ProjectedTime::from_reception(received, source);
    }
    if (time.at("basis") != "source_clock" || !source)
        throw std::invalid_argument("Invalid archive source clock");
    const ClockAnchor anchor{manifest_id<ClockEpochId>(time.at("clock_epoch_id")), *source, begin,
                             Microseconds{number(time.at("uncertainty_us"))}};
    auto projection = ProjectedTime::from_source(anchor, *source, received);
    require(projection.has_value());
    return *projection;
}

void validate_audio(const domain::ManifestAudio& audio, std::int64_t begin, std::int64_t end)
{
    using Pcm = domain::ArchiveSound;
    require(audio.start_us >= begin && audio.start_us < end && audio.size_bytes > 0 &&
            audio.size_bytes <= 256000 && audio.size_bytes % Pcm::bytes_per_sample == 0);
    const auto duration = static_cast<std::uint64_t>(end) - static_cast<std::uint64_t>(audio.start_us);
    require(static_cast<std::uint64_t>(audio.size_bytes / Pcm::bytes_per_sample) <=
            duration / Pcm::sample_period_us);
}

void validate_edge(const std::string& id, const domain::ProjectedTime& first)
{
    require(first.basis() == domain::TimeBasis::source_clock && is_digest(id));
}
} // namespace

std::array<std::uint8_t, 32> parse_digest(const std::string& hash)
{
    std::array<std::uint8_t, 32> digest{};
    require(is_digest(hash) && sodium_hex2bin(digest.data(), digest.size(), hash.data(), hash.size(), nullptr,
                                              nullptr, nullptr) == 0);
    return digest;
}
std::string digest_hex(const std::array<std::uint8_t, 32>& digest)
{
    std::array<char, 65> hex{};
    sodium_bin2hex(hex.data(), hex.size(), digest.data(), digest.size());
    return hex.data();
}

BlockManifest parse_manifest(const Json& m)
{
    using namespace domain;
    require(m.is_object() && m.size() == 15u + m.contains("audio") + m.contains("edge_recording") &&
            number(m.at("manifest_version")) == BlockManifest::version);
    const auto site = manifest_id<SiteId>(m.at("site_id"));
    const BlockKey key{site, manifest_id<BlockId>(m.at("block_id")),
                       manifest_id<RecordingEpochId>(m.at("recording_epoch_id")),
                       number(m.at("owner_generation"))};
    const BindingKey binding{site, manifest_id<ObservationPointId>(m.at("point_id")),
                             number(m.at("binding_revision"))};
    const UtcTime begin{Microseconds{number(m.at("begin_utc_us"))}}, end{Microseconds{number(m.at("end_utc_us"))}};
    const auto interval = UtcInterval::make(begin, end);
    require(interval.has_value() && key.owner_generation > 0 && binding.revision > 0);
    const auto first = read_time(m.at("time"), begin);
    const auto& media = m.at("media");
    require(media.is_object() && media.size() == 4 && media.at("file") == "media.mp4");
    BlockManifest result{RecordedInterval::make(key, binding, *interval, first).value(),
                         manifest_id<RecordingNodeId>(m.at("recording_node_id")),
                         manifest_id<DeviceId>(m.at("device_id")),
                         manifest_id<ChannelId>(m.at("channel_id")),
                         manifest_id<StreamProfileId>(m.at("profile_id")),
                         number(media.at("size_bytes")),
                         parse_digest(media.at("sha256")),
                         {},
                         {},
                         {}};
    const auto& tracks = media.at("tracks");
    require(result.size_bytes > 0 && tracks.is_array() && !tracks.empty());
    for (const auto& track : tracks) {
        const auto id = number(track.at("id")), scale = number(track.at("time_scale"));
        const auto& codec = track.at("codec").get_ref<const std::string&>();
        const auto& kind = track.at("kind");
        require(track.is_object() && track.size() == 4 && (kind == "video" || kind == "audio") && !codec.empty() &&
                id > 0 && id <= INT32_MAX && scale > 0 && scale <= INT32_MAX);
        result.tracks.push_back({static_cast<std::int32_t>(id),
                                 kind == "video" ? TrackKind::video : TrackKind::audio, codec,
                                 static_cast<std::int32_t>(scale)});
    }
    if (m.contains("audio")) {
        const auto& audio = m.at("audio");
        require(audio.is_object() && audio.size() == 3);
        result.audio = ManifestAudio{number(audio.at("start_us")), number(audio.at("size_bytes")),
                                     parse_digest(audio.at("sha256"))};
        validate_audio(*result.audio, result.begin_us(), result.end_us());
    }
    if (m.contains("edge_recording")) {
        result.edge_recording = m.at("edge_recording").get<std::string>();
        validate_edge(*result.edge_recording, first);
    }
    return result;
}
Json manifest_json(const BlockManifest& m)
{
    const auto& key = m.recording.block();
    const auto& time = m.recording.first_frame();
    Json timing{{"basis", time.basis() == domain::TimeBasis::source_clock ? "source_clock" : "reception"},
                {"received_at_utc_us", time.received_at().time_since_epoch().count()},
                {"source_time_us", nullptr},
                {"clock_epoch_id", nullptr},
                {"uncertainty_us", nullptr}};
    if (auto value = time.source()) {
        timing["source_time_us"] = value->value.count();
    }
    if (auto value = time.clock_epoch()) {
        timing["clock_epoch_id"] = value->to_string();
    }
    if (auto value = time.uncertainty()) {
        timing["uncertainty_us"] = value->count();
    }
    Json tracks = Json::array();
    for (const auto& track : m.tracks)
        tracks.push_back({{"id", track.id},
                          {"kind", track.kind == domain::TrackKind::video ? "video" : "audio"},
                          {"codec", track.codec},
                          {"time_scale", track.time_scale}});
    Json result{{"manifest_version", domain::BlockManifestV1::version},
                {"site_id", key.site.to_string()},
                {"block_id", key.id.to_string()},
                {"recording_epoch_id", key.recording_epoch.to_string()},
                {"owner_generation", key.owner_generation},
                {"recording_node_id", m.recording_node.to_string()},
                {"point_id", m.recording.binding().point.to_string()},
                {"device_id", m.device.to_string()},
                {"channel_id", m.channel.to_string()},
                {"profile_id", m.profile.to_string()},
                {"binding_revision", m.recording.binding().revision},
                {"begin_utc_us", time.utc().time_since_epoch().count()},
                {"end_utc_us", m.recording.interval().end().time_since_epoch().count()},
                {"time", std::move(timing)},
                {"media",
                 {{"file", "media.mp4"},
                  {"size_bytes", m.size_bytes},
                  {"sha256", digest_hex(m.sha256)},
                  {"tracks", std::move(tracks)}}}};
    if (m.edge_recording)
        result["edge_recording"] = *m.edge_recording;
    if (m.audio) {
        validate_audio(*m.audio, m.begin_us(), m.end_us());
        result["audio"] = {{"start_us", m.audio->start_us},
                           {"size_bytes", m.audio->size_bytes},
                           {"sha256", digest_hex(m.audio->sha256)}};
    }
    return result;
}

BlockManifest make_manifest(const Identity& identity, const std::string& id, const Block& block,
                            const MediaDigest& digest)
{
    using namespace domain;
    const auto site = manifest_id<SiteId>(identity.owner.site);
    const auto interval = UtcInterval::make(block.first.utc(), block.end);
    require(interval.has_value() && identity.generation > 0 && block.origin.binding > 0 && digest.bytes > 0 &&
            !block.codec.empty());
    const auto recording =
        RecordedInterval::make(
            {site, manifest_id<BlockId>(id), manifest_id<RecordingEpochId>(identity.epoch), identity.generation},
            {site, manifest_id<ObservationPointId>(block.origin.point), block.origin.binding}, *interval,
            block.first)
            .value();
    BlockManifest manifest{recording,
                           manifest_id<RecordingNodeId>(identity.owner.node),
                           manifest_id<DeviceId>(block.origin.device),
                           manifest_id<ChannelId>(block.origin.channel),
                           manifest_id<StreamProfileId>(block.origin.profile),
                           digest.bytes,
                           parse_digest(digest.sha256),
                           {{1, TrackKind::video, block.codec, 90000}},
                           {},
                           {}};
    if (!block.edge_recording.empty()) {
        validate_edge(block.edge_recording, block.first);
        manifest.edge_recording = block.edge_recording;
    }
    return manifest;
}
} // namespace bacuri::vms::archive
