#pragma once

#include "recorder/ingest.hpp"
#include <gst/gst.h>

namespace bacuri::vms::ingest
{

using Receive = std::function<void(FramePtr, VideoFormat, bool discontinuity)>;

bool configure_rtsp_source(GstElement* source, const Endpoint&);
struct ReplayWindow {
    std::int64_t begin_us, end_us;
};
std::string receive_rtsp(const Endpoint& endpoint, const std::stop_token& stop, const Receive& receive,
                         std::optional<ReplayWindow> replay = {}, const std::function<void()>& finished = {});

} // namespace bacuri::vms::ingest
