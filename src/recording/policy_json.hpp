#pragma once
#include "recording/policy.hpp"
#include <nlohmann/json.hpp>
namespace bacuri::vms::recording
{
void to_json(nlohmann::json&, const CaptureRule&);
void from_json(const nlohmann::json&, CaptureRule&);
nlohmann::json capture_rule_json(const CaptureRule&);
CaptureRule parse_capture_rule(const nlohmann::json& rule);
PointPolicies decode_plan(const nlohmann::json&);
} // namespace bacuri::vms::recording
