#pragma once
#include "domain/archive.hpp"
#include <nlohmann/json.hpp>

namespace bacuri::vms::archive
{
using Json = nlohmann::json;
struct Owner {
    std::string site, node;
};
struct Origin {
    std::string point, device, channel, profile;
    domain::Revision binding{};
    friend bool operator==(const Origin&, const Origin&) = default;
};
struct MediaDigest {
    std::int64_t bytes;
    std::string sha256;
};
struct Block {
    Origin origin;
    domain::ProjectedTime first;
    domain::UtcTime end;
    std::string codec{"h264"};
    std::string edge_recording{};
};
struct Identity {
    Owner owner;
    std::string epoch;
    std::int64_t generation;
};

inline constexpr std::size_t manifest_limit = 65536;
std::string uuid();
using BlockManifest = domain::BlockManifestV1;
BlockManifest parse_manifest(const Json&);
Json manifest_json(const BlockManifest&);
std::array<std::uint8_t, 32> parse_digest(const std::string& hash);
std::string digest_hex(const std::array<std::uint8_t, 32>& digest);
BlockManifest make_manifest(const Identity&, const std::string& id, const Block&, const MediaDigest&);
} // namespace bacuri::vms::archive
