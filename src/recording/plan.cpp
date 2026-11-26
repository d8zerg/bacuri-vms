#include "recording/policy_json.hpp"
#include "recording/plan.hpp"

namespace bacuri::vms::recording
{
using storage::bind_text;
using storage::column;
using storage::next;
namespace
{
auto shared_rules(PointPolicies rules)
{
    std::map<std::string, std::shared_ptr<const CaptureRule>> result;
    for (auto& [point, rule] : rules)
        result.emplace(point, std::make_shared<const CaptureRule>(std::move(rule)));
    return result;
}
} // namespace

RecordingPlan::RecordingPlan(const std::filesystem::path& archive_root) : db_{archive_root / "journal.db"}
{
    auto saved = db_.query("SELECT payload FROM recording_plan WHERE singleton=1");
    if (next(saved.get())) {
        payload_ = column(saved.get(), 0);
        rules_ = shared_rules(decode_plan(nlohmann::json::parse(payload_)));
    }
}

void RecordingPlan::update(PointPolicies rules)
{
    auto payload = nlohmann::json(rules).dump();
    std::scoped_lock lock{mutex_};
    if (payload_ == payload && !payload.empty())
        return;
    auto next_rules = shared_rules(std::move(rules));
    auto save = db_.query("INSERT INTO recording_plan VALUES(1,?) ON CONFLICT(singleton) DO UPDATE SET "
                          "payload=excluded.payload WHERE payload<>excluded.payload");
    bind_text(save.get(), 1, payload);
    next(save.get());
    rules_ = std::move(next_rules);
    payload_ = std::move(payload);
}
std::shared_ptr<const CaptureRule> RecordingPlan::rule(const std::string& point) const
{
    std::scoped_lock lock{mutex_};
    static const auto fallback = std::make_shared<const CaptureRule>();
    if (payload_.empty())
        return {};
    auto it = rules_.find(point);
    return it == rules_.end() ? fallback : it->second;
}
} // namespace bacuri::vms::recording
