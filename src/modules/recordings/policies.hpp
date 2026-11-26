#pragma once
#include "application/control_api/api.hpp"
#include "recording/policy.hpp"
namespace bacuri::vms::control
{
class Sessions;
namespace recordings
{
void holds(const Request&, Response&, const Sessions&, const std::string& site, const std::string& point);
void policies(const Request&, Response&, const Sessions&, const std::string& site, const std::string& id);
void manual_recording(const Request&, Response&, const Sessions&, const std::string& site,
                      const std::string& point);
void volumes(const Request&, Response&, const Sessions&, const std::string& site);
recording::PointPolicies recording_plan(postgresql::Database&, const std::string& site);
void advance_point_retention(postgresql::Database&, const std::string& site, const recording::PointPolicies& plan);
void motion_recording(postgresql::Database&, const std::string& site, const std::string& point,
                      std::int64_t binding, std::int64_t at);
} // namespace recordings
} // namespace bacuri::vms::control
