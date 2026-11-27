#include "archive/analysis.hpp"
#include "media/privacy.hpp"
#include "platform/scope_exit.hpp"
#include <gst/app/gstappsink.h>
#include <sys/resource.h>
#include <array>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string_view>

namespace
{
template <typename T> auto own(T* value)
{
    return std::unique_ptr<T, decltype(&gst_object_unref)>{value, gst_object_unref};
}

void measure(const char* description, std::string_view workload)
{
    using namespace bacuri::vms;
    GError* raw_error{};
    auto pipe = own(gst_parse_launch(description, &raw_error));
    std::unique_ptr<GError, decltype(&g_error_free)> error{raw_error, g_error_free};
    if (error || !pipe)
        throw std::runtime_error(error ? error->message : "Missing pipeline");
    auto output = own(gst_bin_get_by_name(GST_BIN(pipe.get()), "output"));
    if (!output || !GST_IS_APP_SINK(output.get()))
        throw std::runtime_error("Missing output");
    media::PrivacyMask mask{domain::ImageRegions{{0.25, 0.25, 0.75, 0.75}}};
    if (workload == "mask" || workload == "paced-mask") {
        auto target = own(gst_bin_get_by_name(GST_BIN(pipe.get()), "mask"));
        mask.attach(target.get());
    }
    auto bus = own(gst_element_get_bus(pipe.get()));
    platform::ScopeExit stop{[&pipe] { gst_element_set_state(pipe.get(), GST_STATE_NULL); }};
    if (gst_element_set_state(pipe.get(), GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE)
        throw std::runtime_error("Cannot start pipeline");
    unsigned frames{}, observations{};
    std::array<unsigned char, std::size_t{archive::preview_width} * archive::preview_height> previous{}, pixels{};
    GstClockTime last_sample = GST_CLOCK_TIME_NONE;
    while (auto* raw = gst_app_sink_pull_sample(GST_APP_SINK(output.get()))) {
        std::unique_ptr<GstSample, decltype(&gst_sample_unref)> sample{raw, gst_sample_unref};
        auto* buffer = gst_sample_get_buffer(sample.get());
        ++frames;
        if (workload == "paced-mask") {
            const auto pts = GST_BUFFER_PTS(buffer);
            if (!GST_CLOCK_TIME_IS_VALID(pts) || (frames > 1 && pts != last_sample + GST_SECOND / 25))
                throw std::runtime_error("Masked output lost its 25 fps cadence");
            last_sample = pts;
        }
        if (workload == "motion") {
            const auto pts = GST_BUFFER_PTS(buffer);
            if (!GST_CLOCK_TIME_IS_VALID(pts))
                throw std::runtime_error("Missing frame time");
            if (last_sample == GST_CLOCK_TIME_NONE || pts - last_sample >= GST_SECOND / 4) {
                if (gst_buffer_extract(buffer, 0, pixels.data(), pixels.size()) != pixels.size())
                    throw std::runtime_error("Incomplete motion image");
                archive::motion_sample(observations ? std::span<const unsigned char>{previous}
                                                    : std::span<const unsigned char>{},
                                       pixels, static_cast<std::int64_t>(pts / GST_USECOND));
                previous = pixels;
                last_sample = pts;
                ++observations;
            }
        }
    }
    std::unique_ptr<GstMessage, decltype(&gst_message_unref)> message{
        gst_bus_timed_pop_filtered(bus.get(), GST_SECOND,
                                   static_cast<GstMessageType>(GST_MESSAGE_ERROR | GST_MESSAGE_EOS)),
        gst_message_unref};
    if (!message)
        throw std::runtime_error("Missing end of stream");
    if (GST_MESSAGE_TYPE(message.get()) == GST_MESSAGE_ERROR) {
        GError* failure{};
        gst_message_parse_error(message.get(), &failure, nullptr);
        error.reset(failure);
        throw std::runtime_error(error->message);
    }
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) != 0)
        throw std::runtime_error("Cannot measure process resources");
    const auto cpu_us = (usage.ru_utime.tv_sec + usage.ru_stime.tv_sec) * 1'000'000L + usage.ru_utime.tv_usec +
                        usage.ru_stime.tv_usec;
    std::cout << frames << ' ' << observations << ' ' << cpu_us << ' ' << usage.ru_maxrss << '\n';
}
} // namespace

int main(int argc, char** argv)
{
    if (argc != 3)
        return 2;
    try {
        gst_init(nullptr, nullptr);
        measure(argv[1], argv[2]);
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
