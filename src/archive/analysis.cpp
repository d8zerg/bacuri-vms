#include "archive/analysis.hpp"
#include "archive/index_anchor.hpp"
#include "media/acceleration/codec_task.hpp"
#include "platform/scope_exit.hpp"
#include <gst/app/gstappsink.h>
#include <gst/video/video.h>
#include <chrono>
#include <cmath>
#include <algorithm>
#include <array>

namespace bacuri::vms::archive
{
namespace
{
using Image = std::array<unsigned char, std::size_t{preview_width} * preview_height>;
using Cells = std::array<unsigned char, std::size_t{grid_width} * grid_height>;
// The position of a pixel in an image stored row by row.
std::size_t at(int x, int y, int width)
{
    return static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x);
}
template <class T> auto release_gst(T* value)
{
    return std::unique_ptr<T, decltype(&gst_object_unref)>{value, gst_object_unref};
}
std::string encode(std::span<const unsigned char> bytes)
{
    std::unique_ptr<char, decltype(&g_free)> raw{g_base64_encode(bytes.data(), bytes.size()), g_free};
    return raw.get();
}
} // namespace
MotionSample motion_sample(std::span<const unsigned char> previous, std::span<const unsigned char> image,
                           std::int64_t offset)
{
    if (image.size() != Image{}.size() || (!previous.empty() && previous.size() != image.size()))
        throw std::invalid_argument("Invalid detector image");
    Cells cells{};
    if (!previous.empty())
        for (int gy = 0; gy < grid_height; ++gy)
            for (int gx = 0; gx < grid_width; ++gx) {
                unsigned sum{};
                for (int y = 0; y < 4; ++y)
                    for (int x = 0; x < 4; ++x) {
                        const auto index = at(gx * 4 + x, gy * 4 + y, preview_width);
                        sum += static_cast<unsigned>(std::abs(static_cast<int>(image[index]) - previous[index]));
                    }
                cells[at(gx, gy, grid_width)] = static_cast<unsigned char>((sum + 8) / 16);
            }
    return {offset, encode(cells), encode(image)};
}
void validate_analysis(const MotionIndex& index, std::int64_t duration)
{
    if (index.samples.empty() || index.samples.size() > 256 || index.frames.empty() || index.frames.size() > 4096)
        throw std::invalid_argument("Invalid motion index");
    std::int64_t last = -1;
    for (auto t : index.frames) {
        if (t <= last || t >= duration)
            throw std::invalid_argument("Invalid indexed timestamp");
        last = t;
    }
    last = -1;
    for (const auto& sample : index.samples) {
        if (sample.backend != "cpu" && sample.backend != "nvidia" && sample.backend != "vaapi")
            throw std::invalid_argument("Invalid analysis backend");
        if (sample.offset <= last || sample.offset >= duration ||
            !std::ranges::binary_search(index.frames, sample.offset))
            throw std::invalid_argument("Invalid motion sample");
        last = sample.offset;
        auto pixels = [](const std::string& bytes, std::size_t length) {
            if (bytes.size() != length ||
                bytes.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/") !=
                    bytes.npos)
                throw std::invalid_argument("Invalid detector pixels");
        };
        pixels(sample.cells, 256);
        pixels(sample.image, 4096);
    }
}
namespace
{
MotionIndex index_block(const std::filesystem::path& path, std::int64_t duration, const std::string& codec,
                        const IndexAnchor& anchor, acceleration::CodecTask& task,
                        const acceleration::CodecPath& device)
{
    using namespace std::chrono_literals;
    gst_init(nullptr, nullptr);
    GError* raw{};
    const auto spec = "filesrc name=file ! qtdemux name=demux demux.video_0 ! " + codec + "parse ! " +
                      device.decoder(codec) +
                      " ! videoconvert ! videoscale ! "
                      "video/x-raw,format=GRAY8,width=64,height=48,pixel-aspect-ratio=1/1 ! "
                      "appsink name=frames max-buffers=2 sync=false";
    auto pipe = release_gst(gst_parse_launch(spec.c_str(), &raw));
    std::unique_ptr<GError, decltype(&g_error_free)> error{raw, g_error_free};
    if (error || !pipe)
        throw acceleration::CodecFailure("Index decoder unavailable");
    platform::ScopeExit stop{[&pipe] { gst_element_set_state(pipe.get(), GST_STATE_NULL); }};
    auto input = release_gst(gst_bin_get_by_name(GST_BIN(pipe.get()), "file"));
    auto sink = release_gst(gst_bin_get_by_name(GST_BIN(pipe.get()), "frames"));
    auto bus = release_gst(gst_element_get_bus(pipe.get()));
    g_object_set(input.get(), "location", path.c_str(), nullptr);
    if (gst_element_set_state(pipe.get(), GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE)
        throw acceleration::CodecFailure("Index decoder failed");
    MotionIndex result;
    const auto deadline = std::chrono::steady_clock::now() + 30s;
    std::int64_t last_sample = -250000;
    Image previous{};
    for (;;) {
        task.check(device);
        if (std::chrono::steady_clock::now() > deadline)
            throw acceleration::CodecFailure("Index decoder timeout");
        std::unique_ptr<GstSample, decltype(&gst_sample_unref)> sample{
            gst_app_sink_try_pull_sample(GST_APP_SINK(sink.get()), GST_SECOND / 10), gst_sample_unref};
        if (!sample) {
            if (auto* message = gst_bus_pop_filtered(bus.get(), GST_MESSAGE_ERROR)) {
                platform::ScopeExit release{[&] { gst_message_unref(message); }};
                acceleration::pipeline_error(message);
            }
            if (gst_app_sink_is_eos(GST_APP_SINK(sink.get())))
                break;
            continue;
        }
        auto* buffer = gst_sample_get_buffer(sample.get());
        if (!buffer || !GST_BUFFER_PTS_IS_VALID(buffer))
            throw std::runtime_error("Missing presentation time");
        const auto pts = static_cast<std::int64_t>(GST_BUFFER_PTS(buffer) / GST_USECOND);
        const auto offset = pts - anchor.pts_us;
        if (result.frames.empty() && offset != 0)
            throw std::runtime_error("Archive analysis does not start at its keyframe");
        if (offset >= duration)
            continue;
        if (result.frames.size() >= 4096)
            throw std::runtime_error("Index frame limit");
        if (result.frames.empty())
            task.used(device);
        result.frames.push_back(offset);
        if (offset - last_sample < 250000)
            continue;
        Image current{};
        if (gst_buffer_extract(buffer, 0, current.data(), current.size()) != current.size())
            throw std::runtime_error("Incomplete detector image");
        result.samples.push_back(motion_sample(last_sample < 0 ? std::span<const unsigned char>{}
                                                               : std::span<const unsigned char>{previous},
                                               current, offset));
        if (!offset)
            result.samples.back().image = anchor.preview;
        result.samples.back().backend = device.backend;
        previous = current;
        last_sample = offset;
    }
    validate_analysis(result, duration);
    task.used(device);
    return result;
}
} // namespace
MotionIndex analyze_recording(const std::filesystem::path& path, std::int64_t duration, const std::string& codec)
{
    const auto anchor = index_anchor(path, codec);
    acceleration::CodecTask task{"archive_analysis"};
    return acceleration::process_block(task, [&](const acceleration::CodecPath& device) {
        return index_block(path, duration, codec, anchor, task, device);
    });
}
} // namespace bacuri::vms::archive
namespace bacuri::vms::archive
{
namespace
{
std::vector<unsigned char> pixels(const std::string& bytes, std::size_t size)
{
    if (bytes.size() != (size + 2) / 3 * 4 ||
        bytes.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/=") != bytes.npos)
        throw std::invalid_argument("Invalid detector pixels");
    gsize length{};
    std::unique_ptr<guchar, decltype(&g_free)> raw{g_base64_decode(bytes.c_str(), &length), g_free};
    if (length != size)
        throw std::invalid_argument("Invalid detector grid size");
    return {raw.get(), raw.get() + size};
}
bool intersects(const std::vector<MotionArea>& areas, int x, int y, int width, int height)
{
    return std::ranges::any_of(areas, [&](const auto& r) {
        return r.overlaps(double(x) / width, double(y) / height, 1. / width, 1. / height);
    });
}
} // namespace
Movement measure_motion(const std::string& bytes, int threshold, const std::vector<MotionArea>& include,
                        const std::vector<MotionArea>& exclude)
{
    auto grid = pixels(bytes, std::size_t{grid_width} * grid_height);
    Movement result;
    for (int y = 0; y < grid_height; ++y)
        for (int x = 0; x < grid_width; ++x) {
            if (grid[at(x, y, grid_width)] < threshold ||
                (!include.empty() && !intersects(include, x, y, grid_width, grid_height)) ||
                intersects(exclude, x, y, grid_width, grid_height))
                continue;
            ++result.cells;
            result.x += (x + .5) / grid_width;
            result.y += (y + .5) / grid_height;
        }
    if (result.cells) {
        result.x /= result.cells;
        result.y /= result.cells;
    }
    return result;
}
std::string mask_preview(const std::string& bytes, const std::vector<MotionArea>& exclude)
{
    if (exclude.empty())
        return bytes;
    auto image = pixels(bytes, std::size_t{preview_width} * preview_height);
    for (int y = 0; y < preview_height; ++y)
        for (int x = 0; x < preview_width; ++x)
            if (intersects(exclude, x, y, preview_width, preview_height))
                image[at(x, y, preview_width)] = 0;
    return encode(image);
}
} // namespace bacuri::vms::archive
