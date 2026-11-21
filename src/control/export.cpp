#include "control/playback.hpp"
#include "control/playback_time.hpp"
#include "archive/evidence.hpp"
#include "archive/clip.hpp"
#include "archive_sql.hpp"
#include <fstream>
#include <semaphore>

namespace bacuri::vms::control
{
namespace
{
namespace fs = std::filesystem;
using namespace std::chrono_literals;
std::counting_semaphore<2> export_slots{2};
struct Slot {
    Slot()
    {
        if (!export_slots.try_acquire())
            throw Failure{429, "rate_limited"};
    }
    ~Slot()
    {
        export_slots.release();
    }
};
void write_text(const fs::path& path, const std::string& value)
{
    std::ofstream out(path, std::ios::binary);
    out.exceptions(std::ios::failbit | std::ios::badbit);
    out << value;
    out.close();
}
class ExportBody final : public MediaBody
{
  public:
    ExportBody()
    {
        std::unique_ptr<char, decltype(&g_free)> temp{g_dir_make_tmp("bacuri-export-XXXXXX", nullptr), g_free};
        if (!temp)
            throw std::runtime_error("Cannot create temporary export directory");
        root = temp.get();
    }
    ~ExportBody() override
    {
        std::error_code error;
        fs::remove_all(root, error);
    }
    auto authorize()
    {
        const auto now = std::chrono::steady_clock::now();
        auto rows = db.exec(R"sql(
            SELECT least(1000, floor(extract(epoch FROM least(b.expires_at,b.last_active+interval '30 minutes')
                    -clock_timestamp())*1000),
                floor((coverage.first_end::numeric-bacuri_archive.retention_now_us()
                    +policy.days*86400000000::bigint)/1000))::bigint
            FROM browser_sessions b JOIN visible_points v USING(user_id)
            JOIN export_grants g USING(user_id,site_id,point_id)
            LEFT JOIN archive_retention policy ON policy.site_id=v.site_id
            CROSS JOIN LATERAL (
                SELECT min(block.end_utc_us) AS first_end, count(block.block_id)=count(*) AS complete
                FROM jsonb_each_text($3::jsonb) source
                LEFT JOIN bacuri_archive.retained_blocks block ON block.site_id=v.site_id AND block.point_id=v.point_id
                    AND block.block_id=source.key::uuid AND block.manifest#>>'{media,sha256}'=source.value
                    AND block.unavailable_reason IS NULL
            ) coverage
            WHERE b.token_hash=$1 AND g.grant_id=$2 AND coverage.complete
            AND NOT EXISTS (SELECT FROM archive_recovery WHERE site_id=v.site_id)
        )sql",
                            {browser, grant, hashes});
        if (!rows.size() || std::stoll(rows.get(0)) <= 0)
            throw Failure{403, "forbidden"};
        return now + std::chrono::milliseconds{std::stoll(rows.get(0))};
    }
    MediaChunk next() override
    {
        if (std::chrono::steady_clock::now() + 100ms >= deadline)
            deadline = authorize();
        constexpr std::streamsize chunk_size = 65536;
        std::string bytes(static_cast<std::size_t>(chunk_size), '\0');
        input.read(bytes.data(), chunk_size);
        bytes.resize(input.gcount());
        return {std::move(bytes), deadline};
    }
    Slot slot;
    Database db;
    fs::path root;
    std::string browser, grant, hashes;
    std::ifstream input;
    std::chrono::steady_clock::time_point deadline{};
};
void pack(const fs::path& root, const std::vector<std::string>& clips)
{
    std::vector<std::string> args{"/usr/bin/tar", "--format=ustar", "-cf",         "../export.tar",
                                  "--",           "manifest.json",  "manifest.sig"};
    args.insert(args.end(), clips.begin(), clips.end());
    std::vector<char*> argv;
    for (auto& arg : args)
        argv.push_back(arg.data());
    argv.push_back(nullptr);
    int status{};
    if (!g_spawn_sync((root / "data").c_str(), argv.data(), nullptr, G_SPAWN_DEFAULT, nullptr, nullptr, nullptr,
                      nullptr, &status, nullptr) ||
        !g_spawn_check_wait_status(status, nullptr))
        throw std::runtime_error("Cannot package export files");
}
void add_gap(Json& periods, std::int64_t a, std::int64_t b, const std::string& reason)
{
    if (b > a)
        periods.push_back({{"state", "gap"}, {"begin_utc_us", a}, {"end_utc_us", b}, {"reason", reason}});
}
void catalog_gaps(Database& db, Json& periods, const std::string& site, const std::string& point,
                  const std::string& revision, std::int64_t a, std::int64_t b)
{
    if (a >= b)
        return;
    auto spans =
        db.exec(archive::sql::search, {site, point, std::to_string(a), std::to_string(b), "[]", revision});
    for (int i = 0; i < spans.size(); ++i) {
        if (std::stoll(spans.get(i)) != a || spans.get(i, 2) == "recorded")
            throw Failure{409, "media_unavailable"};
        const auto end = std::stoll(spans.get(i, 1));
        add_gap(periods, a, end, spans.get(i, 5));
        a = end;
    }
    if (a != b)
        throw Failure{409, "media_unavailable"};
}
void audit(Database& db, const std::string& site, const std::string& point, const std::string& user,
           const std::string& alert, const std::string& outcome, const std::string& reason, const Json& details)
{
    db.exec("INSERT INTO "
            "bacuri_events.alert_audit(site_id,point_id,initiator_id,alert_id,actor_kind,action,outcome,reason,"
            "export_details) "
            "SELECT site_id,$2,$3,nullif($4,'')::uuid,'user','export',$5,$6,$7::jsonb FROM sites WHERE site_id=$1",
            {site, point, user, alert, outcome, reason, details.dump()});
}
} // namespace
void export_fragment(const Request& req, Response& res, const Sessions& sessions, const Key& key,
                     const std::string& site, const std::string& point)
{
    allow(req, res, {http::verb::post});
    Database auth;
    const auto who = sessions.authenticate(auth, req, true);
    const auto browser = fingerprint(key, "session", who.cookie);
    const auto body = object_body(req, {"interval", "alert_id"});
    if (!body.contains("interval"))
        throw Failure{400, "invalid_request"};
    const auto bounds = window(body.at("interval"));
    std::string alert;
    if (body.contains("alert_id")) {
        alert = string_field(body, "alert_id", 36);
        if (!canonical_id(alert))
            throw Failure{400, "invalid_request"};
    }
    const auto id = archive::uuid(), start = std::to_string(bounds.start), end = std::to_string(bounds.end);
    const Json details{{"id", id}, {"interval", bounds.json()}};
    try {
        auto grant =
            auth.exec("SELECT grant_id FROM visible_points JOIN export_grants USING(user_id,site_id,point_id) "
                      "WHERE user_id=$1 AND site_id=$2 AND point_id=$3 AND begin_us<=$4 AND end_us>=$5",
                      {who.user, site, point, start, end});
        if (!grant.size())
            throw Failure{403, "forbidden"};
        auto stream = std::make_shared<ExportBody>();
        auto& db = stream->db;
        stream->grant = grant.get(0);
        stream->browser = browser;
        std::string revision;
        if (!alert.empty()) {
            auto a = db.exec("SELECT binding_revision,observed_at_utc_us FROM bacuri_events.alert_details WHERE "
                             "site_id=$1 AND point_id=$2 AND alert_id=$3",
                             {site, point, alert});
            if (!a.size())
                throw Failure{404, "not_found"};
            const auto allowed = alert_window(std::stoll(a.get(0, 1)));
            if (bounds.start < allowed.start || bounds.end > allowed.end)
                throw Failure{403, "forbidden"};
            revision = a.get(0);
        }
        if (db.exec("SELECT FROM archive_recovery WHERE site_id=$1", {site}).size())
            throw Failure{503, "media_unavailable"};
        auto blocks = db.exec(
            "SELECT manifest FROM bacuri_archive.retained_blocks WHERE site_id=$1 AND point_id=$2 AND "
            "int8range(begin_utc_us,end_utc_us,'[)') && int8range($3,$4,'[)') AND unavailable_reason IS NULL AND "
            "($5='' OR binding_revision=nullif($5,'')::bigint) ORDER BY begin_utc_us,end_utc_us LIMIT 257",
            {site, point, start, end, revision});
        if (!blocks.size())
            throw Failure{409, "media_unavailable"};
        if (blocks.size() > 256)
            throw Failure{413, "export_too_large"};
        std::vector<Json> sources;
        auto hashes = Json::object();
        std::int64_t available = archive::evidence_limit;
        for (int i = 0; i < blocks.size(); ++i) {
            auto m = Json::parse(blocks.get(i));
            archive::validate_manifest(m);
            auto size = m.at("media").at("size_bytes").get<std::int64_t>();
            if (size > available)
                throw Failure{413, "export_too_large"};
            available -= size;
            if (i && sources.back().at("end_utc_us") > m.at("begin_utc_us"))
                throw Failure{409, "media_unavailable"};
            hashes[m.at("block_id").get<std::string>()] = m.at("media").at("sha256");
            sources.push_back(std::move(m));
        }
        stream->hashes = hashes.dump();
        const auto session =
            db.exec("INSERT INTO media_sessions(browser,site_id,point_id,begin_us,end_us,expires_at) "
                    "VALUES($1,$2,$3,$4,$5,now()+interval '5 minutes') RETURNING id",
                    {browser, site, point, sources.front().at("begin_utc_us").dump(),
                     sources.back().at("end_utc_us").dump()})
                .get(0);
        struct CloseSession {
            Database& db;
            const std::string& id;
            ~CloseSession()
            {
                // The session also expires if the database goes offline during preparation.
                try {
                    db.exec("DELETE FROM media_sessions WHERE id=$1", {id});
                } catch (const std::exception&) {
                    std::fputs("Export session cleanup deferred until expiry\n", stderr);
                }
            }
        } close_session{db, session};
        const auto dir = stream->root / "data";
        fs::create_directory(dir);
        auto periods = Json::array();
        std::vector<std::string> clips;
        auto cursor = bounds.start;
        available = archive::evidence_limit;
        for (const auto& m : sources) {
            stream->authorize();
            auto a = m.at("begin_utc_us").get<std::int64_t>(), b = m.at("end_utc_us").get<std::int64_t>();
            auto from = std::max(a, bounds.start), until = std::min(b, bounds.end);
            catalog_gaps(db, periods, site, point, revision, cursor, from);
            cursor = until;
            const auto file = "clip-" + std::to_string(clips.size() + 1) + ".mp4";
            const auto input = stream->root / "source.mp4";
            copy_export_block(session, m, browser, input);
            if (m.at("media").at("sha256") != archive::file_digest(input).sha256)
                throw Failure{503, "media_unavailable"};
            Json clip{{"source", m}, {"state", "recorded"}, {"file", file}, {"transform", "original"}};
            if (a == from && b == until) {
                fs::rename(input, dir / file);
            } else {
                auto frames = archive::select_frames(input, dir / file, from - a, until - a);
                fs::remove(input);
                if (!frames.count) {
                    add_gap(periods, from, until, "no_frame");
                    continue;
                }
                add_gap(periods, from, a + frames.begin_us, "no_frame");
                from = a + frames.begin_us;
                until = a + frames.end_us;
                clip["transform"] = "h264_frame_selection";
                clip["frame_count"] = frames.count;
            }
            const auto digest = archive::file_digest(dir / file);
            if (digest.bytes > available)
                throw Failure{413, "export_too_large"};
            available -= digest.bytes;
            clip.update(Json{{"sha256", digest.sha256},
                             {"size_bytes", digest.bytes},
                             {"begin_utc_us", from},
                             {"end_utc_us", until}});
            clips.push_back(file);
            periods.push_back(std::move(clip));
            add_gap(periods, until, cursor, "no_frame");
        }
        if (clips.empty())
            throw Failure{409, "media_unavailable"};
        catalog_gaps(db, periods, site, point, revision, cursor, bounds.end);
        Json manifest{{"format", "bacuri.vms.export.v1"},
                      {"site_id", site},
                      {"point_id", point},
                      {"export_id", id},
                      {"begin_utc_us", bounds.start},
                      {"end_utc_us", bounds.end},
                      {"periods", std::move(periods)}};
        if (!alert.empty())
            manifest["alert_id"] = alert;
        const auto description = manifest.dump(2);
        if (description.size() > 4uz * 1024 * 1024)
            throw Failure{413, "export_too_large"};
        write_text(dir / "manifest.json", description);
        write_text(dir / "manifest.sig",
                   archive::sign_description(load_key(setting("BACURI_EXPORT_KEY_FILE")), description));
        pack(stream->root, clips);
        const auto tar = stream->root / "export.tar";
        stream->input.open(tar, std::ios::binary);
        if (!stream->input)
            throw std::runtime_error("Cannot open export package");
        stream->authorize();
        audit(db, site, point, who.user, alert, "applied", "release", details);
        res.result(http::status::ok);
        res.set(http::field::content_type, "application/x-tar");
        res.set(http::field::content_disposition, "attachment; filename=\"bacuri-" + id + ".tar\"");
        res.media_size = fs::file_size(tar);
        fs::remove_all(stream->root);
        res.media = std::move(stream);
    } catch (const Failure& error) {
        audit(auth, site, point, who.user, alert, "denied", error.code, details);
        throw;
    } catch (const std::exception&) {
        audit(auth, site, point, who.user, alert, "denied", "media_unavailable", details);
        throw Failure{503, "media_unavailable"};
    }
}
} // namespace bacuri::vms::control
