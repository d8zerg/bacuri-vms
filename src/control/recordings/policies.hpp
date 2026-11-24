#pragma once
#include "control/api.hpp"
namespace bacuri::vms::control
{
class Sessions;
namespace recordings
{
Json case_for_export(Database&, const std::string& site, const std::string& id, const std::string& user);
void investigation(const Request&, Response&, const Sessions&, const std::string& site, const std::string& id);
void case_recipients(const Request&, Response&, const Sessions&, const std::string& site);
void holds(const Request&, Response&, const Sessions&, const std::string& site, const std::string& point);
void policies(const Request&, Response&, const Sessions&, const std::string& site, const std::string& id);
void manual_recording(const Request&, Response&, const Sessions&, const std::string& site,
                      const std::string& point);
void volumes(const Request&, Response&, const Sessions&, const std::string& site);
Json recording_plan(Database&, const std::string& site);
void advance_point_retention(Database&, const std::string& site, const Json& plan);
void motion_recording(Database&, const std::string& site, const std::string& point, std::int64_t binding,
                      std::int64_t at);
} // namespace recordings
} // namespace bacuri::vms::control
