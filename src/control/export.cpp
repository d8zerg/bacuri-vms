#include "control/export.hpp"
#include "control/media_sessions/operations.hpp"
#include "control/events/operations.hpp"
#include "control/recordings/operations.hpp"
#include "control/access/operations.hpp"
#include "control/playback.hpp"
#include "control/playback_time.hpp"
#include "archive/evidence.hpp"
#include "archive/clip.hpp"
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
        db.exec("BEGIN ISOLATION LEVEL REPEATABLE READ READ ONLY");
        const auto lease = access::export_lease(db, browser, grant);
        const auto ms = lease ? std::min(lease->milliseconds,
                                         recordings::export_lifetime_ms(db, lease->site, lease->point, hashes))
                              : 0;
        db.exec("COMMIT");
        if (ms <= 0)
            throw Failure{403, "forbidden"};
        return now + std::chrono::milliseconds{ms};
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
    auto spans = recordings::coverage(db, site, point, std::to_string(a), std::to_string(b), "[]", revision);
    for (const auto& span : spans) {
        if (span.begin != a || span.state == "recorded")
            throw Failure{409, "media_unavailable"};
        const auto end = span.end;
        add_gap(periods, a, end, span.reason);
        a = end;
    }
    if (a != b)
        throw Failure{409, "media_unavailable"};
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
        const auto permission = access::export_permission(auth, site, point, who.user, start, end);
        auto stream = std::make_shared<ExportBody>();
        auto& db = stream->db;
        stream->grant = permission;
        stream->browser = browser;
        std::string revision;
        if (!alert.empty()) {
            auto anchor = events::archive_anchor(db, site, alert);
            if (!anchor || anchor->point != point)
                throw Failure{404, "not_found"};
            const auto allowed = alert_window(anchor->time);
            if (bounds.start < allowed.start || bounds.end > allowed.end)
                throw Failure{403, "forbidden"};
            revision = anchor->revision;
        }
        recordings::require_available(db, site);
        auto blocks = recordings::manifests_for_export(db, site, point, start, end, revision);
        if (blocks.empty())
            throw Failure{409, "media_unavailable"};
        if (blocks.size() > 256)
            throw Failure{413, "export_too_large"};
        std::vector<Json> sources;
        auto hashes = Json::object();
        std::int64_t available = archive::evidence_limit;
        for (const auto& block : blocks) {
            auto m = Json::parse(block);
            archive::validate_manifest(m);
            auto size = m.at("media").at("size_bytes").get<std::int64_t>();
            if (size > available)
                throw Failure{413, "export_too_large"};
            available -= size;
            if (!sources.empty() && sources.back().at("end_utc_us") > m.at("begin_utc_us"))
                throw Failure{409, "media_unavailable"};
            hashes[m.at("block_id").get<std::string>()] = m.at("media").at("sha256");
            sources.push_back(std::move(m));
        }
        stream->hashes = hashes.dump();
        const auto session =
            media_sessions::open_export(db, browser, site, point, sources.front().at("begin_utc_us").dump(),
                                        sources.back().at("end_utc_us").dump());
        struct CloseSession {
            Database& db;
            const std::string& id;
            ~CloseSession()
            {
                // The session also expires if the database goes offline during preparation.
                try {
                    media_sessions::close_export(db, id);
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
        events::record_export(db, site, point, who.user, alert, "applied", "release", details.dump());
        res.result(http::status::ok);
        res.set(http::field::content_type, "application/x-tar");
        res.set(http::field::content_disposition, "attachment; filename=\"bacuri-" + id + ".tar\"");
        res.media_size = fs::file_size(tar);
        fs::remove_all(stream->root);
        res.media = std::move(stream);
    } catch (const Failure& error) {
        events::record_export(auth, site, point, who.user, alert, "denied", error.code, details.dump());
        throw;
    } catch (const std::exception&) {
        events::record_export(auth, site, point, who.user, alert, "denied", "media_unavailable", details.dump());
        throw Failure{503, "media_unavailable"};
    }
}
} // namespace bacuri::vms::control
