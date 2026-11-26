#include "archive/fmp4.hpp"
#include <algorithm>
#include <array>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace bacuri::vms::archive::fmp4
{
namespace
{
using Bytes = std::span<const std::byte>;
constexpr std::uint64_t nanoseconds_per_second = 1'000'000'000;
constexpr std::uint32_t track_id = 1;
// Sample flags of ISO/IEC 14496-12 8.8.3.1: a sync sample depends on no other sample, any other sample
// depends on others and is not a sync sample.
constexpr std::uint32_t sync_sample_flags = 0x0200'0000;
constexpr std::uint32_t delta_sample_flags = 0x0101'0000;

[[noreturn]] void reject(const char* reason)
{
    throw std::invalid_argument(reason);
}

struct FullBox {
    std::uint8_t version;
    std::uint32_t flags;
};

class Output
{
  public:
    void u8(std::uint32_t value)
    {
        data_.push_back(static_cast<std::byte>(value & 0xff));
    }
    void u16(std::uint32_t value)
    {
        u8(value >> 8);
        u8(value);
    }
    void u24(std::uint32_t value)
    {
        u8(value >> 16);
        u16(value);
    }
    void u32(std::uint32_t value)
    {
        u16(value >> 16);
        u16(value);
    }
    void u64(std::uint64_t value)
    {
        u32(static_cast<std::uint32_t>(value >> 32));
        u32(static_cast<std::uint32_t>(value));
    }
    void zeros(std::size_t count)
    {
        data_.insert(data_.end(), count, std::byte{});
    }
    void bytes(Bytes value)
    {
        data_.insert(data_.end(), value.begin(), value.end());
    }
    void fourcc(const char (&code)[5])
    {
        for (int i = 0; i < 4; ++i)
            u8(static_cast<unsigned char>(code[i]));
    }
    // Starts a box and returns its offset for close().
    std::size_t open(const char (&type)[5])
    {
        const auto at = data_.size();
        u32(0);
        fourcc(type);
        return at;
    }
    // Starts a full box with its version and flags.
    std::size_t open(const char (&type)[5], FullBox header)
    {
        const auto at = open(type);
        u8(header.version);
        u24(header.flags);
        return at;
    }
    void close(std::size_t at)
    {
        patch(at, checked(data_.size() - at));
    }
    void patch(std::size_t at, std::uint32_t value)
    {
        for (std::size_t i = 0; i < 4; ++i)
            data_[at + i] = static_cast<std::byte>((value >> (24 - 8 * i)) & 0xff);
    }
    std::size_t size() const
    {
        return data_.size();
    }
    std::vector<std::byte> release()
    {
        return std::move(data_);
    }

    static std::uint32_t checked(std::size_t value)
    {
        if (value > std::numeric_limits<std::uint32_t>::max())
            reject("MP4 box exceeds 4 GiB");
        return static_cast<std::uint32_t>(value);
    }

  private:
    std::vector<std::byte> data_;
};

// NAL units of an Annex B access unit without their start codes.
std::vector<Bytes> nal_units(Bytes unit)
{
    std::vector<std::size_t> starts;
    for (std::size_t i = 0; i + 2 < unit.size(); ++i) {
        if (unit[i] == std::byte{0} && unit[i + 1] == std::byte{0} && unit[i + 2] == std::byte{1}) {
            starts.push_back(i + 3);
            i += 2;
        }
    }
    if (starts.empty())
        reject("Access unit without a start code");
    std::vector<Bytes> result;
    for (std::size_t n = 0; n < starts.size(); ++n) {
        auto end = n + 1 < starts.size() ? starts[n + 1] - 3 : unit.size();
        // Zero bytes before the next start code belong to it or are trailing padding.
        while (end > starts[n] && unit[end - 1] == std::byte{0})
            --end;
        if (end > starts[n])
            result.push_back(unit.subspan(starts[n], end - starts[n]));
    }
    return result;
}

// The NAL unit payload without emulation prevention bytes.
std::vector<std::uint8_t> rbsp(Bytes nal)
{
    std::vector<std::uint8_t> result;
    result.reserve(nal.size());
    int zeros = 0;
    for (const auto value : nal) {
        const auto byte = std::to_integer<std::uint8_t>(value);
        if (zeros >= 2 && byte == 3) {
            zeros = 0;
            continue;
        }
        zeros = byte == 0 ? zeros + 1 : 0;
        result.push_back(byte);
    }
    return result;
}

class BitReader
{
  public:
    BitReader(const std::vector<std::uint8_t>& data, std::size_t first_byte) : data_{data}, bit_{first_byte * 8} {}
    std::uint32_t bits(int count)
    {
        std::uint32_t value = 0;
        for (int i = 0; i < count; ++i) {
            if (bit_ >= data_.size() * 8)
                reject("Truncated parameter set");
            value = (value << 1) | ((data_[bit_ / 8] >> (7 - bit_ % 8)) & 1u);
            ++bit_;
        }
        return value;
    }
    bool flag()
    {
        return bits(1) != 0;
    }
    void skip(int count)
    {
        while (count > 0) {
            const auto step = std::min(count, 32);
            bits(step);
            count -= step;
        }
    }
    // Exp-Golomb code ue(v).
    std::uint32_t ue()
    {
        int zeros = 0;
        while (!flag()) {
            if (++zeros > 31)
                reject("Invalid Exp-Golomb code");
        }
        return (zeros ? bits(zeros) : 0) + ((1u << zeros) - 1);
    }
    // Exp-Golomb code se(v).
    std::int32_t se()
    {
        const auto code = ue();
        const auto magnitude = static_cast<std::int32_t>((code + 1) / 2);
        return code % 2 ? magnitude : -magnitude;
    }

  private:
    const std::vector<std::uint8_t>& data_;
    std::size_t bit_;
};

struct Picture {
    std::uint32_t width{}, height{};
    std::uint32_t chroma_format = 1, luma_bit_depth = 8, chroma_bit_depth = 8;
};

// Frame size from the coded size and the cropping window in chroma units (H.264 7.4.2.1.1, H.265 7.4.3.2.1).
std::uint32_t cropped(std::uint32_t coded, std::uint64_t crop, std::uint32_t unit)
{
    if (crop * unit >= coded)
        reject("Cropping window exceeds the picture");
    return coded - static_cast<std::uint32_t>(crop * unit);
}

void skip_scaling_list(BitReader& reader, int size)
{
    int last = 8, next = 8;
    for (int j = 0; j < size; ++j) {
        if (next != 0)
            next = (last + reader.se() + 256) % 256;
        last = next == 0 ? last : next;
    }
}

// H.264 seq_parameter_set_data (7.3.2.1.1) up to the cropping window.
Picture h264_picture(const std::vector<std::uint8_t>& sps)
{
    BitReader reader{sps, 1};
    const auto profile = reader.bits(8);
    reader.skip(16);
    reader.ue();
    Picture picture;
    bool separate_planes = false;
    if (profile == 100 || profile == 110 || profile == 122 || profile == 244 || profile == 44 || profile == 83 ||
        profile == 86 || profile == 118 || profile == 128 || profile == 138 || profile == 139 || profile == 134 ||
        profile == 135) {
        picture.chroma_format = reader.ue();
        if (picture.chroma_format == 3)
            separate_planes = reader.flag();
        picture.luma_bit_depth = reader.ue() + 8;
        picture.chroma_bit_depth = reader.ue() + 8;
        reader.skip(1);
        if (reader.flag()) {
            for (int i = 0; i < (picture.chroma_format == 3 ? 12 : 8); ++i) {
                if (reader.flag())
                    skip_scaling_list(reader, i < 6 ? 16 : 64);
            }
        }
    }
    reader.ue();
    const auto order_type = reader.ue();
    if (order_type == 0) {
        reader.ue();
    } else if (order_type == 1) {
        reader.skip(1);
        reader.se();
        reader.se();
        const auto cycle = reader.ue();
        for (std::uint32_t i = 0; i < cycle; ++i)
            reader.se();
    }
    reader.ue();
    reader.skip(1);
    const auto width_mbs = reader.ue() + 1;
    const auto height_units = reader.ue() + 1;
    const bool frame_only = reader.flag();
    if (!frame_only)
        reader.skip(1);
    reader.skip(1);
    const auto chroma_type = separate_planes ? 0 : picture.chroma_format;
    const std::uint32_t unit_x = chroma_type == 0 || chroma_type == 3 ? 1 : 2;
    const std::uint32_t unit_y = (chroma_type == 1 ? 2u : 1u) * (frame_only ? 1u : 2u);
    picture.width = width_mbs * 16;
    picture.height = height_units * 16 * (frame_only ? 1 : 2);
    if (reader.flag()) {
        const std::uint64_t left = reader.ue(), right = reader.ue(), top = reader.ue(), bottom = reader.ue();
        picture.width = cropped(picture.width, left + right, unit_x);
        picture.height = cropped(picture.height, top + bottom, unit_y);
    }
    return picture;
}

// H.265 seq_parameter_set_rbsp (7.3.2.2.1) up to the bit depths.
Picture h265_picture(const std::vector<std::uint8_t>& sps)
{
    BitReader reader{sps, 2};
    reader.skip(4);
    const auto sub_layers = reader.bits(3);
    reader.skip(1);
    // General profile, tier and level.
    reader.skip(2 + 1 + 5 + 32 + 48 + 8);
    std::array<bool, 8> profile_present{}, level_present{};
    for (std::uint32_t i = 0; i < sub_layers; ++i) {
        profile_present[i] = reader.flag();
        level_present[i] = reader.flag();
    }
    if (sub_layers > 0)
        reader.skip(2 * static_cast<int>(8 - sub_layers));
    for (std::uint32_t i = 0; i < sub_layers; ++i) {
        if (profile_present[i])
            reader.skip(88);
        if (level_present[i])
            reader.skip(8);
    }
    reader.ue();
    Picture picture;
    picture.chroma_format = reader.ue();
    bool separate_planes = false;
    if (picture.chroma_format == 3)
        separate_planes = reader.flag();
    picture.width = reader.ue();
    picture.height = reader.ue();
    if (reader.flag()) {
        const std::uint64_t left = reader.ue(), right = reader.ue(), top = reader.ue(), bottom = reader.ue();
        const auto chroma_type = separate_planes ? 0 : picture.chroma_format;
        picture.width = cropped(picture.width, left + right, chroma_type == 1 || chroma_type == 2 ? 2 : 1);
        picture.height = cropped(picture.height, top + bottom, chroma_type == 1 ? 2 : 1);
    }
    picture.luma_bit_depth = reader.ue() + 8;
    picture.chroma_bit_depth = reader.ue() + 8;
    return picture;
}

struct Configuration {
    std::vector<Bytes> vps, sps, pps;
};

enum class NalKind { parameter_vps, parameter_sps, parameter_pps, delimiter, media };

NalKind kind(Codec codec, Bytes nal)
{
    const auto header = std::to_integer<std::uint8_t>(nal[0]);
    if (codec == Codec::h264) {
        switch (header & 0x1f) {
        case 7:
            return NalKind::parameter_sps;
        case 8:
            return NalKind::parameter_pps;
        case 9:
            return NalKind::delimiter;
        default:
            return NalKind::media;
        }
    }
    if (nal.size() < 2)
        reject("Truncated H.265 NAL unit header");
    switch ((header >> 1) & 0x3f) {
    case 32:
        return NalKind::parameter_vps;
    case 33:
        return NalKind::parameter_sps;
    case 34:
        return NalKind::parameter_pps;
    case 35:
        return NalKind::delimiter;
    default:
        return NalKind::media;
    }
}

void add_unique(std::vector<Bytes>& sets, Bytes nal)
{
    if (std::ranges::none_of(sets, [&](Bytes known) { return std::ranges::equal(known, nal); }))
        sets.push_back(nal);
}

void parameter_list(Output& out, const std::vector<Bytes>& sets)
{
    for (const auto set : sets) {
        if (set.size() > 0xffff)
            reject("Parameter set exceeds 64 KiB");
        out.u16(static_cast<std::uint32_t>(set.size()));
        out.bytes(set);
    }
}

// AVCDecoderConfigurationRecord (ISO/IEC 14496-15 5.3.3.1).
void avc_configuration(Output& out, const Configuration& sets, const Picture& picture)
{
    const auto at = out.open("avcC");
    const auto& sps = sets.sps.front();
    if (sps.size() < 4)
        reject("Truncated H.264 sequence parameter set");
    out.u8(1);
    out.bytes(sps.subspan(1, 3));
    out.u8(0xfc | 3);
    out.u8(0xe0 | static_cast<std::uint32_t>(sets.sps.size()));
    parameter_list(out, sets.sps);
    out.u8(static_cast<std::uint32_t>(sets.pps.size()));
    parameter_list(out, sets.pps);
    const auto profile = std::to_integer<std::uint8_t>(sps[1]);
    if (profile != 66 && profile != 77 && profile != 88) {
        out.u8(0xfc | picture.chroma_format);
        out.u8(0xf8 | (picture.luma_bit_depth - 8));
        out.u8(0xf8 | (picture.chroma_bit_depth - 8));
        out.u8(0);
    }
    out.close(at);
}

// HEVCDecoderConfigurationRecord (ISO/IEC 14496-15 8.3.3.1).
void hevc_configuration(Output& out, const Configuration& sets, const Picture& picture,
                        const std::vector<std::uint8_t>& sps)
{
    if (sps.size() < 15)
        reject("Truncated H.265 sequence parameter set");
    const auto at = out.open("hvcC");
    out.u8(1);
    // General profile space, tier, profile, compatibility flags, constraint flags and level.
    for (std::size_t i = 3; i < 15; ++i)
        out.u8(sps[i]);
    out.u16(0xf000);
    out.u8(0xfc);
    out.u8(0xfc | picture.chroma_format);
    out.u8(0xf8 | (picture.luma_bit_depth - 8));
    out.u8(0xf8 | (picture.chroma_bit_depth - 8));
    out.u16(0);
    const auto sub_layers = (sps[2] >> 1) & 7u;
    const auto nested = sps[2] & 1u;
    out.u8(((sub_layers + 1) << 3) | (nested << 2) | 3);
    out.u8(3);
    for (const auto& [type, list] :
         {std::pair{32u, &sets.vps}, std::pair{33u, &sets.sps}, std::pair{34u, &sets.pps}}) {
        out.u8(0x80 | type);
        out.u16(static_cast<std::uint32_t>(list->size()));
        parameter_list(out, *list);
    }
    out.close(at);
}

void identity_matrix(Output& out)
{
    for (const std::uint32_t value : {0x0001'0000u, 0u, 0u, 0u, 0x0001'0000u, 0u, 0u, 0u, 0x4000'0000u})
        out.u32(value);
}

std::uint64_t ticks(std::uint64_t ns)
{
    return ns / nanoseconds_per_second * timescale +
           (ns % nanoseconds_per_second * timescale + nanoseconds_per_second / 2) / nanoseconds_per_second;
}

struct Timing {
    std::uint64_t decode, duration;
    std::uint32_t composition_offset;
};

struct Track {
    std::vector<Timing> samples;
    // Composition time of presentation time zero. It is non-zero only for reordered frames, and an edit list
    // removes it from the presentation.
    std::uint64_t delay{};
    std::uint64_t presentation_duration{};
};

// Decode times are the presentation times in ascending order. A reordered frame is decoded after a frame that is
// presented later, so composition times are shifted by the largest such distance to stay at or after decoding.
Track timing(std::span<const Sample> samples)
{
    std::vector<std::uint64_t> presentation;
    presentation.reserve(samples.size());
    for (const auto& sample : samples)
        presentation.push_back(ticks(sample.pts_ns));
    auto decode = presentation;
    std::ranges::sort(decode);
    for (std::size_t i = 1; i < decode.size(); ++i)
        decode[i] = std::max(decode[i], decode[i - 1] + 1);
    Track track;
    for (std::size_t i = 0; i < samples.size(); ++i)
        track.delay = std::max(track.delay, decode[i] > presentation[i] ? decode[i] - presentation[i] : 0);
    track.samples.resize(samples.size());
    for (std::size_t i = 0; i < samples.size(); ++i) {
        const auto offset = presentation[i] + track.delay - decode[i];
        const auto duration = i + 1 < samples.size() ? decode[i + 1] - decode[i]
                                                     : std::max<std::uint64_t>(1, ticks(samples[i].duration_ns));
        if (offset > std::numeric_limits<std::uint32_t>::max() ||
            duration > std::numeric_limits<std::uint32_t>::max())
            reject("Sample time out of range");
        track.samples[i] = {decode[i], duration, static_cast<std::uint32_t>(offset)};
        track.presentation_duration =
            std::max(track.presentation_duration,
                     presentation[i] + std::max<std::uint64_t>(1, ticks(samples[i].duration_ns)));
    }
    return track;
}

void file_header(Output& out, Codec codec, const Configuration& sets, const Picture& picture,
                 const std::vector<std::uint8_t>& sps, const Track& track)
{
    auto box = out.open("ftyp");
    out.fourcc("iso6");
    out.u32(0);
    out.fourcc("iso6");
    out.fourcc("isom");
    out.fourcc("mp41");
    out.close(box);

    const auto moov = out.open("moov");
    box = out.open("mvhd", {0, 0});
    out.u32(0);
    out.u32(0);
    out.u32(timescale);
    out.u32(0);
    out.u32(0x0001'0000);
    out.u16(0x0100);
    out.zeros(10);
    identity_matrix(out);
    out.zeros(24);
    out.u32(track_id + 1);
    out.close(box);

    const auto trak = out.open("trak");
    box = out.open("tkhd", {0, 3});
    out.u32(0);
    out.u32(0);
    out.u32(track_id);
    out.u32(0);
    out.u32(0);
    out.zeros(8);
    out.u16(0);
    out.u16(0);
    out.u16(0);
    out.u16(0);
    identity_matrix(out);
    out.u32(picture.width << 16);
    out.u32(picture.height << 16);
    out.close(box);
    if (track.delay) {
        const auto edts = out.open("edts");
        box = out.open("elst", {1, 0});
        out.u32(1);
        out.u64(track.presentation_duration);
        out.u64(track.delay);
        out.u16(1);
        out.u16(0);
        out.close(box);
        out.close(edts);
    }

    const auto mdia = out.open("mdia");
    box = out.open("mdhd", {0, 0});
    out.u32(0);
    out.u32(0);
    out.u32(timescale);
    out.u32(0);
    // Language "und" packed as three 5-bit letters.
    out.u16(0x55c4);
    out.u16(0);
    out.close(box);
    box = out.open("hdlr", {0, 0});
    out.u32(0);
    out.fourcc("vide");
    out.zeros(12);
    for (const char letter : std::string_view{"VideoHandler"})
        out.u8(static_cast<unsigned char>(letter));
    out.u8(0);
    out.close(box);

    const auto minf = out.open("minf");
    box = out.open("vmhd", {0, 1});
    out.zeros(8);
    out.close(box);
    const auto dinf = out.open("dinf");
    box = out.open("dref", {0, 0});
    out.u32(1);
    out.close(out.open("url ", {0, 1}));
    out.close(box);
    out.close(dinf);

    const auto stbl = out.open("stbl");
    box = out.open("stsd", {0, 0});
    out.u32(1);
    const auto entry = codec == Codec::h264 ? out.open("avc1") : out.open("hvc1");
    out.zeros(6);
    out.u16(1);
    out.zeros(16);
    if (picture.width > 0xffff || picture.height > 0xffff)
        reject("Picture size out of range");
    out.u16(picture.width);
    out.u16(picture.height);
    out.u32(0x0048'0000);
    out.u32(0x0048'0000);
    out.u32(0);
    out.u16(1);
    out.zeros(32);
    out.u16(0x0018);
    out.u16(0xffff);
    if (codec == Codec::h264)
        avc_configuration(out, sets, picture);
    else
        hevc_configuration(out, sets, picture, sps);
    out.close(entry);
    out.close(box);
    // The samples are described by the fragments, so the sample tables are empty.
    const auto empty_table = [&](const char(&type)[5]) {
        const auto table = out.open(type, {0, 0});
        out.u32(0);
        out.close(table);
    };
    empty_table("stts");
    empty_table("stsc");
    empty_table("stco");
    box = out.open("stsz", {0, 0});
    out.u32(0);
    out.u32(0);
    out.close(box);
    out.close(stbl);
    out.close(minf);
    out.close(mdia);
    out.close(trak);

    const auto mvex = out.open("mvex");
    box = out.open("mehd", {1, 0});
    out.u64(track.presentation_duration);
    out.close(box);
    box = out.open("trex", {0, 0});
    out.u32(track_id);
    out.u32(1);
    out.u32(0);
    out.u32(0);
    out.u32(0);
    out.close(box);
    out.close(mvex);
    out.close(moov);
}

struct SyncEntry {
    std::uint64_t time, moof;
    std::uint32_t sample;
};
} // namespace

std::vector<std::byte> encode(Codec codec, std::span<const Sample> samples)
{
    if (samples.empty())
        reject("No samples");
    if (!samples.front().keyframe)
        reject("The first sample is not a keyframe");

    Configuration sets;
    std::vector<std::vector<Bytes>> payloads(samples.size());
    for (std::size_t i = 0; i < samples.size(); ++i) {
        for (const auto nal : nal_units(samples[i].access_unit)) {
            switch (kind(codec, nal)) {
            case NalKind::parameter_vps:
                if (i == 0)
                    add_unique(sets.vps, nal);
                break;
            case NalKind::parameter_sps:
                if (i == 0)
                    add_unique(sets.sps, nal);
                break;
            case NalKind::parameter_pps:
                if (i == 0)
                    add_unique(sets.pps, nal);
                break;
            case NalKind::delimiter:
                break;
            case NalKind::media:
                payloads[i].push_back(nal);
                break;
            }
        }
        if (payloads[i].empty())
            reject("Access unit without coded picture data");
    }
    if (sets.sps.empty() || sets.pps.empty() || (codec == Codec::h265 && sets.vps.empty()))
        reject("The keyframe carries no parameter sets");
    if (sets.sps.size() > 31 || sets.pps.size() > 255)
        reject("Too many parameter sets");
    const auto sps = rbsp(sets.sps.front());
    const auto picture = codec == Codec::h264 ? h264_picture(sps) : h265_picture(sps);
    if (!picture.width || !picture.height)
        reject("Empty picture size");
    if (picture.chroma_format > 3 || picture.luma_bit_depth > 15 || picture.chroma_bit_depth > 15)
        reject("Unsupported picture format");

    const auto track = timing(samples);
    const auto& times = track.samples;
    Output out;
    file_header(out, codec, sets, picture, sps, track);

    const auto fragment_ticks = static_cast<std::uint64_t>(fragment_duration.count()) * timescale / 1000;
    std::vector<SyncEntry> index;
    std::uint32_t sequence = 0;
    for (std::size_t first = 0; first < samples.size();) {
        auto last = first + 1;
        while (last < samples.size() && times[last].decode - times[first].decode < fragment_ticks)
            ++last;
        const auto moof = out.size();
        const auto moof_box = out.open("moof");
        auto box = out.open("mfhd", {0, 0});
        out.u32(++sequence);
        out.close(box);
        const auto traf = out.open("traf");
        // The data offsets are relative to the start of this moof.
        box = out.open("tfhd", {0, 0x02'0000});
        out.u32(track_id);
        out.close(box);
        box = out.open("tfdt", {1, 0});
        out.u64(times[first].decode);
        out.close(box);
        box = out.open("trun", {0, 0x00'0f01});
        out.u32(Output::checked(last - first));
        const auto data_offset = out.size();
        out.u32(0);
        std::size_t media_bytes = 0;
        for (auto i = first; i < last; ++i) {
            std::size_t size = 0;
            for (const auto nal : payloads[i])
                size += 4 + nal.size();
            media_bytes += size;
            out.u32(static_cast<std::uint32_t>(times[i].duration));
            out.u32(Output::checked(size));
            out.u32(samples[i].keyframe ? sync_sample_flags : delta_sample_flags);
            out.u32(times[i].composition_offset);
            if (samples[i].keyframe)
                index.push_back({times[i].decode + times[i].composition_offset, moof,
                                 static_cast<std::uint32_t>(i - first + 1)});
        }
        out.close(box);
        out.close(traf);
        out.close(moof_box);
        out.patch(data_offset, Output::checked(out.size() - moof + 8));
        const auto mdat = out.open("mdat");
        for (auto i = first; i < last; ++i) {
            for (const auto nal : payloads[i]) {
                out.u32(Output::checked(nal.size()));
                out.bytes(nal);
            }
        }
        out.close(mdat);
        if (out.size() - mdat != media_bytes + 8)
            reject("Media size mismatch");
        first = last;
    }

    const auto mfra = out.open("mfra");
    auto box = out.open("tfra", {1, 0});
    out.u32(track_id);
    // One byte for the traf and trun numbers, four bytes for the sample number.
    out.u32(0x03);
    out.u32(Output::checked(index.size()));
    for (const auto& entry : index) {
        out.u64(entry.time);
        out.u64(entry.moof);
        out.u8(1);
        out.u8(1);
        out.u32(entry.sample);
    }
    out.close(box);
    box = out.open("mfro", {0, 0});
    out.u32(Output::checked(out.size() - mfra + 4));
    out.close(box);
    out.close(mfra);
    return out.release();
}
} // namespace bacuri::vms::archive::fmp4
