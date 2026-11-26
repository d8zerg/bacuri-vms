#include "recording/plan.hpp"

namespace bacuri::vms::recording
{
using storage::bind_text;
using storage::column;
using storage::next;
namespace
{
auto decode_rules(const std::string& payload)
{
    std::map<std::string, std::shared_ptr<const CaptureRule>> next_rules;
    auto data = nlohmann::json::parse(payload);
    if (!data.is_object())
        throw std::invalid_argument("Invalid recording plan");
    for (const auto& [point, entry] : data.items()) {
        auto rule = parse_capture_rule(entry.at("rule"));
        rule.id = entry.at("id");
        rule.revision = entry.at("revision");
        rule.since = entry.at("since");
        for (const auto& w : entry.at("windows"))
            rule.windows.push_back({w.at(0), w.at(1), w.at(2)});
        next_rules.emplace(point, std::make_shared<const CaptureRule>(std::move(rule)));
    }
    return next_rules;
}
} // namespace

RecordingPlan::RecordingPlan(const std::filesystem::path& archive_root) : db_{archive_root / "journal.db"}
{
    auto saved = db_.query("SELECT payload FROM recording_plan WHERE singleton=1");
    if (next(saved.get())) {
        payload_ = column(saved.get(), 0);
        rules_ = decode_rules(payload_);
    }
}

void RecordingPlan::update(std::string payload)
{
    std::scoped_lock lock{mutex_};
    if (payload_ == payload && !payload.empty())
        return;
    auto next_rules = decode_rules(payload);
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
