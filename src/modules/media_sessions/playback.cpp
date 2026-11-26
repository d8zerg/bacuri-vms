#include "modules/inventory/operations.hpp"
#include "modules/events/operations.hpp"
#include "modules/access/operations.hpp"
#include "modules/recordings/operations.hpp"
#include "modules/media_sessions/playback.hpp"
#include "application/control_api/playback_time.hpp"
#include "modules/media_sessions/playback_operations.hpp"
#include "domain/time.hpp"
namespace bacuri::vms::control
{
namespace
{
using namespace std::chrono_literals;

Json query_parameters(std::string_view target)
{
    Json fields = Json::object();
    const auto separator = target.find('?');
    if (separator == target.npos) {
        return fields;
    }
    target.remove_prefix(separator + 1);
    while (!target.empty()) {
        auto end = target.find('&');
        auto pair = target.substr(0, end);
        auto eq = pair.find('=');
        auto key = pair.substr(0, eq);
        if (eq == pair.npos || (key != "start" && key != "end" && key != "cursor" && key != "presentation") ||
            fields.contains(key)) {
            throw Failure{ErrorCode::invalid_request};
        }
        const std::unique_ptr<char, decltype(&g_free)> value{
            g_uri_unescape_segment(pair.data() + eq + 1, pair.data() + pair.size(), nullptr), g_free};
        if (!value) {
            throw Failure{ErrorCode::invalid_request};
        }
        fields[key] = value.get();
        if (end == target.npos) {
            break;
        }
        target.remove_prefix(end + 1);
    }
    return fields;
}

} // namespace
void Playback::point(const Request& req, Response& res, const std::string& site, const std::string& point,
                     bool searching) const
{
    allow(req, res, {searching ? http::verb::get : http::verb::post});
    postgresql::Database db;
    db.exec("BEGIN");
    auto who = sessions_.authenticate(db, req, !searching);
    if (!inventory::point_exists(db, site, point))
        throw Failure{ErrorCode::not_found};
    playback::PointAccess access{site, point, fingerprint(key_, "session", who.cookie),
                                 access::site_role(db, site, who.user, access::RoleLock::read), who.user};
    if (searching) {
        respond(res, playback::search(db, key_, access, query_parameters(std::string_view{req.target()})));
    } else {
        respond(res, playback::create(db, key_, access, req), 201);
    }
    db.exec("COMMIT");
}

void Playback::alert_archive(const Request& req, Response& res, const std::string& site,
                             const std::string& id) const
{
    allow(req, res, {http::verb::get});
    postgresql::Database db;
    db.exec("BEGIN");
    auto who = sessions_.authenticate(db, req, false);
    auto anchor = events::archive_anchor(db, site, id);
    if (!anchor)
        throw Failure{ErrorCode::not_found};
    const auto& point = anchor->point;
    auto role = access::role_for_existing_point(db, site, point, who.user);
    if (!access::allowed(db, site, who.user, point, "view", "archive"))
        throw Failure{ErrorCode::not_found};
    recordings::require_available(db, site);
    const auto time = anchor->time;
    Json link{{"point_id", point}, {"state", "uncertain_time"}, {"archive_interval", alert_window(time).json()}};
    if (anchor->basis == "source_clock" && std::stoll(anchor->uncertainty) <= alert_context_us) {
        const auto uncertainty = std::stoll(anchor->uncertainty);
        Window instant{time - uncertainty, time + uncertainty + 1};
        auto blocks =
            recordings::blocks_at(db, site, point, std::to_string(instant.start), std::to_string(instant.end));
        if (blocks.size() == 1 && blocks.front().revision == anchor->revision &&
            blocks.front().basis == "source_clock" && blocks.front().unavailable_reason.empty()) {
            Window block{blocks.front().begin, blocks.front().end};
            auto error = std::stoll(blocks.front().uncertainty);
            using domain::Microseconds;
            using domain::UtcTime;
            auto interval =
                domain::UtcInterval::make(UtcTime{Microseconds{block.start}}, UtcTime{Microseconds{block.end}});
            if (interval && error <= Microseconds::max().count() - uncertainty &&
                interval->contains(UtcTime{Microseconds{time}}, Microseconds{uncertainty + error})) {
                link["state"] = "ready";
                link["interval"] = block.json();
                link["position"] = ProtoTime::ToString(ProtoTime::MicrosecondsToTimestamp(time));
            }
        } else if (blocks.empty() || (blocks.size() == 1 && !blocks.front().unavailable_reason.empty())) {
            auto page =
                playback::search(db, key_, {site, point, fingerprint(key_, "session", who.cookie), role, who.user},
                                 instant.json(), std::stoll(anchor->revision));
            const auto& items = page.at("items");
            if (items.size() == 1 && items[0].at("state") != "recorded") {
                link["state"] = items[0].at("state");
                if (items[0].contains("reason")) {
                    link["reason"] = items[0].at("reason");
                }
            }
        }
    }
    db.exec("COMMIT");
    respond(res, link);
}
} // namespace bacuri::vms::control
