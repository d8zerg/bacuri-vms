#include "media/acceleration/probe.hpp"
#include "media/acceleration/library.hpp"
#include "media/privacy.hpp"
#include "archive/analysis.hpp"
#include "platform/scope_exit.hpp"
#include <gst/app/gstappsrc.h>
#include <gst/app/gstappsink.h>
#include <algorithm>
#include <array>
#include <fstream>

namespace bacuri::vms::acceleration
{
namespace
{
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;
using Element = std::unique_ptr<GstElement, decltype(&gst_object_unref)>;
using Buffer = std::unique_ptr<GstBuffer, decltype(&gst_buffer_unref)>;
using Caps = std::unique_ptr<GstCaps, decltype(&gst_caps_unref)>;
constexpr unsigned frames = 16;
struct Pipeline {
    Element pipe{nullptr, gst_object_unref};
    explicit Pipeline(const std::string& description)
    {
        GError* error{};
        pipe.reset(gst_parse_launch(description.c_str(), &error));
        if (error || !pipe) {
            if (error)
                g_error_free(error);
            throw std::runtime_error("Video pipeline unavailable");
        }
    }
    ~Pipeline()
    {
        gst_element_set_state(pipe.get(), GST_STATE_NULL);
    }
    Pipeline(const Pipeline&) = delete;
    Pipeline& operator=(const Pipeline&) = delete;
    Element element(const char* name)
    {
        return Element{gst_bin_get_by_name(GST_BIN(pipe.get()), name), gst_object_unref};
    }
    void play()
    {
        if (gst_element_set_state(pipe.get(), GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE)
            throw std::runtime_error("Cannot start video pipeline");
    }
};
std::string encoder(const std::string& name)
{
    if (name == "x264enc")
        return name + " threads=2 speed-preset=ultrafast tune=zerolatency";
    if (name == "x265enc")
        return name + " speed-preset=ultrafast tune=zerolatency option-string=pools=none:frame-threads=2";
    return name;
}
std::string decoder(const std::string& name)
{
    return name.starts_with("avdec_") ? name + " max-threads=2" : name;
}
std::string source()
{
    return "videotestsrc num-buffers=" + std::to_string(frames) +
           " pattern=ball ! video/x-raw,format=I420,width=1920,height=1080,framerate=25/1";
}
const std::string sink = " ! appsink name=out sync=false max-buffers=2";

struct Reference {
    Caps caps{nullptr, gst_caps_unref};
    std::vector<Buffer> buffers;
};
template <class F> unsigned receive(Pipeline& pipe, F consume)
{
    auto out = pipe.element("out");
    unsigned count{};
    while (auto* raw = gst_app_sink_try_pull_sample(GST_APP_SINK(out.get()), 2 * GST_SECOND)) {
        std::unique_ptr<GstSample, decltype(&gst_sample_unref)> sample{raw, gst_sample_unref};
        consume(sample.get(), count++);
    }
    if (!gst_app_sink_is_eos(GST_APP_SINK(out.get())) || !count)
        throw std::runtime_error("Video sample did not finish");
    return count;
}
Reference reference(const std::string& codec)
{
    Pipeline pipe{source() + " ! " + encoder(codec == "h264" ? "x264enc" : "x265enc") + " ! video/x-" + codec +
                  ",profile=main ! " + codec + "parse ! video/x-" + codec +
                  ",stream-format=byte-stream,alignment=au" + sink};
    pipe.play();
    Reference ref;
    if (receive(pipe, [&](GstSample* s, unsigned) {
            if (!ref.caps)
                ref.caps.reset(gst_caps_ref(gst_sample_get_caps(s)));
            ref.buffers.emplace_back(gst_buffer_ref(gst_sample_get_buffer(s)), gst_buffer_unref);
        }) != frames)
        throw std::runtime_error("Invalid video reference");
    return ref;
}
bool foreign_video(const Json& dev)
{
    if (dev.at("backend") != "nvidia")
        return false;
    Library nvml{"libnvidia-ml.so.1"};
    if (!nvml || nvml.symbol<int()>("nvmlInit_v2")() != 0)
        return false;
    platform::ScopeExit shutdown{[&] { nvml.symbol<int()>("nvmlShutdown")(); }};
    void* handle{};
    const auto pci = dev.at("id").get<std::string>().substr(7);
    if (nvml.symbol<int(const char*, void**)>("nvmlDeviceGetHandleByPciBusId_v2")(pci.c_str(), &handle))
        return false;
    unsigned sessions{}, fps{}, latency{}, util{}, period{};
    return (!nvml.symbol<int(void*, unsigned*, unsigned*, unsigned*)>("nvmlDeviceGetEncoderStats")(
                handle, &sessions, &fps, &latency) &&
            sessions > 0) ||
           (!nvml.symbol<int(void*, unsigned*, unsigned*)>("nvmlDeviceGetDecoderUtilization")(handle, &util,
                                                                                              &period) &&
            util > 0);
}
Json sessions(const std::string& enc)
{
    // Hold each encoder at preroll with a real encoded buffer until the last attempt has finished.
    constexpr unsigned ceiling = 16;
    std::vector<std::unique_ptr<Pipeline>> held;
    while (held.size() < ceiling) {
        auto p = std::make_unique<Pipeline>(
            "videotestsrc ! video/x-raw,width=320,height=180,framerate=25/1 ! videoconvert ! " + encoder(enc) +
            " ! fakesink");
        if (gst_element_set_state(p->pipe.get(), GST_STATE_PAUSED) == GST_STATE_CHANGE_FAILURE ||
            gst_element_get_state(p->pipe.get(), nullptr, nullptr, GST_SECOND) != GST_STATE_CHANGE_SUCCESS)
            break;
        held.push_back(std::move(p));
    }
    return {{"state", held.empty() ? "failed" : "measured"},
            {"verified_sessions", held.size()},
            {"probe_ceiling", ceiling},
            {"reached_ceiling", held.size() == ceiling}};
}
// Device-wide usage while a reference pipeline is resident also accounts for the driver and its context.
class DeviceMemory
{
  public:
    explicit DeviceMemory(const Json& device)
    {
        if (device.at("backend") == "vaapi") {
            const auto name = device.at("id").get<std::string>().substr(6);
            counter_ = std::filesystem::path{"/sys/class/drm"} / name / "device/mem_info_vram_used";
        } else if (device.at("backend") == "nvidia") {
            nvml_ = std::make_unique<Library>("libnvidia-ml.so.1");
            if (*nvml_ && nvml_->symbol<int()>("nvmlInit_v2")() == 0) {
                initialized_ = true;
                const auto pci = device.at("id").get<std::string>().substr(7);
                if (nvml_->symbol<int(const char*, void**)>("nvmlDeviceGetHandleByPciBusId_v2")(pci.c_str(),
                                                                                                &handle_))
                    handle_ = nullptr;
            }
        }
    }
    ~DeviceMemory()
    {
        if (initialized_)
            nvml_->symbol<int()>("nvmlShutdown")();
    }
    DeviceMemory(const DeviceMemory&) = delete;
    DeviceMemory& operator=(const DeviceMemory&) = delete;
    std::uint64_t read() const
    {
        if (handle_) {
            struct Memory {
                unsigned long long total, free, used;
            } memory{};
            if (nvml_->symbol<int(void*, Memory*)>("nvmlDeviceGetMemoryInfo")(handle_, &memory) == 0)
                return memory.used;
        }
        if (!counter_.empty()) {
            std::uint64_t bytes{};
            std::ifstream input{counter_};
            if (input >> bytes)
                return bytes;
        }
        return 0;
    }

