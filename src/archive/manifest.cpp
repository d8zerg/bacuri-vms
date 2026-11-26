#include "archive/manifest.hpp"
#include "platform/identifiers.hpp"

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

void validate_manifest(const Json& m)
{
    require(m.is_object() && m.size() == 15u + m.contains("audio") + m.contains("edge_recording") &&
            number(m.at("manifest_version")) == domain::BlockManifestV1::version);
    for (const auto key : {"site_id", "recording_node_id", "block_id", "recording_epoch_id", "point_id",
                           "device_id", "channel_id", "profile_id"}) {
        require(platform::canonical_id(m.at(key).get_ref<const std::string&>()));
    }
    require(number(m.at("owner_generation")) > 0 && number(m.at("binding_revision")) > 0);
    const auto begin = number(m.at("begin_utc_us"));
    const auto end = number(m.at("end_utc_us"));
    require(domain::UtcInterval::make(domain::UtcTime{domain::Microseconds{begin}},
                                      domain::UtcTime{domain::Microseconds{end}})
                .has_value());
    if (m.contains("edge_recording")) {
        const auto& id = m.at("edge_recording").get_ref<const std::string&>();
        require(m.at("time").at("basis") == "source_clock" && id.size() == 64 &&
                id.find_first_not_of("0123456789abcdef") == id.npos);
    }
    if (m.contains("audio")) {
        const auto& a = m.at("audio");
        const auto start = number(a.at("start_us")), bytes = number(a.at("size_bytes"));
        const auto& hash = a.at("sha256").get_ref<const std::string&>();
        require(a.is_object() && a.size() == 3 && start >= begin && start < end && bytes > 0 &&
                bytes % domain::ArchiveSound::bytes_per_sample == 0 && bytes <= 256000 &&
                bytes / domain::ArchiveSound::bytes_per_sample <=
                    (end - start) / domain::ArchiveSound::sample_period_us &&
                hash.size() == 64 && hash.find_first_not_of("0123456789abcdef") == hash.npos);
    }
    const auto& t = m.at("time");
    require(t.is_object() && t.size() == 5);
    auto received = number(t.at("received_at_utc_us"));
    const auto& source = t.at("source_time_us");
    if (!source.is_null()) {
        number(source);
    }
    if (t.at("basis") == "source_clock") {
        require(!source.is_null() &&
                platform::canonical_id(t.at("clock_epoch_id").get_ref<const std::string&>()) &&
                number(t.at("uncertainty_us")) >= 0);
    } else {
        require(t.at("basis") == "reception" && received == begin && t.at("clock_epoch_id").is_null() &&
                t.at("uncertainty_us").is_null());
    }
    const auto& media = m.at("media");
    require(media.is_object() && media.size() == 4 && media.at("file") == "media.mp4" &&
            number(media.at("size_bytes")) > 0);
    const auto& hash = media.at("sha256").get_ref<const std::string&>();
    require(hash.size() == 64 && hash.find_first_not_of("0123456789abcdef") == hash.npos);
    const auto& tracks = media.at("tracks");
    require(tracks.is_array() && !tracks.empty());
    for (const auto& track : tracks) {
        require(track.is_object() && track.size() == 4 &&
                (track.at("kind") == "video" || track.at("kind") == "audio") &&
                !track.at("codec").get_ref<const std::string&>().empty());
        for (auto field : {"id", "time_scale"}) {
            const auto value = number(track.at(field));
            require(value > 0 && value <= INT32_MAX);
        }
    }
}

Json make_manifest(const Identity& identity, const std::string& id, const Block& block, const MediaDigest& digest)
{
    const auto& time = block.first;
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
    Json track{{"id", 1}, {"kind", "video"}, {"codec", block.codec}, {"time_scale", 90000}};
    Json result{{"manifest_version", domain::BlockManifestV1::version},
                {"site_id", identity.owner.site},
                {"block_id", id},
                {"recording_epoch_id", identity.epoch},
                {"owner_generation", identity.generation},
                {"recording_node_id", identity.owner.node},
                {"point_id", block.origin.point},
                {"device_id", block.origin.device},
                {"channel_id", block.origin.channel},
                {"profile_id", block.origin.profile},
                {"binding_revision", block.origin.binding},
                {"begin_utc_us", time.utc().time_since_epoch().count()},
                {"end_utc_us", block.end.time_since_epoch().count()},
                {"time", std::move(timing)},
                {"media",
                 {{"file", "media.mp4"},
                  {"size_bytes", digest.bytes},
                  {"sha256", digest.sha256},
                  {"tracks", Json::array({track})}}}};
    if (!block.edge_recording.empty())
        result["edge_recording"] = block.edge_recording;
    validate_manifest(result);
    return result;
}
} // namespace bacuri::vms::archive
