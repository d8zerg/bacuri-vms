#include "recorder/frame_clock.hpp"

#include <glib.h>

#include <chrono>
#include <memory>

namespace bacuri::vms::ingest
{
using namespace domain;
using namespace std::chrono_literals;

ProjectedTime FrameClock::project(UtcTime received, ReferenceTimes reference)
{
    const auto& [ntp_ns, pts_ns] = reference;
    constexpr auto unix_epoch_ns = 2'208'988'800ULL * 1'000'000'000;
    if (!ntp_ns || *ntp_ns < unix_epoch_ns || !pts_ns) {
        return ProjectedTime::from_reception(received);
    }
    const SourceTime source{Microseconds{*ntp_ns / 1000}};
    const UtcTime utc{Microseconds{(*ntp_ns - unix_epoch_ns) / 1000}};
    const auto skew = std::chrono::abs(received - utc);
    if (skew > 5s) {
        previous_offset_.reset();
        return ProjectedTime::from_reception(received, source);
    }
    const auto offset = source.value - Microseconds{*pts_ns / 1000};
    if (!epoch_ || !previous_offset_ || std::chrono::abs(offset - *previous_offset_) > 500ms) {
        std::unique_ptr<char, decltype(&g_free)> id{g_uuid_string_random(), g_free};
        epoch_ = ClockEpochId::parse(id.get());
    }
    previous_offset_ = offset;
    if (epoch_) {
        return ProjectedTime::from_source({*epoch_, source, utc, skew}, source, received)
            .value_or(ProjectedTime::from_reception(received, source));
    }
    return ProjectedTime::from_reception(received, source);
}

} // namespace bacuri::vms::ingest