  private:
    std::unique_ptr<Library> nvml_;
    void* handle_{};
    bool initialized_{};
    std::filesystem::path counter_;
};
Json video_sample(const Json& request)
{
    const auto& dev = request.at("device");
    const auto codec = request.at("codec").get<std::string>(), op = request.at("operation").get<std::string>();
    const bool keyframes = op == "keyframe" || op == "thumbnail";
    const bool encoding = op == "encode" || op == "sessions";
    const bool transcode = op == "mask" || op == "profile" || op == "export";
    const auto& factories = dev.at("codecs");
    const auto key = codec + (encoding ? "_enc" : "_dec");
    if (!factories.contains(key) || (transcode && !factories.contains("h264_enc")))
        return {{"state", "unsupported"}};
    auto factory = factories.at(key).get<std::string>();
    if (op == "sessions")
        return sessions(factory);
    std::string line;
    Reference ref;
    if (encoding)
        line = source() + " ! videoconvert ! " + encoder(factory);
    else {
        ref = reference(codec);
        line = "appsrc name=input format=time ! " + codec + "parse ! " + decoder(factory);
        if (op == "profile")
            line += " ! videoconvertscale ! video/x-raw,width=1280,height=720";
        if (op == "mask" || op == "export")
            line += " ! videoconvert ! video/x-raw,format=I420 ! identity name=mask";
        if (transcode)
            line += " ! videoconvert ! " + encoder(factories.at("h264_enc").get<std::string>());
        if (op == "thumbnail" || op == "analysis")
            line += " ! videoconvertscale ! video/x-raw,format=GRAY8,width=64,height=48";
    }
    Pipeline pipe{line + sink};
    media::PrivacyMask mask{domain::ImageRegions{{0.25, 0.25, 0.75, 0.75}}};
    if (op == "mask" || op == "export")
        mask.attach(pipe.element("mask").get());
    auto input = pipe.element("input");
    if (input)
        gst_app_src_set_caps(GST_APP_SRC(input.get()), ref.caps.get());
    DeviceMemory memory{dev};
    std::uint64_t peak{};
    const auto started = Clock::now();
    pipe.play();
    if (input) {
        for (const auto& buffer : ref.buffers) {
            if (keyframes && GST_BUFFER_FLAG_IS_SET(buffer.get(), GST_BUFFER_FLAG_DELTA_UNIT))
                continue;
            if (gst_app_src_push_buffer(GST_APP_SRC(input.get()), gst_buffer_ref(buffer.get())) != GST_FLOW_OK)
                throw std::runtime_error("Cannot supply video reference");
        }
        gst_app_src_end_of_stream(GST_APP_SRC(input.get()));
    }
    std::array<unsigned char, 3072> previous{}, pixels{};
    const auto count = receive(pipe, [&](GstSample* sample, unsigned i) {
        if (i == 0 || i + 1 == frames)
            peak = std::max(peak, memory.read());
        if (op == "analysis") {
            if (gst_buffer_extract(gst_sample_get_buffer(sample), 0, pixels.data(), pixels.size()) !=
                pixels.size())
                throw std::runtime_error("Incomplete analysis frame");
            archive::motion_sample(i ? std::span<const unsigned char>{previous} : std::span<const unsigned char>{},
                                   pixels, static_cast<std::int64_t>(i) * 40000);
            previous = pixels;
        }
    });
    if (count != (keyframes ? 1u : frames))
        throw std::runtime_error("Unexpected frame count");
    auto us = std::max<std::int64_t>(
        1, std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - started).count());
    Json result = {
        {"state", "measured"}, {"frames", count},
        {"elapsed_us", us},    {"width", 1920},
        {"height", 1080},      {"fps", static_cast<double>(count) * 1000000. / static_cast<double>(us)}};
    if (peak)
        result["sampled_device_memory_bytes"] = peak;
    return result;
}
} // namespace
Json measure_video(const Json& request)
{
    gst_init(nullptr, nullptr);
    if (foreign_video(request.at("device")))
        return {{"state", "busy"}};
    auto first = video_sample(request);
    if (first.at("state") != "measured" ||
        (request.at("operation") != "encode" && request.at("operation") != "decode"))
        return first;
    // A median limits the effect of process scheduling on a single cold pipeline.
    std::array<Json, 3> trials{std::move(first), video_sample(request), video_sample(request)};
    std::ranges::sort(trials, {}, [](const Json& trial) { return trial.at("elapsed_us").get<std::int64_t>(); });
    auto result = trials[1];
    result["elapsed_samples_us"] = {trials[0].at("elapsed_us"), trials[1].at("elapsed_us"),
                                    trials[2].at("elapsed_us")};
    return result;
}
} // namespace bacuri::vms::acceleration
