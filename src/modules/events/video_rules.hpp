#pragma once
#include <optional>
#include "application/control_api/api.hpp"
#include <stop_token>
#include <functional>
namespace bacuri::vms::control
{
class Sessions;
namespace events
{
using PresetDispatch = std::function<void(const std::string& point, std::int64_t binding, const std::string& user,
                                          const std::string& request, const std::string& preset)>;
void dispatch_presets(const std::stop_token&, const std::string& site, const PresetDispatch&);
struct RuleDecision {
    bool record{}, notify{};
};
void video_rule(const Request&, Response&, const Sessions&, const std::string& site, const std::string& point,
                bool simulation);
std::vector<std::string> detector_points(postgresql::Database&, const std::string& site);
struct MotionAttributes {
    std::optional<std::string> topic;
    std::string cells, previous;
};
struct VideoEvent {
    std::int64_t at{};
    std::string id, rule_revision, topic, source, alert_id;
    std::string source_id;
};
RuleDecision evaluate_observation(postgresql::Database&, const std::string& site, const std::string& point,
                                  const std::string& binding, const std::string& source,
                                  const std::string& observation, std::int64_t utc, bool local, bool device,
                                  const MotionAttributes& attributes);
std::vector<VideoEvent> video_context(postgresql::QuerySession&, const std::string& site, const std::string& point,
                                      std::int64_t begin, std::int64_t end, const std::string& query);
} // namespace events
} // namespace bacuri::vms::control
