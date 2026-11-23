#pragma once

#include "recorder/ingest.hpp"
#include <gst/gst.h>

namespace bacuri::vms::ingest
{

using Receive = std::function<void(FramePtr, VideoFormat, bool discontinuity)>;

bool configure_rtsp_source(GstElement* source, const Endpoint&);
std::string receive_rtsp(const Endpoint& endpoint, std::stop_token stop, const Receive& receive);

} // namespace bacuri::vms::ingest
