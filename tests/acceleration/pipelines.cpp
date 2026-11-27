#include "archive/analysis_json.hpp"
#include "media/clip.hpp"
#include "media/live_codec.hpp"
#include "platform/scope_exit.hpp"
#include <gst/app/gstappsink.h>
#include <fstream>
#include <iostream>
#include <thread>

namespace
{
using namespace bacuri::vms;
using namespace std::chrono_literals;
template <class T> auto own(T* p)
{
    return std::unique_ptr<T, decltype(&gst_object_unref)>{p, gst_object_unref};
}
const domain::ImageRegions regions{{0.25, 0.25, 0.75, 0.75}};

void live(const std::string& kind, const std::string& codec, const char* file, std::ofstream& data)
{
    const bool long_gop = kind == "long_gop";
    const auto spec = "filesrc name=file ! qtdemux ! " + codec + "parse config-interval=-1 ! video/x-" + codec +
                      ",stream-format=byte-stream,alignment=au ! appsink name=frames max-buffers=4 sync=" +
                      (long_gop ? "false" : "true");
    auto pipe = own(gst_parse_launch(spec.c_str(), nullptr));
    auto src = own(gst_bin_get_by_name(GST_BIN(pipe.get()), "file"));
    auto sink = own(gst_bin_get_by_name(GST_BIN(pipe.get()), "frames"));
    g_object_set(src.get(), "location", file, nullptr);
    platform::ScopeExit stop{[&] { gst_element_set_state(pipe.get(), GST_STATE_NULL); }};
    data.exceptions(std::ios::badbit | std::ios::failbit);
    media::LiveCodec transform{
        codec, media::PrivacyMask{(kind == "live_mask" || long_gop) ? regions : domain::ImageRegions{}},
        kind == "live_analysis" ? media::LiveCodec::Format::gray : media::LiveCodec::Format::h264};
    unsigned count{};
    const auto consume = [&](std::string_view bytes, bool, GstClockTime pts, const std::string& backend) {
        if (pts != count * (GST_SECOND / 25))
            throw std::runtime_error("Unexpected frame timestamp");
        data.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        if (++count == 5)
            std::cout << "started " << backend << '\n' << std::flush;
    };
    gst_element_set_state(pipe.get(), GST_STATE_PLAYING);
    while (auto* raw = gst_app_sink_try_pull_sample(GST_APP_SINK(sink.get()), 3 * GST_SECOND)) {
        std::unique_ptr<GstSample, decltype(&gst_sample_unref)> sample{raw, gst_sample_unref};
        auto* buf = gst_sample_get_buffer(raw);
        GstMapInfo map{};
        if (!gst_buffer_map(buf, &map, GST_MAP_READ))
            throw std::runtime_error("Unreadable input");
        platform::ScopeExit unmap{[&] { gst_buffer_unmap(buf, &map); }};
        if (long_gop)
            std::this_thread::sleep_for(2ms);
        transform.push({reinterpret_cast<const char*>(map.data), map.size},
                       !GST_BUFFER_FLAG_IS_SET(buf, GST_BUFFER_FLAG_DELTA_UNIT), GST_BUFFER_PTS(buf), consume);
    }
    transform.finish(consume);
    if (!gst_app_sink_is_eos(GST_APP_SINK(sink.get())) || count != (long_gop ? 936u : 96u))
        throw std::runtime_error("Incomplete reference");
}
} // namespace
int main(int argc, char** argv)
try {
    if (argc != 5)
        return 2;
    gst_init(nullptr, nullptr);
    const std::string kind = argv[1], codec = argv[2];
    if (kind == "archive") {
        auto index = archive::analyze_recording(argv[3], 3840000, codec);
        std::ofstream{argv[4]} << nlohmann::json(index);
    } else if (kind == "export") {
        const auto clip = archive::select_frames(argv[3], argv[4], 200000, 3200000, false, regions, codec);
        if (clip.count != 75 || clip.begin_us != 200000 || clip.end_us != 3200000)
            throw std::runtime_error("Incorrect selected interval");
    } else {
        std::ofstream data{argv[4], std::ios::binary};
        live(kind, codec, argv[3], data);
    }
    return 0;
} catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
}
