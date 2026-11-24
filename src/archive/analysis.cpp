#include "archive/analysis.hpp"
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
template <class T> auto release_gst(T* value)
{
    return std::unique_ptr<T, decltype(&gst_object_unref)>{value, gst_object_unref};
}
std::string encode(std::span<const unsigned char> bytes)
{
    std::unique_ptr<char, decltype(&g_free)> raw{g_base64_encode(bytes.data(), bytes.size()), g_free};
    return raw.get();
}
struct Stop {
    GstElement* pipeline;
    ~Stop()
    {
        gst_element_set_state(pipeline, GST_STATE_NULL);
    }
};
} // namespace
Json motion_sample(std::span<const unsigned char> previous, std::span<const unsigned char> image,
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
                        const auto index = (gy * 4 + y) * preview_width + gx * 4 + x;
                        sum += std::abs(static_cast<int>(image[index]) - previous[index]);
                    }
                cells[gy * grid_width + gx] = static_cast<unsigned char>((sum + 8) / 16);
            }
    return {{"at", offset}, {"cells", encode(cells)}, {"image", encode(image)}};
}
void validate_analysis(const Json& data, std::int64_t duration)
{
    if (!data.is_object() || data.size() != 2 || !data.at("samples").is_array() || data.at("samples").empty() ||
        data.at("samples").size() > 256 || !data.at("frames").is_array() || data.at("frames").empty() ||
        data.at("frames").size() > 4096)
        throw std::invalid_argument("Invalid motion index");
    std::int64_t last = -1;
    for (const auto& t : data.at("frames")) {
        if (!t.is_number_integer() || t <= last || t >= duration)
            throw std::invalid_argument("Invalid indexed timestamp");
        last = t.get<std::int64_t>();
    }
    last = -1;
    for (const auto& s : data.at("samples")) {
        if (s.size() != 3 || !s.at("at").is_number_integer() || s.at("at") <= last || s.at("at") >= duration ||
            !std::binary_search(data.at("frames").begin(), data.at("frames").end(), s.at("at")))
            throw std::invalid_argument("Invalid motion sample");
        last = s.at("at");
        for (auto [name, length] : {std::pair{"cells", 256u}, {"image", 4096u}}) {
            const auto& bytes = s.at(name).get_ref<const std::string&>();
            if (bytes.size() != length ||
                bytes.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/") !=
                    bytes.npos)
                throw std::invalid_argument("Invalid detector pixels");
        }
    }
}
Json analyze_recording(const std::filesystem::path& path, std::int64_t duration)
{
    using namespace std::chrono_literals;
    gst_init(nullptr, nullptr);
    GError* raw{};
    auto pipe =
        release_gst(gst_parse_launch("filesrc name=file ! qtdemux ! decodebin ! videoconvert ! videoscale ! "
                                     "video/x-raw,format=GRAY8,width=64,height=48,pixel-aspect-ratio=1/1 ! "
                                     "appsink name=frames max-buffers=2 sync=false",
                                     &raw));
    std::unique_ptr<GError, decltype(&g_error_free)> error{raw, g_error_free};
    if (error || !pipe)
        throw std::runtime_error("Index decoder unavailable");
    Stop stop{pipe.get()};
    auto input = release_gst(gst_bin_get_by_name(GST_BIN(pipe.get()), "file"));
    auto sink = release_gst(gst_bin_get_by_name(GST_BIN(pipe.get()), "frames"));
    auto bus = release_gst(gst_element_get_bus(pipe.get()));
    g_object_set(input.get(), "location", path.c_str(), nullptr);
    if (gst_element_set_state(pipe.get(), GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE)
        throw std::runtime_error("Index decoder failed");
    Json result{{"frames", Json::array()}, {"samples", Json::array()}};
    const auto deadline = std::chrono::steady_clock::now() + 30s;
    std::int64_t base = -1, last_sample = -250000;
    Image previous{};
    for (;;) {
        if (std::chrono::steady_clock::now() > deadline)
            throw std::runtime_error("Index decoder timeout");
        std::unique_ptr<GstSample, decltype(&gst_sample_unref)> sample{
            gst_app_sink_try_pull_sample(GST_APP_SINK(sink.get()), GST_SECOND / 10), gst_sample_unref};
        if (!sample) {
            if (auto* message = gst_bus_pop_filtered(bus.get(), GST_MESSAGE_ERROR)) {
                gst_message_unref(message);
                throw std::runtime_error("Incomplete indexed stream");
            }
            if (gst_app_sink_is_eos(GST_APP_SINK(sink.get())))
                break;
            continue;
        }
        auto* buffer = gst_sample_get_buffer(sample.get());
        if (!buffer || !GST_BUFFER_PTS_IS_VALID(buffer))
            throw std::runtime_error("Missing presentation time");
        const auto pts = static_cast<std::int64_t>(GST_BUFFER_PTS(buffer) / GST_USECOND);
        if (base < 0)
            base = pts;
        const auto offset = pts - base;
        if (offset >= duration)
            continue;
        if (result["frames"].size() >= 4096)
            throw std::runtime_error("Index frame limit");
        result["frames"].push_back(offset);
        if (offset - last_sample < 250000)
            continue;
        Image current{};
        if (gst_buffer_extract(buffer, 0, current.data(), current.size()) != current.size())
            throw std::runtime_error("Incomplete detector image");
        result["samples"].push_back(motion_sample(last_sample < 0 ? std::span<const unsigned char>{}
                                                                  : std::span<const unsigned char>{previous},
                                                  current, offset));
        previous = current;
        last_sample = offset;
    }
    validate_analysis(result, duration);
    return result;
}
} // namespace bacuri::vms::archive
namespace bacuri::vms::archive
{
MotionArea MotionArea::parse(const Json& item)
{
    MotionArea r{item.at("left"), item.at("top"), item.at("right"), item.at("bottom")};
    if (item.size() != 4 ||
        !(r.left >= 0 && r.top >= 0 && r.right <= 1 && r.bottom <= 1 && r.left < r.right && r.top < r.bottom))
        throw std::invalid_argument("Invalid analysis area");
    return r;
}
bool MotionArea::overlaps(double x, double y, double w, double h) const
{
    return x < right && x + w > left && y < bottom && y + h > top;
}
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
            if (grid[y * grid_width + x] < threshold ||
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
                image[y * preview_width + x] = 0;
    return encode(image);
}
} // namespace bacuri::vms::archive
