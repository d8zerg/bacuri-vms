#include "domain/archive.hpp"
#include "domain/observation.hpp"
#include "domain/resources.hpp"
#include "domain/time.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <expected>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <variant>

namespace bacuri::vms::test
{
namespace
{

using namespace std::chrono_literals;
using namespace bacuri::vms::domain;

template <typename Id> consteval Id fixture_id(std::string_view value)
{
    const auto id = Id::parse(value);
    if (!id) {
        throw std::invalid_argument{"Invalid fixture identifier"};
    }
    return *id;
}

constexpr auto site = fixture_id<SiteId>("10000000-0000-4000-8000-000000000001");
constexpr auto other_site = fixture_id<SiteId>("10000000-0000-4000-8000-000000000002");
constexpr auto point = fixture_id<ObservationPointId>("20000000-0000-4000-8000-000000000001");
constexpr BindingKey binding{site, point, 1};
constexpr BlockKey block{
    site,
    fixture_id<BlockId>("60000000-0000-4000-8000-000000000001"),
    fixture_id<RecordingEpochId>("70000000-0000-4000-8000-000000000001"),
    1,
};
constexpr ClockAnchor anchor{
    .epoch = fixture_id<ClockEpochId>("50000000-0000-4000-8000-000000000001"),
    .source = SourceTime{100us},
    .utc = UtcTime{1'000us},
    .uncertainty = 8us,
};

static_assert(!std::is_convertible_v<SiteId, DeviceId>);
static_assert(!std::is_aggregate_v<ProjectedTime> && !std::is_default_constructible_v<ProjectedTime>);
static_assert(!std::is_aggregate_v<RecordedInterval> && !std::is_default_constructible_v<RecordedInterval>);

TEST(Identifier, CanonicalizesAndValidatesUuid)
{
    constexpr auto id = fixture_id<SiteId>("12345678-ABCD-4000-8000-123456789ABC");
    EXPECT_EQ(id.to_string(), "12345678-abcd-4000-8000-123456789abc");
    EXPECT_FALSE(SiteId::parse("12345678-abcd-4000-8000-123456789abz"));
    EXPECT_FALSE(SiteId::parse("12345678xabcd-4000-8000-123456789abc"));
    EXPECT_FALSE(SiteId::parse("12345678-abcd-4000"));
}

TEST(ChannelBinding, PreservesPointAndHistoricalBinding)
{
    constexpr auto original_channel = fixture_id<ChannelId>("30000000-0000-4000-8000-000000000001");
    constexpr ChannelBinding old{.key = binding, .channel = original_channel, .valid_from = UtcTime{0us}};
    Channel replacement{
        .site = site,
        .device = fixture_id<DeviceId>("40000000-0000-4000-8000-000000000002"),
        .id = fixture_id<ChannelId>("30000000-0000-4000-8000-000000000002"),
        .index = 0,
    };

    const auto next = replace_channel(old, replacement, UtcTime{10us});
    ASSERT_TRUE(next);
    EXPECT_EQ(next->key, (BindingKey{site, point, 2}));
    EXPECT_EQ(next->channel, replacement.id);
    EXPECT_EQ(old.key, binding);
    EXPECT_EQ(old.channel, original_channel);
    EXPECT_EQ(replace_channel(old, replacement, old.valid_from),
              std::unexpected{BindingError::non_increasing_time});

    auto final_binding = old;
    final_binding.key.revision = std::numeric_limits<Revision>::max();
    EXPECT_EQ(replace_channel(final_binding, replacement, UtcTime{10us}),
              std::unexpected{BindingError::invalid_revision});

    replacement.site = other_site;
    EXPECT_EQ(replace_channel(old, replacement, UtcTime{10us}), std::unexpected{BindingError::different_site});
}

TEST(ConfigurationProgress, DistinguishesDesiredAndAppliedRevisions)
{
    EXPECT_FALSE((ConfigurationProgress{.desired_revision = 2, .applied_revision = 1}.converged()));
    EXPECT_TRUE((ConfigurationProgress{.desired_revision = 2, .applied_revision = 2}.converged()));
}

TEST(UtcInterval, ContainsTheEntireClockUncertaintyAtBothBounds)
{
    const auto interval = UtcInterval::make(UtcTime{100us}, UtcTime{200us}).value();
    EXPECT_TRUE(interval.contains(UtcTime{105us}, 5us));
    EXPECT_FALSE(interval.contains(UtcTime{104us}, 5us));
    EXPECT_TRUE(interval.contains(UtcTime{194us}, 5us));
    EXPECT_FALSE(interval.contains(UtcTime{195us}, 5us));
    EXPECT_FALSE(interval.contains(UtcTime{150us}, -1us));
    EXPECT_FALSE(interval.contains(UtcTime::max(), 1us));
    EXPECT_FALSE(interval.contains(UtcTime::min(), 1us));
}

TEST(UtcInterval, IncludesOnlyItsStartBoundary)
{
    const auto interval = UtcInterval::make(UtcTime{1'018us}, UtcTime{1'118us});
    ASSERT_TRUE(interval);
    EXPECT_TRUE(interval->contains(interval->begin()));
    EXPECT_FALSE(interval->contains(interval->end()));
    EXPECT_EQ(UtcInterval::make(interval->end(), interval->begin()),
              std::unexpected{TimeError::empty_or_reversed_interval});
    EXPECT_EQ(UtcInterval::make(interval->begin(), interval->begin()),
              std::unexpected{TimeError::empty_or_reversed_interval});
}

TEST(ProjectedTime, RetainsSourceAndClockEpoch)
{
    const auto mapped = ProjectedTime::from_source(anchor, SourceTime{118us}, UtcTime{1'021us});
    ASSERT_TRUE(mapped);
    EXPECT_EQ(mapped->utc(), UtcTime{1'018us});
    EXPECT_EQ(mapped->received_at(), UtcTime{1'021us});
    EXPECT_EQ(mapped->source(), SourceTime{118us});
    EXPECT_EQ(mapped->clock_epoch(), anchor.epoch);
    EXPECT_EQ(mapped->uncertainty(), 8us);
    EXPECT_EQ(mapped->basis(), TimeBasis::source_clock);

    auto restarted = anchor;
    restarted.epoch = fixture_id<ClockEpochId>("50000000-0000-4000-8000-000000000002");
    restarted.utc = UtcTime{1'150us};
    restarted.uncertainty = 0us;
    const auto after_restart = ProjectedTime::from_source(restarted, SourceTime{100us}, UtcTime{1'151us});
    ASSERT_TRUE(after_restart);
    EXPECT_EQ(after_restart->clock_epoch(), restarted.epoch);
    EXPECT_EQ(after_restart->utc(), restarted.utc);
    EXPECT_EQ(after_restart->uncertainty(), 0us);
}

TEST(ProjectedTime, ReceptionUsesReceiptTimeAndCanRetainRawSource)
{
    const auto estimate = ProjectedTime::from_reception(UtcTime{1'119us}, SourceTime{100us});
    EXPECT_EQ(estimate.basis(), TimeBasis::reception);
    EXPECT_EQ(estimate.utc(), UtcTime{1'119us});
    EXPECT_EQ(estimate.utc(), estimate.received_at());
    EXPECT_EQ(estimate.source(), SourceTime{100us});
    EXPECT_FALSE(estimate.clock_epoch());
    EXPECT_FALSE(estimate.uncertainty());
    EXPECT_FALSE(ProjectedTime::from_reception(UtcTime{1'119us}).source());
}

TEST(ProjectedTime, RejectsNegativeUncertaintyAndOverflow)
{
    auto invalid = anchor;
    invalid.uncertainty = -1us;
    EXPECT_EQ(ProjectedTime::from_source(invalid, SourceTime{100us}, UtcTime{1'151us}),
              std::unexpected{TimeError::negative_uncertainty});

    invalid.uncertainty = 0us;
    invalid.utc = UtcTime{Microseconds::max()};
    EXPECT_EQ(ProjectedTime::from_source(invalid, SourceTime{101us}, UtcTime{1'151us}),
              std::unexpected{TimeError::arithmetic_overflow});
    invalid.utc = UtcTime{Microseconds::min()};
    EXPECT_EQ(ProjectedTime::from_source(invalid, SourceTime{99us}, UtcTime{1'151us}),
              std::unexpected{TimeError::arithmetic_overflow});

    invalid.source = SourceTime{Microseconds::min()};
    EXPECT_EQ(ProjectedTime::from_source(invalid, SourceTime{Microseconds::max()}, UtcTime{1'151us}),
              std::unexpected{TimeError::arithmetic_overflow});
    invalid.source = SourceTime{Microseconds::max()};
    EXPECT_EQ(ProjectedTime::from_source(invalid, SourceTime{Microseconds::min()}, UtcTime{1'151us}),
              std::unexpected{TimeError::arithmetic_overflow});
}

TEST(RecordedInterval, DistinguishesRecordingWithUncertainTimeFromGap)
{
    auto next_generation = block;
    ++next_generation.owner_generation;
    EXPECT_NE(block, next_generation);

    const auto interval = UtcInterval::make(UtcTime{0us}, UtcTime{10us});
    ASSERT_TRUE(interval);
    const auto recording =
        RecordedInterval::make(block, binding, *interval, ProjectedTime::from_reception(UtcTime{0us}));
    ASSERT_TRUE(recording);
    const ArchiveInterval recorded = *recording;
    const ArchiveInterval gap = RecordingGap{
        .binding = binding,
        .interval = UtcInterval::make(UtcTime{10us}, UtcTime{20us}).value(),
        .reason = GapReason::source_offline,
    };

    const auto& available = std::get<RecordedInterval>(recorded);
    const auto& missing = std::get<RecordingGap>(gap);
    EXPECT_EQ(available.block(), block);
    EXPECT_EQ(available.binding(), missing.binding);
    EXPECT_EQ(available.interval().end(), missing.interval.begin());
    EXPECT_EQ(available.first_frame().basis(), TimeBasis::reception);
    EXPECT_EQ(missing.reason, GapReason::source_offline);
}

TEST(RecordedInterval, RequiresSameSiteAndFirstFrameAtStart)
{
    const auto interval = UtcInterval::make(UtcTime{0us}, UtcTime{10us});
    ASSERT_TRUE(interval);
    auto foreign_binding = binding;
    foreign_binding.site = other_site;
    EXPECT_EQ(
        RecordedInterval::make(block, foreign_binding, *interval, ProjectedTime::from_reception(UtcTime{0us})),
        std::unexpected{RecordingError::different_site});
    EXPECT_EQ(RecordedInterval::make(block, binding, *interval, ProjectedTime::from_reception(UtcTime{1us})),
              std::unexpected{RecordingError::first_frame_not_at_begin});
}

TEST(Observation, UsesSourceAndIdForRetryIdentity)
{
    const Observation event{
        .key =
            {
                site,
                fixture_id<ObservationSourceId>("80000000-0000-4000-8000-000000000001"),
                fixture_id<ObservationId>("90000000-0000-4000-8000-000000000001"),
            },
        .type = "motion",
        .schema_version = 1,
        .time = ProjectedTime::from_reception(UtcTime{25us}),
        .binding = binding,
        .origin = "camera",
        .attributes_json = "{}",
    };
    auto retry = event;
    retry.time = ProjectedTime::from_reception(UtcTime{30us});
    EXPECT_EQ(event.key, retry.key);
    retry.key.source = fixture_id<ObservationSourceId>("80000000-0000-4000-8000-000000000002");
    EXPECT_NE(event.key, retry.key);
}

} // namespace
} // namespace bacuri::vms::test
