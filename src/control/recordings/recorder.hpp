#pragma once

#include "control/recordings/status.hpp"

#include <optional>
#include <string>

namespace bacuri::vms::control::recordings
{
// Reception, policy selection and storage capacity reported by the local recorder.
// Returns nothing when the recorder does not answer within a short deadline.
std::optional<RecorderSnapshot> recorder_snapshot(const std::string& site);
} // namespace bacuri::vms::control::recordings
