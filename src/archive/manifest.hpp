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
Json make_manifest(const Identity&, const std::string& id, const Block&, const MediaDigest&);
void validate_manifest(const Json&);
} // namespace bacuri::vms::archive
