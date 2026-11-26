#include "archive/fmp4.hpp"
#include "archive/mp4.hpp"

#include <fcntl.h>
#include <gst/app/gstappsink.h>
#include <gst/gst.h>
#include <gtest/gtest.h>
#include <unistd.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>

namespace bacuri::vms::archive
{
namespace
{
namespace fs = std::filesystem;

struct Encoded {
    std::vector<std::byte> bytes;
    std::uint64_t pts_ns;
    bool keyframe;
};

using Element = std::unique_ptr<GstElement, decltype(&gst_object_unref)>;

Element launch(const std::string& description)
{
    gst_init(nullptr, nullptr);
    GError* error{};
    Element pipeline{gst_parse_launch(description.c_str(), &error), gst_object_unref};
    if (error || !pipeline) {
        std::string reason = error ? error->message : "Missing pipeline";
        g_clear_error(&error);
        throw std::runtime_error(reason);
    }
    return pipeline;
}

// Encodes `count` frames at 25 frames per second in decode order.
std::vector<Encoded> encode_frames(const std::string& encoder, int width, int height, int count)
{
    auto pipeline =
        launch("videotestsrc num-buffers=" + std::to_string(count) +
               " ! video/x-raw,format=I420,framerate=25/1,width=" + std::to_string(width) +
               ",height=" + std::to_string(height) + " ! " + encoder + " ! appsink name=sample sync=false");
    Element sink{gst_bin_get_by_name(GST_BIN(pipeline.get()), "sample"), gst_object_unref};
    gst_element_set_state(pipeline.get(), GST_STATE_PLAYING);
    std::vector<Encoded> frames;
    while (static_cast<int>(frames.size()) < count) {
        auto* sample = gst_app_sink_try_pull_sample(GST_APP_SINK(sink.get()), 10 * GST_SECOND);
        if (!sample)
            break;
        auto* buffer = gst_sample_get_buffer(sample);
        Encoded frame{std::vector<std::byte>(gst_buffer_get_size(buffer)), GST_BUFFER_PTS(buffer),
                      !GST_BUFFER_FLAG_IS_SET(buffer, GST_BUFFER_FLAG_DELTA_UNIT)};
        gst_buffer_extract(buffer, 0, frame.bytes.data(), frame.bytes.size());
        gst_sample_unref(sample);
        frames.push_back(std::move(frame));
    }
    gst_element_set_state(pipeline.get(), GST_STATE_NULL);
    if (static_cast<int>(frames.size()) != count)
        throw std::runtime_error("Missing encoded frames");
    // Times relative to the first presented frame.
    const auto base = std::ranges::min(frames, {}, &Encoded::pts_ns).pts_ns;
    for (auto& frame : frames)
        frame.pts_ns -= base;
    return frames;
}

std::vector<fmp4::Sample> samples(const std::vector<Encoded>& frames)
{
    std::vector<fmp4::Sample> result;
    result.reserve(frames.size());
    for (const auto& frame : frames)
        result.push_back({frame.bytes, frame.pts_ns, 40'000'000, frame.keyframe});
    return result;
}

class TemporaryFile
{
  public:
    explicit TemporaryFile(const std::vector<std::byte>& data)
        : path_{fs::temp_directory_path() / ("bacuri-fmp4-" + std::to_string(std::random_device{}()) + ".mp4")}
    {
        std::ofstream file{path_, std::ios::binary};
        file.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    }
    ~TemporaryFile()
    {
        fs::remove(path_);
    }
    TemporaryFile(const TemporaryFile&) = delete;
    TemporaryFile& operator=(const TemporaryFile&) = delete;
    const fs::path& path() const
    {
        return path_;
    }

