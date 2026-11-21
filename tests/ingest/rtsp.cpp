#include "recorder/ingest.hpp"

#include <gtest/gtest.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>

namespace bacuri::vms::ingest
{
namespace
{
using namespace std::chrono_literals;
using Steady = std::chrono::steady_clock;

template <typename Predicate> bool wait_for(Predicate predicate, std::chrono::seconds timeout)
{
    const auto deadline = Steady::now() + timeout;
    while (!predicate()) {
        if (Steady::now() >= deadline) {
            return false;
        }
        std::this_thread::sleep_for(20ms);
    }
    return true;
}

StreamOptions fixture_stream(Profile profile, const char* path)
{
    const auto* base = std::getenv("BACURI_TEST_RTSP_BASE");
    if (!base) {
        throw std::invalid_argument("Missing RTSP test relay");
    }
    auto url = std::string{base} + path;
    return {profile,
            Endpoint{url, {}, {}},
            [url](const std::string& candidate) { return candidate == url ? std::optional{url} : std::nullopt; },
            {}};
}

bool switch_link(char command)
{
    const auto* value = std::getenv("BACURI_TEST_LINK_FD");
    if (!value) {
        return false;
    }
    const int fd = std::stoi(value);
    char ack{};
    return write(fd, &command, 1) == 1 && read(fd, &ack, 1) == 1 && ack == command;
}

TEST(RtspIngest, IndependentProfilesBoundedPreviewAndOutageRecovery)
{
    Stream main{fixture_stream(Profile::main, "/main")};
    Stream secondary{fixture_stream(Profile::secondary, "/sub")};
    auto archive = main.subscribe();
    auto preview = secondary.subscribe();
    ASSERT_TRUE(wait_for(
        [&] {
            while (archive->try_pop()) {
                // Drain the main profile while the secondary session negotiates.
            }
            return main.stats().frames >= 8 && secondary.stats().frames >= 8;
        },
        18s))
        << main.stats().state << " / " << secondary.stats().state;
    const auto measure_start = Steady::now();
    const auto initial = main.stats();
    const auto first_preview = secondary.stats();
    EXPECT_EQ(initial.width, 1280);
    EXPECT_EQ(initial.height, 720);
    EXPECT_EQ(first_preview.width, 640);
    EXPECT_EQ(first_preview.height, 360);
    EXPECT_TRUE(initial.rtp_timestamp.has_value());
    EXPECT_TRUE(initial.pts_ns.has_value());
    EXPECT_GT(initial.bytes, 0U);
    ASSERT_TRUE(wait_for(
        [&] {
            while (archive->try_pop()) {
                // Consume the main profile while the preview reader is paused.
            }
            return preview->stats().dropped > 0 && main.stats().frames >= initial.frames + 125 &&
                   secondary.stats().frames >= first_preview.frames + 60;
        },
        14s));
    const auto elapsed = std::chrono::duration<double>(Steady::now() - measure_start).count();
    const auto summary = [elapsed](const StreamStats& before, const StreamStats& after) {
        const auto frames = static_cast<double>(after.frames - before.frames);
        const auto keys = after.keyframes - before.keyframes;
        EXPECT_GT(keys, 0U);
        std::cout << after.width << 'x' << after.height << ": " << frames / elapsed << " fps, "
                  << static_cast<double>(after.bytes - before.bytes) * 0.008 / elapsed << " kbit/s, "
                  << frames / static_cast<double>(std::max(keys, std::uint64_t{1})) << " frames/keyframe, "
                  << (after.time && after.time->basis() == domain::TimeBasis::source_clock ? "source_clock"
                                                                                           : "reception")
                  << '\n';
    };
    summary(initial, main.stats());
    summary(first_preview, secondary.stats());
    const auto slow = preview->stats();
    EXPECT_LE(slow.peak_depth, limits(Profile::secondary).frames);
    EXPECT_LE(slow.peak_bytes, limits(Profile::secondary).bytes);
    EXPECT_EQ(archive->stats().dropped, 0U);
    EXPECT_EQ(main.stats().gaps, initial.gaps);
    FramePtr recovered_preview;
    ASSERT_TRUE(wait_for(
        [&] {
            recovered_preview = preview->try_pop();
            return static_cast<bool>(recovered_preview);
        },
        8s));
    EXPECT_TRUE(recovered_preview->keyframe);
    std::cout << "Preview peak " << slow.peak_depth << " frames, " << slow.peak_bytes << " bytes, dropped "
              << slow.dropped << "; main received " << main.stats().frames << " frames\n";

    ASSERT_TRUE(switch_link('0'));
    ASSERT_TRUE(wait_for(
        [&] {
            const auto a = main.stats();
            const auto b = secondary.stats();
            return a.gap_begin && !a.gap_end && b.gap_begin && !b.gap_end;
        },
        12s));
    const auto gap = main.stats();
    ASSERT_TRUE(wait_for([&] { return main.stats().reconnects >= gap.reconnects + 1; }, 5s));
    EXPECT_LE(main.stats().reconnects - gap.reconnects, 2U);
    ASSERT_TRUE(switch_link('1'));
    ASSERT_TRUE(wait_for(
        [&] { return main.stats().state == "receiving" && secondary.stats().state == "receiving"; }, 18s));
    const auto restored = main.stats();
    ASSERT_TRUE(restored.gap_end);
    EXPECT_EQ(restored.gap_begin, gap.gap_begin);
    EXPECT_GT(restored.gap_end, restored.gap_begin);
    EXPECT_EQ(restored.gaps, gap.gaps);
    EXPECT_GT(restored.frames, gap.frames);
    if (gap.gap_begin && restored.gap_end) {
        std::cout << "Main gap UTC us [" << gap.gap_begin->time_since_epoch().count() << ", "
                  << restored.gap_end->time_since_epoch().count() << "), reconnects " << restored.reconnects
                  << '\n';
    }
}
TEST(RtspIngest, ReportsAnUnsupportedVideoCodec)
{
    if (!std::getenv("BACURI_TEST_JPEG")) {
        GTEST_SKIP() << "This source provides two H.264 profiles";
    }
    Stream jpeg{fixture_stream(Profile::main, "/jpeg")};
    ASSERT_TRUE(wait_for([&] { return jpeg.stats().state == "incompatible_profile"; }, 12s)) << jpeg.stats().state;
    EXPECT_EQ(jpeg.stats().frames, 0U);
}

} // namespace
} // namespace bacuri::vms::ingest
