#include "archive/mp4.hpp"
#include <boost/endian/conversion.hpp>
#include <unistd.h>
#include <array>
#include <cerrno>
#include <stdexcept>
#include <string_view>
#include <system_error>

namespace bacuri::vms::archive
{
namespace
{
struct Box {
    std::string type;
    std::uint64_t content, end;
};

Box read_box(int fd, std::uint64_t offset, std::uint64_t limit)
{
    std::array<unsigned char, 16> header{};
    if (limit - offset < 8) {
        throw std::invalid_argument("Truncated MP4 header");
    }
    const auto count = pread(fd, header.data(), header.size(), static_cast<off_t>(offset));
    if (count < 0) {
        throw std::system_error(errno, std::generic_category(), "Cannot read MP4 header");
    }
    if (count < 8) {
        throw std::invalid_argument("Truncated MP4 header");
    }
    std::uint64_t length = boost::endian::load_big_u32(header.data());
    const std::uint64_t prefix = length == 1 ? 16 : 8;
    if (prefix == 16) {
        if (count < 16) {
            throw std::invalid_argument("Truncated MP4 extended size");
        }
        length = boost::endian::load_big_u64(header.data() + 8);
    }
    if (length < prefix || length > limit - offset) {
        throw std::invalid_argument("Invalid MP4 box size");
    }
    return {std::string(reinterpret_cast<const char*>(header.data() + 4), 4), offset + prefix, offset + length};
}

bool contains_box(int fd, const Box& parent, std::string_view type)
{
    for (auto offset = parent.content; offset < parent.end;) {
        const auto child = read_box(fd, offset, parent.end);
        if (child.type == type) {
            return true;
        }
        offset = child.end;
    }
    return false;
}
} // namespace

void verify_fragment_layout(int fd, std::uint64_t size)
{
    bool initialized{}, fragment{}, media{}, indexed{};
    for (std::uint64_t offset = 0; offset < size;) {
        const auto box = read_box(fd, offset, size);
        if (offset == 0 && box.type != "ftyp") {
            throw std::invalid_argument("Missing MP4 type");
        }
        if (box.type == "moov") {
            initialized = contains_box(fd, box, "trak") && contains_box(fd, box, "mvex");
        }
        if (box.type == "moof") {
            fragment = initialized && contains_box(fd, box, "traf");
        }
        if (box.type == "mdat") {
            media = fragment && box.content < box.end;
        }
        if (box.type == "mfra") {
            indexed = media && box.end == size && contains_box(fd, box, "tfra") && contains_box(fd, box, "mfro");
        }
        offset = box.end;
    }
    if (!indexed) {
        throw std::invalid_argument("Missing fMP4 initialization, media or random-access index");
    }
}
} // namespace bacuri::vms::archive
