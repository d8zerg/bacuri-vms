#include "modules/access/session.hpp"
#pragma once
#include "application/control_api/api.hpp"
namespace bacuri::vms::control
{
struct FeedBatch {
    std::vector<std::string> frames;
    unsigned close_code{};
};
class AlertFeed
{
  public:
    AlertFeed(const Sessions& sessions, const Key& key, Request request, std::string site)
        : sessions_{sessions}, key_{key}, request_{std::move(request)}, site_{std::move(site)}
    {
    }
    Json snapshot();
    void authorize();
    FeedBatch poll(std::string_view after);

  private:
    struct Scope {
        std::string user, context, policy_revision;
        std::int64_t revision, head, floor, work_revision;
        bool admin;
    };
    Scope scope(postgresql::Database& db) const;
    std::string cursor(const Scope&, std::int64_t position, bool pending = false) const;
    const Sessions& sessions_;
    const Key& key_;
    Request request_;
    std::string site_, policy_revision_;
    std::int64_t position_{}, revision_{}, resume_head_{}, work_revision_{};
    bool subscribed_{}, pending_{};
};
inline constexpr std::string_view alert_protocol = "bacuri.vms.alerts.v1";
} // namespace bacuri::vms::control
