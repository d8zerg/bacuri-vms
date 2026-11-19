#include "recorder/ingest.hpp"
#include "recorder/frame_clock.hpp"

#include <gmock/gmock.h>

#include <future>

namespace bacuri::vms::ingest
{
namespace
{
using namespace domain;
using namespace std::chrono_literals;

FramePtr frame(int index, bool keyframe, std::size_t bytes = 1)
{
    return std::make_shared<Frame>(Frame{std::vector<std::byte>(bytes),
                                         ProjectedTime::from_reception(UtcTime{Microseconds{index}}),
                                         {},
                                         {},
                                         {},
                                         keyframe});
}

TEST(FrameQueue, SlowPreviewDiscardsBufferedGopAndWaitsForKeyframe)
{
    FrameQueue queue{Profile::secondary};
    const auto capacity = limits(Profile::secondary);
    queue.push(frame(0, true));
    for (std::size_t i = 1; i <= capacity.frames; ++i) {
        queue.push(frame(static_cast<int>(i), false));
    }
    EXPECT_FALSE(queue.try_pop());
    EXPECT_EQ(queue.stats().dropped, capacity.frames + 1);
    EXPECT_EQ(queue.stats().peak_depth, capacity.frames);
    const auto idr = frame(10, true);
    queue.push(idr);
    EXPECT_EQ(queue.try_pop(), idr);
    EXPECT_EQ(queue.stats().loss_begin, UtcTime{0us});
    EXPECT_EQ(queue.stats().recovered_at, UtcTime{10us});
}

TEST(FrameQueue, PreviewWaitReturnsAcceptedFrameOrTimesOut)
{
    FrameQueue queue{Profile::secondary};
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    auto waiting = std::async(std::launch::async, [&] { return queue.wait_pop(deadline); });
    const auto keyframe = frame(1, true);
    queue.push(keyframe);
    EXPECT_EQ(waiting.get(), keyframe);
    EXPECT_FALSE(queue.wait_pop(std::chrono::steady_clock::now() + 1ms));
}

TEST(FrameQueue, OversizeKeyframeDoesNotExceedByteBudget)
{
    FrameQueue queue{Profile::main};
    queue.push(frame(1, true, limits(Profile::main).bytes + 1));
    queue.push(frame(2, false));
    EXPECT_EQ(queue.stats().dropped, 2U);
    EXPECT_EQ(queue.stats().bytes, 0U);
    EXPECT_FALSE(queue.try_pop());
}

TEST(FrameQueue, FullRecordingQueueKeepsAcceptedFramesAndReportsMissingInterval)
{
    FrameQueue queue{Profile::main};
    for (std::size_t i = 0; i < limits(Profile::main).frames; ++i) {
        queue.push(frame(static_cast<int>(i), i == 0));
    }
    queue.push(frame(64, false));
    EXPECT_EQ(queue.stats().depth, 64U);
    ASSERT_TRUE(queue.try_pop());
    queue.push(frame(65, false));
    queue.push(frame(66, true));
    EXPECT_EQ(queue.stats().dropped, 2U);
    EXPECT_EQ(queue.stats().loss_begin, UtcTime{64us});
    EXPECT_EQ(queue.stats().recovered_at, UtcTime{66us});
}

TEST(FrameQueue, SourceInterruptionStartsNewGopAfterQueuedFrames)
{
    FrameQueue queue{Profile::main};
    const auto before = frame(1, true);
    const auto after = frame(3, true);
    queue.push(before);
    queue.interrupt();
    queue.push(frame(2, false));
    queue.push(after);
    EXPECT_EQ(queue.try_pop(), before);
    EXPECT_EQ(queue.try_pop(), after);
    EXPECT_FALSE(queue.try_pop());
}
TEST(FrameClock, PreservesRawTimeAndChangesEpochOnSourceClockJump)
{
    constexpr auto ntp_epoch = 2'208'988'800'000'000'000ULL;
    FrameClock clock;
    const auto received = UtcTime{100ms};
    EXPECT_EQ(clock.project(received, {}).basis(), TimeBasis::reception);
    const auto reference = clock.project(received, {ntp_epoch, 0});
    EXPECT_EQ(reference.basis(), TimeBasis::source_clock);
    EXPECT_EQ(reference.utc(), UtcTime{0us});
    EXPECT_EQ(reference.uncertainty(), 100ms);
    EXPECT_EQ(reference.clock_epoch(),
              clock.project(received + 1s, {ntp_epoch + 1'000'000'000, 1'000'000'000}).clock_epoch());
    const auto jump = clock.project(received + 2s, {ntp_epoch + 2'000'000'000, 1'040'000'000});
    EXPECT_NE(reference.clock_epoch(), jump.clock_epoch());
    const auto untrusted = clock.project(received + 20s, {ntp_epoch, 0});
    EXPECT_EQ(untrusted.basis(), TimeBasis::reception);
    EXPECT_TRUE(untrusted.source());
}

TEST(StreamPolicy, DeniedAddressReportsFailureAndShutdownCancelsBackoff)
{
    testing::StrictMock<testing::MockFunction<bool(const std::string&)>> allow;
    EXPECT_CALL(allow, Call("rtsp://example.invalid/main")).WillOnce(testing::Return(false));
    std::promise<StreamStats> reported;
    auto result = reported.get_future();
    Stream stream{{Profile::main,
                   {"rtsp://example.invalid/main", {}, {}},
                   allow.AsStdFunction(),
                   [&](const StreamStats& stats) { reported.set_value(stats); }}};
    ASSERT_EQ(result.wait_for(2s), std::future_status::ready);
    stream.request_stop();
    EXPECT_EQ(result.get().state, "address_denied");
}
} // namespace
} // namespace bacuri::vms::ingest
