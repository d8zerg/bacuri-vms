#include "control/events/video_rules.hpp"
#include "control/events/operations.hpp"
#include "control/recordings/search.hpp"
#include "control/access/operations.hpp"
#include "control/access/session.hpp"
#include "control/inventory/operations.hpp"
#include "control/playback_time.hpp"
#include "archive/analysis.hpp"
#include "archive/recording_policy.hpp"
#include <cmath>
#include <condition_variable>

namespace bacuri::vms::control::events
{
namespace
{
using archive::MotionArea;
struct Detector {
    bool enabled, record, notify;
    std::string source, preset;
    int threshold, minimum, cooldown;
    std::vector<MotionArea> include, exclude;
    std::vector<double> line;
    archive::CaptureRule schedule;
};
Detector parse(const Json& body)
{
    try {
        const auto& areas = body.at("include");
        const auto& exclusions = body.at("exclude");
        if (body.size() != 13 || !areas.is_array() || !exclusions.is_array() || areas.size() > 8 ||
            exclusions.size() > 8)
            throw std::invalid_argument("areas");
        Detector d{};
        d.enabled = body.at("enabled");
        d.record = body.at("record");
        d.notify = body.at("notify");
        d.source = body.at("source").get<std::string>();
        d.preset = body.at("preset").get<std::string>();
        d.threshold = body.at("threshold");
        d.minimum = body.at("min_cells");
        d.cooldown = body.at("cooldown");
        if ((d.source != "local" && d.source != "onvif") || d.preset.size() > 128 ||
            (!d.record && !d.notify && d.preset.empty()) || !body.at("threshold").is_number_integer() ||
            !body.at("min_cells").is_number_integer() || !body.at("cooldown").is_number_integer() ||
            d.threshold < 1 || d.threshold > 255 || d.minimum < 1 || d.minimum > 192 || d.cooldown < 1 ||
            d.cooldown > 3600)
            throw std::invalid_argument("threshold");
        for (const auto& a : areas)
            d.include.push_back(MotionArea::parse(a));
        for (const auto& a : exclusions)
            d.exclude.push_back(MotionArea::parse(a));
        if (!body.at("line").is_null()) {
            d.line = body.at("line").get<std::vector<double>>();
            if (d.line.size() != 4 ||
                std::ranges::any_of(d.line, [](double x) { return !std::isfinite(x) || x < 0 || x > 1; }) ||
                (d.line[0] == d.line[2] && d.line[1] == d.line[3]))
                throw std::invalid_argument("line");
        }
        if (d.source == "onvif" && (!areas.empty() || !exclusions.empty() || !d.line.empty()))
            throw std::invalid_argument("No ONVIF motion geometry");
        d.schedule = archive::parse_capture_rule({{"continuous", body.at("schedule").empty()},
                                                  {"event", false},
                                                  {"manual", false},
                                                  {"audio", false},
                                                  {"pre", 0},
                                                  {"post", 1},
                                                  {"days", nullptr},
                                                  {"zone", body.at("zone")},
                                                  {"schedule", body.at("schedule")}});
        return d;
    } catch (const std::exception&) {
        throw Failure{400, "invalid_request"};
    }
}
bool matches(const Detector& d, std::int64_t at, bool local, const Json& sample)
{
    if ((d.source == "local") != local || !d.schedule.selects(at, 1))
        return false;
    if (!local)
        return sample.contains("topic");
    const auto a = archive::measure_motion(sample.at("cells"), d.threshold, d.include, d.exclude);
    if (a.cells < d.minimum)
        return false;
    if (d.line.empty())
        return true;
    auto prev = sample.value("previous", std::string{});
    if (prev.empty())
        return false;
    const auto b = archive::measure_motion(prev, d.threshold, d.include, d.exclude);
    if (b.cells < d.minimum)
        return false;
    auto side = [](double x, double y, double u, double v, double px, double py) {
        return (u - x) * (py - y) - (v - y) * (px - x);
    };
    const auto& l = d.line;
    return side(l[0], l[1], l[2], l[3], a.x, a.y) * side(l[0], l[1], l[2], l[3], b.x, b.y) < 0 &&
           side(a.x, a.y, b.x, b.y, l[0], l[1]) * side(a.x, a.y, b.x, b.y, l[2], l[3]) <= 0;
}
} // namespace
void dispatch_presets(const std::stop_token& stop, const std::string& site, const PresetDispatch& dispatch)
{
    using namespace std::chrono_literals;
    std::mutex mutex;
    std::unique_lock lock{mutex};
    std::condition_variable_any delay;
    while (!stop.stop_requested()) {
        try {
            Database db;
            auto job = db.exec(
                "UPDATE bacuri_events.video_matches m SET preset_state='dispatching' WHERE "
                "(site_id,source_id,observation_id) IN "
                "(SELECT site_id,source_id,observation_id FROM bacuri_events.video_matches WHERE site_id=$1 AND "
                "preset_state='pending' ORDER BY at_us LIMIT 1 FOR UPDATE SKIP LOCKED) "
                "RETURNING "
                "point_id,binding,context->>'author',observation_id,context->>'preset',source_id,revision,at_us",
                {site});
            if (job.size()) {
                // Commands dispatched before a restart have an indeterminate outcome. Never repeat an
                // unacknowledged operation.
                std::string status = "rejected";
                auto current = db.exec("SELECT revision FROM bacuri_events.video_rules WHERE site_id=$1 AND "
                                       "point_id=$2 ORDER BY revision DESC LIMIT 1",
                                       {site, job.get(0)});
                if (current.size() && current.get(0) == job.get(0, 6) &&
                    std::abs(g_get_real_time() - std::stoll(job.get(0, 7))) < 10000000) {
                    try {
                        dispatch(job.get(0), std::stoll(job.get(0, 1)), job.get(0, 2), job.get(0, 3),
                                 job.get(0, 4));
                        status = "acknowledged";
                    } catch (const Failure& f) {
                        status = f.code == "camera_ptz_unknown" ? "unknown" : "rejected";
                    } catch (...) {
                        status = "unknown";
                    }
                }
                db.exec("UPDATE bacuri_events.video_matches SET preset_state=$4 WHERE site_id=$1 AND source_id=$2 "
                        "AND observation_id=$3",
                        {site, job.get(0, 5), job.get(0, 3), status});
                continue;
            }
            db.exec("UPDATE bacuri_events.video_matches SET preset_state='unknown' WHERE site_id=$1 AND "
                    "preset_state='dispatching' AND at_us<$2",
                    {site, std::to_string(g_get_real_time() - 60000000)});
        } catch (const std::exception&) {
            std::fputs("Video rule reaction deferred\n", stderr);
        }
        delay.wait_for(lock, stop, 500ms, [] { return false; });
    }
}
std::vector<std::string> detector_points(Database& db, const std::string& site)
{
    return db
        .exec("SELECT point_id FROM (SELECT DISTINCT ON(point_id) point_id,definition FROM "
              "bacuri_events.video_rules WHERE site_id=$1 ORDER BY point_id,revision DESC) current_rules "
              "WHERE definition->>'enabled'='true' AND definition->>'source'='local'",
              {site})
        .column();
}
RuleDecision evaluate_observation(Database& db, const std::string& site, const std::string& point,
                                  const std::string& binding, const std::string& source, const std::string& obs,
                                  std::int64_t at, bool local, bool device, const Json& attrs)
{
    auto rule = db.exec("SELECT revision,definition,author_id FROM bacuri_events.video_rules WHERE site_id=$1 AND "
                        "point_id=$2 AND effective_us<=$3 ORDER BY revision DESC LIMIT 1",
                        {site, point, std::to_string(at)});
    if (!rule.size())
        return {!local, !local};
    auto d = parse(Json::parse(rule.get(0, 1)));
    if ((!local && !device) || !d.enabled || !matches(d, at, local, attrs))
        return {};
    auto revision = rule.get(0);
    db.exec("SELECT pg_advisory_xact_lock(hashtextextended($1,8))", {point});
    if (db.exec("SELECT FROM bacuri_events.video_matches WHERE site_id=$1 AND point_id=$2 AND binding=$3 AND "
                "revision=$4 AND at_us BETWEEN $5 AND $6 LIMIT 1",
                {site, point, binding, revision, std::to_string(at - d.cooldown * 1000000LL + 1),
                 std::to_string(at + d.cooldown * 1000000LL - 1)})
            .size())
        return {};
    db.exec("INSERT INTO "
            "bacuri_events.video_matches(site_id,source_id,observation_id,point_id,binding,revision,at_us,context,"
            "preset_state) VALUES($1,$2,$3,$4,$5,$6,$7,$8,$9)",
            {site, source, obs, point, binding, revision, std::to_string(at),
             Json{{"source", d.source},
                  {"include", Json::parse(rule.get(0, 1)).at("include")},
                  {"topic", attrs.value("topic", "")},
                  {"record", d.record},
                  {"notify", d.notify},
                  {"preset", d.preset},
                  {"author", rule.get(0, 2)}}
                 .dump(),
             d.preset.empty() ? "none" : "pending"});
    return {d.record, d.notify};
}
Json video_context(Database& db, const std::string& site, const std::string& point, std::int64_t begin,
                   std::int64_t end, const std::string& query)
{
    auto rows = db.exec(
        "SELECT "
        "o.observed_at_utc_us,o.observation_id,coalesce(m.revision,0),coalesce(o.attributes->>'topic',o.type),"
        "coalesce(m.context->>'source',o.origin),coalesce(o.attributes->'data','{}'::jsonb),a.alert_id "
        "FROM bacuri_events.observations o LEFT JOIN bacuri_events.video_matches m "
        "USING(site_id,source_id,observation_id) LEFT JOIN bacuri_events.alerts a "
        "USING(site_id,source_id,observation_id) "
        "WHERE o.site_id=$1 AND o.point_id=$2 AND o.type='motion' AND o.observed_at_utc_us>=$3 AND "
        "o.observed_at_utc_us<$4 AND ($5='' OR strpos(lower(concat_ws(' ',o.observation_id::text,"
        "coalesce(o.attributes->>'topic',o.type),coalesce(m.context->>'source',o.origin),"
        "(o.attributes->'data')::text,a.alert_id::text)),lower($5))>0) ORDER BY o.observed_at_utc_us LIMIT 256",
        {site, point, std::to_string(begin), std::to_string(end), query});
    auto items = Json::array();
    for (int i = 0; i < rows.size(); i++)
        items.push_back({{"at", ProtoTime::ToString(ProtoTime::MicrosecondsToTimestamp(std::stoll(rows.get(i))))},
                         {"id", rows.get(i, 1)},
                         {"rule_revision", rows.get(i, 2)},
                         {"topic", rows.get(i, 3)},
                         {"source", rows.get(i, 4)},
                         {"metadata", Json::parse(rows.get(i, 5))},
                         {"alert_id", rows.get(i, 6)}});
    return items;
}
void video_rule(const Request& req, Response& res, const Sessions& sessions, const std::string& site,
                const std::string& point, bool simulation)
{
    if (simulation)
        allow(req, res, {http::verb::post});
    else
        allow(req, res, {http::verb::get, http::verb::put});
    Database db;
    db.exec("BEGIN");
    access::lock_policy(db, site, true);
    auto user = sessions.authenticate(db, req, req.method() != http::verb::get).user;
    access::site_role(db, site, user, access::RoleLock::shared);
    if (!inventory::lock_point(db, site, point) || !access::allowed(db, site, user, point, "configure"))
        throw Failure{403, "forbidden"};
    auto rows = db.exec("SELECT revision,definition FROM bacuri_events.video_rules WHERE site_id=$1 AND "
                        "point_id=$2 ORDER BY revision DESC LIMIT 1",
                        {site, point});
    auto revision = rows.size() ? rows.get(0) : "0";
    if (req.method() == http::verb::get) {
        respond(res,
                {{"revision", revision}, {"rule", rows.size() ? Json::parse(rows.get(0, 1)) : Json(nullptr)}});
        return;
    }
    auto body = object_body(req, {"rule", "revision", "interval"});
    if (!body.contains("rule"))
        throw Failure{400, "invalid_request"};
    auto d = parse(body.at("rule"));
    if (simulation) {
        if (!body.contains("interval"))
            throw Failure{400, "invalid_request"};
        auto span = window(body.at("interval"));
        if (!access::allowed(db, site, user, point, "view", "archive", std::to_string(span.start),
                             std::to_string(span.end)))
            throw Failure{403, "forbidden"};
        Json hits = Json::array();
        std::int64_t last = std::numeric_limits<std::int64_t>::min() / 2;
        bool complete = true;
        if (d.source == "onvif") {
            auto events =
                db.exec("SELECT observed_at_utc_us,attributes FROM bacuri_events.observations WHERE site_id=$1 "
                        "AND point_id=$2 AND origin='onvif' AND type='motion' AND observed_at_utc_us>=$3 AND "
                        "observed_at_utc_us<$4 ORDER BY observed_at_utc_us LIMIT 4097",
                        {site, point, std::to_string(span.start), std::to_string(span.end)});
            complete = events.size() <= 4096;
            for (int i = 0; i < std::min(4096, events.size()); i++) {
                auto at = std::stoll(events.get(i));
                if (at - last >= d.cooldown * 1000000LL && matches(d, at, false, Json::parse(events.get(i, 1)))) {
                    hits.push_back(ProtoTime::ToString(ProtoTime::MicrosecondsToTimestamp(at)));
                    last = at;
                }
            }
        } else {
            auto blocks = recordings::indexed_blocks(db, site, point, span.start, span.end, 65);
            std::string prev, binding;
            std::int64_t previous_at = 0;
            complete = blocks.size() <= 64 && !blocks.empty();
            auto covered = span.start;
            int n = 0;
            for (const auto& b : blocks) {
                if (++n > 64)
                    break;
                if (b.at("begin").get<std::int64_t>() > covered)
                    complete = false;
                covered = std::max(covered, b.at("end").get<std::int64_t>());
                if (b.at("index").is_null()) {
                    complete = false;
                    prev.clear();
                    continue;
                }
                for (const auto& sample : b["index"]["samples"]) {
                    auto at = b.at("begin").get<std::int64_t>() + sample.at("at").get<std::int64_t>();
                    if (at < span.start || at >= span.end)
                        continue;
                    if (at - previous_at > 1000000 || b.at("binding") != binding)
                        prev.clear();
                    if (at - last >= d.cooldown * 1000000LL &&
                        matches(d, at, true, {{"cells", sample.at("cells")}, {"previous", prev}})) {
                        hits.push_back(ProtoTime::ToString(ProtoTime::MicrosecondsToTimestamp(at)));
                        last = at;
                    }
                    prev = sample.at("cells");
                    binding = b.at("binding");
                    previous_at = at;
                }
            }
            complete = complete && covered >= span.end;
        }
        respond(res, {{"hits", hits}, {"complete", complete}});
        return;
    }
    if (!body.contains("revision") || body.at("revision") != revision)
        throw Failure{409, "revision_conflict"};
    if (!d.preset.empty() && !access::allowed(db, site, user, point, "ptz"))
        throw Failure{403, "forbidden"};
    auto saved = db.exec("INSERT INTO bacuri_events.video_rules VALUES($1,$2,$3::bigint+1,$4,$5,$6) ON CONFLICT "
                         "DO NOTHING RETURNING revision",
                         {site, point, revision, std::to_string(g_get_real_time()), user, body.at("rule").dump()});
    if (!saved.size())
        throw Failure{409, "revision_conflict"};
    record_recording_change(db, site, point, user, "video_rule", saved.get(0));
    db.exec("COMMIT");
    respond(res, {{"revision", saved.get(0)}, {"rule", body.at("rule")}});
}
} // namespace bacuri::vms::control::events
