#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace bacuri::vms::domain {

template <typename Tag>
class Identifier {
public:
    [[nodiscard]] static constexpr std::optional<Identifier> parse(std::string_view text) noexcept
    {
        if (text.size() != 36 || text[8] != '-' || text[13] != '-' || text[18] != '-' || text[23] != '-') {
            return std::nullopt;
        }

        std::array<std::uint8_t, 16> bytes{};
        std::size_t offset = 0;
        for (auto& byte : bytes) {
            if (offset == 8 || offset == 13 || offset == 18 || offset == 23) {
                ++offset;
            }

            const int high = hex_value(text[offset++]);
            const int low = hex_value(text[offset++]);
            if (high < 0 || low < 0) {
                return std::nullopt;
            }
            byte = static_cast<std::uint8_t>(high * 16 + low);
        }
        return Identifier{bytes};
    }

    [[nodiscard]] std::string to_string() const
    {
        constexpr std::string_view hex = "0123456789abcdef";
        std::string text;
        text.reserve(36);
        for (std::size_t index = 0; index < bytes_.size(); ++index) {
            if (index == 4 || index == 6 || index == 8 || index == 10) {
                text.push_back('-');
            }
            text.push_back(hex[bytes_[index] >> 4]);
            text.push_back(hex[bytes_[index] & 0x0f]);
        }
        return text;
    }

    [[nodiscard]] friend constexpr bool operator==(const Identifier&, const Identifier&) noexcept = default;

private:
    explicit constexpr Identifier(std::array<std::uint8_t, 16> bytes) noexcept : bytes_{bytes} {}

    [[nodiscard]] static constexpr int hex_value(char character) noexcept
    {
        if ('0' <= character && character <= '9') {
            return character - '0';
        }
        if ('a' <= character && character <= 'f') {
            return character - 'a' + 10;
        }
        if ('A' <= character && character <= 'F') {
            return character - 'A' + 10;
        }
        return -1;
    }

    std::array<std::uint8_t, 16> bytes_;
};

using SiteId = Identifier<struct SiteTag>;
using ObservationPointId = Identifier<struct ObservationPointTag>;
using DeviceId = Identifier<struct DeviceTag>;
using ChannelId = Identifier<struct ChannelTag>;
using StreamProfileId = Identifier<struct StreamProfileTag>;
using ClockEpochId = Identifier<struct ClockEpochTag>;
using RecordingEpochId = Identifier<struct RecordingEpochTag>;
using RecordingNodeId = Identifier<struct RecordingNodeTag>;
using BlockId = Identifier<struct BlockTag>;
using ObservationSourceId = Identifier<struct ObservationSourceTag>;
using ObservationId = Identifier<struct ObservationTag>;

}  // namespace bacuri::vms::domain