  private:
    fs::path path_;
};

struct Decoded {
    std::vector<std::uint64_t> pts_ns;
    int width{}, height{};
};

// Decodes the file with the demuxer and decoders of GStreamer, independent of the writer.
Decoded decode(const fs::path& path)
{
    auto pipeline = launch("filesrc location=" + path.string() +
                           " ! qtdemux ! decodebin ! videoconvert ! video/x-raw,format=I420 ! appsink name=frames "
                           "sync=false");
    Element sink{gst_bin_get_by_name(GST_BIN(pipeline.get()), "frames"), gst_object_unref};
    gst_element_set_state(pipeline.get(), GST_STATE_PLAYING);
    Decoded result;
    while (auto* sample = gst_app_sink_try_pull_sample(GST_APP_SINK(sink.get()), 10 * GST_SECOND)) {
        const auto* structure = gst_caps_get_structure(gst_sample_get_caps(sample), 0);
        gst_structure_get_int(structure, "width", &result.width);
        gst_structure_get_int(structure, "height", &result.height);
        // The position in the presentation, which includes the edit list applied through the segment.
        result.pts_ns.push_back(gst_segment_to_stream_time(gst_sample_get_segment(sample), GST_FORMAT_TIME,
                                                           GST_BUFFER_PTS(gst_sample_get_buffer(sample))));
        gst_sample_unref(sample);
    }
    gst_element_set_state(pipeline.get(), GST_STATE_NULL);
    return result;
}

std::size_t count_boxes(const std::vector<std::byte>& data, std::string_view type)
{
    std::size_t count = 0;
    for (std::size_t i = 4; i + 4 <= data.size(); ++i)
        count += std::equal(type.begin(), type.end(), reinterpret_cast<const char*>(data.data() + i));
    return count;
}

void expect_valid_layout(const TemporaryFile& file, std::size_t size)
{
    const int fd = open(file.path().c_str(), O_RDONLY | O_CLOEXEC);
    ASSERT_GE(fd, 0);
    EXPECT_NO_THROW(verify_fragment_layout(fd, size));
    close(fd);
}

void expect_frames_at(const Decoded& decoded, const std::vector<Encoded>& frames)
{
    std::vector<std::uint64_t> expected;
    expected.reserve(frames.size());
    for (const auto& frame : frames)
        expected.push_back(frame.pts_ns);
    std::ranges::sort(expected);
    ASSERT_EQ(decoded.pts_ns.size(), expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i) {
        // One tick of the 90 kHz media clock.
        EXPECT_NEAR(static_cast<double>(decoded.pts_ns[i]), static_cast<double>(expected[i]), 11'112.0) << i;
    }
}

TEST(FragmentedMp4, H264WithCroppingDecodesEveryFrameAtItsTime)
{
    const auto frames =
        encode_frames("x264enc tune=zerolatency key-int-max=1000 ! video/x-h264,stream-format=byte-stream,"
                      "alignment=au",
                      100, 60, 30);
    const auto media = fmp4::encode(fmp4::Codec::h264, samples(frames));
    const TemporaryFile file{media};

    expect_valid_layout(file, media.size());
    // 30 frames of 40 ms form fragments of 500 ms.
    EXPECT_EQ(count_boxes(media, "moof"), 3U);
    EXPECT_EQ(count_boxes(media, "avcC"), 1U);
    // Frames presented in decode order need no edit list.
    EXPECT_EQ(count_boxes(media, "elst"), 0U);
    const auto decoded = decode(file.path());
    EXPECT_EQ(decoded.width, 100);
    EXPECT_EQ(decoded.height, 60);
    expect_frames_at(decoded, frames);
}

TEST(FragmentedMp4, H265DecodesEveryFrameAtItsTime)
{
    const auto frames = encode_frames("x265enc tune=zerolatency speed-preset=ultrafast key-int-max=1000 ! "
                                      "video/x-h265,stream-format=byte-stream,alignment=au",
                                      64, 48, 20);
    const auto media = fmp4::encode(fmp4::Codec::h265, samples(frames));
    const TemporaryFile file{media};

    expect_valid_layout(file, media.size());
    EXPECT_EQ(count_boxes(media, "hvcC"), 1U);
    const auto decoded = decode(file.path());
    EXPECT_EQ(decoded.width, 64);
    EXPECT_EQ(decoded.height, 48);
    expect_frames_at(decoded, frames);
}

TEST(FragmentedMp4, ReorderedFramesKeepTheirPresentationTimes)
{
    const auto frames = encode_frames("x264enc speed-preset=veryfast bframes=2 b-adapt=false key-int-max=1000 ! "
                                      "video/x-h264,stream-format=byte-stream,alignment=au",
                                      64, 48, 25);
    ASSERT_FALSE(std::ranges::is_sorted(frames, {}, &Encoded::pts_ns)) << "The encoder produced no B-frames";
    const auto media = fmp4::encode(fmp4::Codec::h264, samples(frames));
    const TemporaryFile file{media};

    expect_valid_layout(file, media.size());
    EXPECT_EQ(count_boxes(media, "elst"), 1U);
    expect_frames_at(decode(file.path()), frames);
}

TEST(FragmentedMp4, RejectsSamplesThatCannotStartABlock)
{
    const auto frames =
        encode_frames("x264enc tune=zerolatency key-int-max=1000 ! video/x-h264,stream-format=byte-stream,"
                      "alignment=au",
                      32, 32, 2);
    EXPECT_THROW(fmp4::encode(fmp4::Codec::h264, samples({frames[1]})), std::invalid_argument);
    EXPECT_THROW(fmp4::encode(fmp4::Codec::h264, {}), std::invalid_argument);

    // An IDR slice without the parameter sets that describe it.
    const std::vector<std::byte> slice{std::byte{0}, std::byte{0}, std::byte{1}, std::byte{0x65}, std::byte{0x88}};
    const std::vector<fmp4::Sample> bare{{slice, 0, 40'000'000, true}};
    EXPECT_THROW(fmp4::encode(fmp4::Codec::h264, bare), std::invalid_argument);

    const std::vector<std::byte> no_start_code{std::byte{0x65}, std::byte{0x88}};
    const std::vector<fmp4::Sample> raw{{no_start_code, 0, 40'000'000, true}};
    EXPECT_THROW(fmp4::encode(fmp4::Codec::h264, raw), std::invalid_argument);
}
} // namespace
} // namespace bacuri::vms::archive
