#pragma once

#include "recorder/ingest.hpp"

namespace bacuri::vms::ingest
{

using Receive = std::function<void(FramePtr, VideoFormat, bool discontinuity)>;

std::string receive_rtsp(const Endpoint& endpoint, std::stop_token stop, const Receive& receive);

} // namespace bacuri::vms::ingest
