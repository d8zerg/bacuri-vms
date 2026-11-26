#pragma once
#include "ingest/ingest.hpp"

namespace bacuri::vms::ingest
{
struct ReplayInterval {
    std::int64_t begin_us, end_us;
};
std::string receive_replay(const Endpoint&, ReplayInterval, const std::function<bool()>& cancelled,
                           const std::function<void(FramePtr, VideoFormat, bool)>&);
int replay_worker();
} // namespace bacuri::vms::ingest
