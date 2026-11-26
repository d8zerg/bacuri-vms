#pragma once
#include "archive/volume.hpp"
#include "recorder/ingest.hpp"
namespace bacuri::vms::ingest
{
void detect_motion(const std::stop_token&, Stream&, archive::Volume&, const std::string& point,
                   std::int64_t binding);
}
