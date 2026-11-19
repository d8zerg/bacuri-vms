#pragma once
#include "archive/manifest.hpp"
#include <cstdint>
namespace bacuri::vms::archive
{
MediaDigest digest_media(int fd);
struct FragmentLayout {
    std::uint64_t media_begin, media_end;
};
FragmentLayout verify_fragment_layout(int fd, std::uint64_t size);
} // namespace bacuri::vms::archive
