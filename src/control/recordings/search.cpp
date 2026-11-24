#include "control/maps.hpp"
#include "control/events/video_rules.hpp"
#include "control/recordings/search.hpp"
#include "control/recordings/operations.hpp"
#include "control/access/operations.hpp"
#include "control/access/session.hpp"
#include "control/playback_time.hpp"
#include "archive/analysis.hpp"
#include <set>

namespace bacuri::vms::control::recordings
{
Json indexed_blocks(Database& db, const std::string& site, const std::string& point, std::int64_t begin,
                    std::int64_t end, int limit)
{
    auto rows = db.exec(
        R"sql(SELECT b.block_id,b.begin_utc_us,b.end_utc_us,b.time_basis,b.uncertainty_us,b.binding_revision,i.samples
        FROM bacuri_archive.retained_blocks b LEFT JOIN bacuri_archive.motion_indexes i USING(site_id,block_id,recording_epoch_id,owner_generation)
        WHERE site_id=$1 AND point_id=$2 AND unavailable_reason IS NULL AND int8range(begin_utc_us,end_utc_us,'[)') && int8range($3,$4,'[)')
        ORDER BY begin_utc_us,block_id LIMIT $5)sql",
        {site, point, std::to_string(begin), std::to_string(end), std::to_string(limit)});
    Json result = Json::array();
    for (int i = 0; i < rows.size(); ++i)
        result.push_back(
            {{"id", rows.get(i)},
             {"begin", std::stoll(rows.get(i, 1))},
             {"end", std::stoll(rows.get(i, 2))},
             {"basis", rows.get(i, 3)},
             {"uncertainty", rows.get(i, 4).empty() ? Json(nullptr) : Json(std::stoll(rows.get(i, 4)))},
             {"binding", rows.get(i, 5)},
             {"index", rows.get(i, 6).empty() ? Json(nullptr) : Json::parse(rows.get(i, 6))}});
    return result;
}
void search_index(const Request& req, Response& res, const Sessions& sessions, const std::string& site)
{
    allow(req, res, {http::verb::post});
    Database db;
    db.exec("BEGIN ISOLATION LEVEL REPEATABLE READ");
    const auto user = sessions.authenticate(db, req, true).user;
    access::site_role(db, site, user, access::RoleLock::read);
    require_available(db, site);
    auto body = object_body(
        req, {"points", "interval", "region", "threshold", "min_cells", "motion", "plan", "event_query"});
    if (!body.contains("points") || !body.at("points").is_array() || body.at("points").empty() ||
        body.at("points").size() > 4 || !body.contains("interval"))
        throw Failure{400, "invalid_request"};
    const auto span = window(body.at("interval"));
    int threshold = 24, minimum = 1;
    bool motion = true;
    std::vector<archive::MotionArea> roi;
    try {
        if (body.contains("region") && !body.at("region").is_null())
            roi.push_back(archive::MotionArea::parse(body.at("region")));
        threshold = body.value("threshold", 24);
        minimum = body.value("min_cells", 1);
        motion = body.value("motion", true);
        if (threshold < 1 || threshold > 255 || minimum < 1 || minimum > 192)
            throw std::invalid_argument("Invalid motion threshold");
    } catch (const std::exception&) {
        throw Failure{400, "invalid_request"};
    }
    const auto plan = body.contains("plan") ? string_field(body, "plan", 36) : "",
               event = body.contains("event_query") ? string_field(body, "event_query", 128, true) : "";
    if (!plan.empty() && !canonical_id(plan))
        throw Failure{400, "invalid_request"};
    Json result = Json::array();
    std::set<std::string> unique;
    for (const auto& entry : body.at("points")) {
        if (!entry.is_string() || !canonical_id(entry.get<std::string>()) ||
            !unique.insert(entry.get<std::string>()).second)
            throw Failure{400, "invalid_request"};
        const std::string point = entry;
        std::vector<archive::MotionArea> masks;
        bool permitted = true;
        for (const auto* surface : {"archive", "thumbnail"}) {
            if (access::allowed(db, site, user, point, "view", surface, std::to_string(span.start),
                                std::to_string(span.end)))
                continue;
            auto mask = access::delivery_regions(db, site, point, surface);
            if (mask.empty() || !access::allowed(db, site, user, point, "view", "", std::to_string(span.start),
                                                 std::to_string(span.end))) {
                permitted = false;
                break;
            }
            for (const auto& r : Json::parse(mask))
                masks.push_back(archive::MotionArea::parse(r));
        }
        if (!permitted || (!plan.empty() && !point_on_plan(db, site, point, plan)))
            continue;
        auto contexts =
            masks.empty() ? events::video_context(db, site, point, span.start, span.end, event) : Json::array();
        std::vector<std::int64_t> anchors;
        if (!event.empty()) {
            for (const auto& item : contexts)
                anchors.push_back(parse_utc(item.at("at")));
        }

        Json row{{"point_id", point},       {"coverage", Json::array()}, {"hits", Json::array()},
                 {"frames", Json::array()}, {"gaps", Json::array()},     {"next", nullptr}};
        // Limit a response by blocks, frames and previews. Continuation begins at the first omitted timestamp.
        auto blocks = indexed_blocks(db, site, point, span.start, span.end, 17);
        auto horizon = span.end;
        for (const auto& b : blocks) {
            const auto base = b.at("begin").get<std::int64_t>(), end = b.at("end").get<std::int64_t>();
            if (row["coverage"].size() >= 16 || row["hits"].size() >= 48 || row["frames"].size() >= 4096) {
                horizon = std::max(base, span.start);
                break;
            }
            auto first = std::max(base, span.start), last = std::min(end, horizon);
            const auto& index = b.at("index");
            if (!index.is_null()) {
                for (const auto& sample : index.at("samples")) {
                    const auto at = base + sample.at("at").get<std::int64_t>();
                    if (at < first || at >= last)
                        continue;
                    if (!event.empty() &&
                        std::ranges::none_of(anchors, [&](auto anchor) { return std::abs(at - anchor) < 250000; }))
                        continue;
                    auto measured = archive::measure_motion(sample.at("cells"), threshold, roi, masks);
                    if (motion && measured.cells < minimum)
                        continue;
                    if (row["hits"].size() == 48) {
                        horizon = last = at;
                        break;
                    }
                    row["hits"].push_back({{"at", ProtoTime::ToString(ProtoTime::MicrosecondsToTimestamp(at))},
                                           {"cells", measured.cells},
                                           {"preview", archive::mask_preview(sample.at("image"), masks)}});
                }
                for (const auto& frame : index.at("frames")) {
                    const auto at = base + frame.get<std::int64_t>();
                    if (at >= first && at < last)
                        row["frames"].push_back(ProtoTime::ToString(ProtoTime::MicrosecondsToTimestamp(at)));
                }
            }
            if (last > first)
                row["coverage"].push_back({{"interval", Window{first, last}.json()},
                                           {"indexed", !index.is_null()},
                                           {"basis", b.at("basis")},
                                           {"uncertainty_us", b.at("uncertainty")},
                                           {"binding_revision", b.at("binding")}});
            if (horizon != span.end)
                break;
        }
        if (horizon < span.end)
            row["next"] = ProtoTime::ToString(ProtoTime::MicrosecondsToTimestamp(horizon));
        if (horizon > span.start)
            for (const auto& hole :
                 coverage(db, site, point, std::to_string(span.start), std::to_string(horizon), "[]", ""))
                if (hole.state != "recorded")
                    row["gaps"].push_back(
                        {{"interval", Window{hole.begin, hole.end}.json()}, {"reason", hole.reason}});
        std::erase_if(contexts.get_ref<Json::array_t&>(),
                      [&](const Json& item) { return parse_utc(item.at("at")) >= horizon; });
        row["events"] = std::move(contexts);
        result.push_back(std::move(row));
    }
    db.exec("COMMIT");
    respond(res, result);
}
} // namespace bacuri::vms::control::recordings
