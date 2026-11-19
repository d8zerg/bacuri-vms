#include "domain/archive.hpp"
#include "domain/observation.hpp"
#include "domain/resources.hpp"
#include "domain/time.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <expected>
#include <format>
#include <limits>
#include <source_location>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <variant>

namespace bacuri::vms::test {
namespace {

using namespace std::chrono_literals;
using namespace bacuri::vms::domain;

void check(bool condition, std::string_view message, std::source_location location = std::source_location::current())
{
    if (!condition) {
        throw std::runtime_error{std::format("{}:{}: {}", location.file_name(), location.line(), message)};
    }
}

template <typename Id>
Id fixture_id(std::string_view value)
{
    const auto id = Id::parse(value);
    if (!id) {
        throw std::runtime_error{"invalid fixture identifier"};
    }
    return *id;
}

void verify_identifiers()
{
    static_assert(!std::is_convertible_v<SiteId, DeviceId>);
    const auto canonical = "12345678-abcd-4000-8000-123456789abc";

    const auto id = fixture_id<SiteId>("12345678-ABCD-4000-8000-123456789ABC");
    check(id.to_string() == canonical, "canonical identifier");
    check(!SiteId::parse("12345678-abcd-4000-8000-123456789abz"), "reject non-hexadecimal digit");
    check(!SiteId::parse("12345678xabcd-4000-8000-123456789abc"), "reject incorrect separator");
    check(!SiteId::parse("12345678-abcd-4000"), "reject shortened identifier");
}

void verify_camera_replacement()
{
    const auto site = fixture_id<SiteId>("10000000-0000-4000-8000-000000000001");
    const auto original_channel = fixture_id<ChannelId>("30000000-0000-4000-8000-000000000001");
    const auto point = fixture_id<ObservationPointId>("20000000-0000-4000-8000-000000000001");
    const ChannelBinding old{.key = {site, point, 1}, .channel = original_channel, .valid_from = UtcTime{0us}};
    Channel replacement{
        .site = site,
        .device = fixture_id<DeviceId>("40000000-0000-4000-8000-000000000002"),
        .id = fixture_id<ChannelId>("30000000-0000-4000-8000-000000000002"),
        .index = 0,
    };

    const auto next = replace_channel(old, replacement, UtcTime{10us}).value();
    check(next.key.site == site && next.key.point == point, "retain point identity");
    check(next.key.revision == 2 && next.channel == replacement.id, "create a new binding");
    check(old.channel == original_channel && old.key.revision == 1, "retain historical binding");
    check(replace_channel(old, replacement, old.valid_from) == std::unexpected{BindingError::non_increasing_time},
        "reject replacement at the previous boundary");

    auto final_binding = old;
    final_binding.key.revision = std::numeric_limits<Revision>::max();
    check(replace_channel(final_binding, replacement, UtcTime{10us}) == std::unexpected{BindingError::invalid_revision},
        "guard binding revision overflow");

    replacement.site = fixture_id<SiteId>("10000000-0000-4000-8000-000000000002");
    check(replace_channel(old, replacement, UtcTime{10us}) == std::unexpected{BindingError::different_site},
        "reject replacement from a different site");

    check(!ConfigurationProgress{.desired_revision = 2, .applied_revision = 1}.converged(),
        "desired revision may be ahead of applied revision");
    check(ConfigurationProgress{.desired_revision = 2, .applied_revision = 2}.converged(),
        "matching applied revision is current");
}

void verify_time()
{
    const auto epoch = fixture_id<ClockEpochId>("50000000-0000-4000-8000-000000000001");
    const auto interval = UtcInterval::make(UtcTime{1'018us}, UtcTime{1'118us}).value();
    check(interval.contains(UtcTime{1'018us}), "interval includes its beginning");
    check(!interval.contains(UtcTime{1'118us}), "interval excludes its end");
    check(!UtcInterval::make(interval.end(), interval.begin()), "reject reversed interval");
    check(!UtcInterval::make(interval.begin(), interval.begin()), "reject empty interval");

    const ClockAnchor anchor{
        .epoch = epoch,
        .source = SourceTime{100us},
        .utc = UtcTime{1'000us},
        .uncertainty = 8us,
    };
    const auto mapped = project_from_source(anchor, SourceTime{118us}, UtcTime{1'021us}).value();
    check(mapped.utc == interval.begin(), "project source time to UTC");
    check(mapped.received_at == UtcTime{1'021us}, "preserve receive time");
    check(mapped.source == SourceTime{118us}, "preserve source time");
    check(mapped.clock_epoch == epoch && mapped.uncertainty == 8us, "retain epoch and finite uncertainty");
    check(mapped.basis() == TimeBasis::source_clock, "indicate source clock");

    const auto estimate = estimate_from_reception(UtcTime{1'119us}, SourceTime{100us});
    check(estimate.basis() == TimeBasis::reception, "mark receipt-based estimate");
    check(estimate.utc == estimate.received_at && estimate.source == SourceTime{100us}, "retain untrusted source time");
    check(!estimate.clock_epoch && !estimate.uncertainty, "do not imply known clock uncertainty");
    check(!estimate_from_reception(UtcTime{1'119us}).source, "support observations without source time");

    auto restarted = anchor;
    restarted.epoch = fixture_id<ClockEpochId>("50000000-0000-4000-8000-000000000002");
    restarted.utc = UtcTime{1'150us};
    const auto after_restart = project_from_source(restarted, SourceTime{100us}, UtcTime{1'151us}).value();
    check(after_restart.clock_epoch != mapped.clock_epoch, "distinguish clock epochs across restart");
    check(after_restart.utc == UtcTime{1'150us}, "apply new clock anchor");

    restarted.uncertainty = -1us;
    check(project_from_source(restarted, SourceTime{100us}, UtcTime{1'151us})
            == std::unexpected{TimeError::negative_uncertainty},
        "reject negative uncertainty");

    restarted.uncertainty = 0us;
    restarted.utc = UtcTime{Microseconds::max()};
    check(project_from_source(restarted, SourceTime{101us}, UtcTime{1'151us})
            == std::unexpected{TimeError::arithmetic_overflow},
        "guard UTC overflow");

    restarted.source = SourceTime{Microseconds::min()};
    check(project_from_source(restarted, SourceTime{Microseconds::max()}, UtcTime{1'151us})
            == std::unexpected{TimeError::arithmetic_overflow},
        "guard overflow in source delta");
}

void verify_archive_and_observations()
{
    const auto site = fixture_id<SiteId>("10000000-0000-4000-8000-000000000001");
    const auto point = fixture_id<ObservationPointId>("20000000-0000-4000-8000-000000000001");
    const auto block = fixture_id<BlockId>("60000000-0000-4000-8000-000000000001");
    const auto epoch = fixture_id<RecordingEpochId>("70000000-0000-4000-8000-000000000001");
    const auto source = fixture_id<ObservationSourceId>("80000000-0000-4000-8000-000000000001");
    const auto observation = fixture_id<ObservationId>("90000000-0000-4000-8000-000000000001");
    const BindingKey binding{site, point, 1};
    const BlockKey key{site, block, epoch, 1};
    check(key != BlockKey{site, block, epoch, 2}, "owner generation is part of the block key");

    const ArchiveInterval recorded = RecordedInterval{
        .block = key,
        .binding = binding,
        .interval = UtcInterval::make(UtcTime{0us}, UtcTime{10us}).value(),
        .first_frame = estimate_from_reception(UtcTime{0us}),
    };
    const ArchiveInterval gap = RecordingGap{
        .binding = binding,
        .interval = UtcInterval::make(UtcTime{10us}, UtcTime{20us}).value(),
        .reason = GapReason::source_offline,
    };

    const auto& recorded_interval = std::get<RecordedInterval>(recorded);
    const auto& gap_interval = std::get<RecordingGap>(gap);
    check(recorded_interval.binding == gap_interval.binding, "gap and recording refer to the same binding");
    check(recorded_interval.interval.end() == gap_interval.interval.begin(), "gap starts at recording boundary");
    check(recorded_interval.first_frame.basis() == TimeBasis::reception, "available recording can have uncertain time");
    check(gap_interval.reason == GapReason::source_offline, "gap carries its cause");

    const Observation event{
        .key = {site, source, observation},
        .type = "motion",
        .schema_version = 1,
        .time = estimate_from_reception(UtcTime{25us}),
        .binding = binding,
        .origin = "camera",
        .attributes_json = "{}",
    };
    auto retry = event;
    retry.time = estimate_from_reception(UtcTime{30us});
    check(event.key == retry.key, "receive time does not define observation identity");
    retry.key.source = fixture_id<ObservationSourceId>("80000000-0000-4000-8000-000000000002");
    check(event.key != retry.key, "source is part of observation identity");
}

}  // namespace

void verify_domain_model()
{
    verify_identifiers();
    verify_camera_replacement();
    verify_time();
    verify_archive_and_observations();
}

}  // namespace bacuri::vms::test

int main()
try
{
    bacuri::vms::test::verify_domain_model();
    return EXIT_SUCCESS;
}
catch (const std::exception& error)
{
    std::fputs(error.what(), stderr);
    std::fputc('\n', stderr);
    return EXIT_FAILURE;
}
