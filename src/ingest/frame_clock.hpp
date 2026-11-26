#pragma once

#include "domain/time.hpp"

#include <cstdint>

namespace bacuri::vms::ingest
{

struct ReferenceTimes {
    std::optional<std::uint64_t> ntp_ns;
    std::optional<std::uint64_t> pts_ns;
};

class FrameClock
{
  public:
    [[nodiscard]] domain::ProjectedTime project(domain::UtcTime received, ReferenceTimes reference);

  private:
    std::optional<domain::ClockEpochId> epoch_;
    std::optional<domain::Microseconds> previous_offset_;
};

} // namespace bacuri::vms::ingest
