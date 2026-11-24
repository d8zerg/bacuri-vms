#include "control/recordings/policies.hpp"
#include "control/export.hpp"
#include "control/media_sessions/operations.hpp"
#include "control/events/operations.hpp"
#include "control/recordings/operations.hpp"
#include "control/access/operations.hpp"
#include "control/playback.hpp"
#include "control/playback_time.hpp"
#include "archive/evidence.hpp"
#include "archive/clip.hpp"
#include "media/lease.hpp"
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
    explicit ExportBody(std::shared_ptr<Slot> capacity) : slot{std::move(capacity)}
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
    std::chrono::steady_clock::time_point authorize()
    {
        if (group_lease)
            return group_lease();
        const auto now = std::chrono::steady_clock::now();
        db.exec("BEGIN ISOLATION LEVEL REPEATABLE READ READ ONLY");
        const auto lease = access::export_lease(db, browser, grant, point, begin, end, derivative, regions, sound);
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
    std::shared_ptr<Slot> slot;
    std::function<std::chrono::steady_clock::time_point()> group_lease;
    Database db;
    fs::path root;
    std::string browser, grant, hashes, point, begin, end;
    bool derivative{}, sound{};
    std::string regions;
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
Json export_audio(const Json& source, std::int64_t begin, std::int64_t end, const fs::path& dir,
                  const std::string& file)
{
    if (!source.contains("audio"))
        return nullptr;
    const auto& a = source.at("audio");
    const auto start = a.at("start_us").get<std::int64_t>(), size = a.at("size_bytes").get<std::int64_t>();
    const auto first = std::clamp<std::int64_t>((begin - start + 124) / 125, 0, size / 2);
    const auto last = std::clamp<std::int64_t>((end - start) / 125, 0, size / 2);
    if (last <= first)
        return nullptr;
    std::string pcm;
    auto stub = v1::RecorderControl::NewStub(media::local_channel("BACURI_SOCKET"));
    for (auto offset = first * 2; offset < last * 2;) {
        const auto length = std::min<std::int64_t>(16000, last * 2 - offset);
        v1::AudioRequest q;
        q.set_mode(v1::AudioRequest::ARCHIVE);
        q.set_site_id(source.at("site_id").get<std::string>());
        q.set_point_id(source.at("point_id").get<std::string>());
        q.set_block_id(source.at("block_id").get<std::string>());
        q.set_sha256(a.at("sha256").get<std::string>());
        q.set_at_us(offset);
        q.set_length(length);
        grpc::ClientContext ctx;
        ctx.set_deadline(std::chrono::system_clock::now() + 2s);
        v1::AudioChunk chunk;
        if (!stub->ExchangeAudio(&ctx, q, &chunk).ok() || chunk.pcm().size() != static_cast<std::size_t>(length))
            throw Failure{503, "media_unavailable"};
        pcm += chunk.pcm();
        offset += length;
    }
    std::string wav = "RIFF";
    auto le = [&](unsigned value, int count) {
        while (count--) {
            wav.push_back(static_cast<char>(value & 255));
            value >>= 8;
        }
    };
    le(36 + pcm.size(), 4);
    wav += "WAVEfmt ";
    le(16, 4);
    le(1, 2);
    le(1, 2);
    le(8000, 4);
    le(16000, 4);
    le(2, 2);
    le(16, 2);
    wav += "data";
    le(pcm.size(), 4);
    wav += pcm;
    write_text(dir / file, wav);
    const auto digest = archive::file_digest(dir / file);
    return {{"file", file},
            {"codec", "pcm_s16le"},
            {"sample_rate", 8000},
            {"channels", 1},
            {"sha256", digest.sha256},
            {"size_bytes", digest.bytes},
            {"begin_utc_us", start + first * 125},
            {"end_utc_us", start + last * 125}};
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
static std::shared_ptr<ExportBody> prepare_fragment(const Request& req, Response& res, const Sessions& sessions,
                                                    const Key& key, const std::string& site,
                                                    const std::string& point, std::shared_ptr<Slot> capacity,
                                                    const fs::path& material = {})
{
    allow(req, res, {http::verb::post});
    Database auth;
    const auto who = sessions.authenticate(auth, req, true);
    const auto browser = fingerprint(key, "session", who.cookie);
    const auto body = object_body(req, {"interval", "alert_id", "mode", "reason", "audio"});
    if (!body.contains("interval"))
        throw Failure{400, "invalid_request"};
    const auto bounds = window(body.at("interval"));
    const auto mode = body.contains("mode") ? string_field(body, "mode", 16) : std::string{"selection"};
    if (mode != "selection" && mode != "original" && mode != "derivative")
        throw Failure{400, "invalid_request"};
    if (body.contains("audio") && !body.at("audio").is_boolean())
        throw Failure{400, "invalid_request"};
    const bool sound = body.value("audio", false), derivative = mode == "derivative";
    const auto reason = (mode != "selection" || sound || body.contains("reason"))
                            ? string_field(body, "reason", 512)
                            : std::string{};
    if ((mode != "selection" || sound) && reason.find_first_not_of(" \t\r\n") == reason.npos)
        throw Failure{400, "invalid_request"};
    std::string alert;
    if (body.contains("alert_id")) {
        alert = string_field(body, "alert_id", 36);
        if (!canonical_id(alert))
            throw Failure{400, "invalid_request"};
    }
    const auto id = archive::uuid(), start = std::to_string(bounds.start), end = std::to_string(bounds.end);
    const Json details{
        {"id", id}, {"interval", bounds.json()}, {"mode", mode}, {"reason", reason}, {"audio", sound}};
    try {
        const auto permission = access::export_permission(auth, site, point, who.user, start, end, derivative);
        auto stream = std::make_shared<ExportBody>(capacity);
        auto& db = stream->db;
        stream->grant = permission;
        stream->point = point;
        stream->begin = start;
        stream->end = end;
        stream->browser = browser;
        stream->derivative = derivative;
        stream->sound = sound;
        stream->regions = access::export_regions(db, site, point);
        if (sound && !access::allowed(db, site, who.user, point, "audio", "", start, end))
            throw Failure{403, "forbidden"};
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
        if (mode == "original") {
            const auto first = std::min(bounds.start, sources.front().at("begin_utc_us").get<std::int64_t>());
            const auto last = std::max(bounds.end, sources.back().at("end_utc_us").get<std::int64_t>());
            if (!alert.empty()) {
                auto anchor = events::archive_anchor(db, site, alert);
                if (!anchor || first < alert_window(anchor->time).start || last > alert_window(anchor->time).end)
                    throw Failure{403, "forbidden"};
            }
            stream->begin = std::to_string(first);
            stream->end = std::to_string(last);
            stream->grant = access::export_permission(db, site, point, who.user, stream->begin, stream->end);
        }
        Json private_scope{{"export_grant", stream->grant}, {"begin", stream->begin},   {"end", stream->end},
                           {"regions", stream->regions},    {"derivative", derivative}, {"sound", sound}};
        const auto session =
            media_sessions::open_export(db, browser, site, point, sources.front().at("begin_utc_us").dump(),
                                        sources.back().at("end_utc_us").dump(), private_scope.dump());
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
        unsigned ordinal{};
        auto cursor = bounds.start;
        available = archive::evidence_limit;
        for (const auto& m : sources) {
            stream->authorize();
            auto a = m.at("begin_utc_us").get<std::int64_t>(), b = m.at("end_utc_us").get<std::int64_t>();
            auto from = std::max(a, bounds.start), until = std::min(b, bounds.end);
            catalog_gaps(db, periods, site, point, revision, cursor, from);
            cursor = until;
            const auto file = "clip-" + std::to_string(ordinal + 1) + ".mp4";
            const auto input = stream->root / "source.mp4";
            copy_export_block(session, m, browser, input);
            if (m.at("media").at("sha256") != archive::file_digest(input).sha256)
                throw Failure{503, "media_unavailable"};
            Json clip{{"source", m}, {"state", "recorded"}, {"file", file}, {"transform", "original"}};
            if (mode == "original" || (!derivative && a == from && b == until)) {
                fs::rename(input, dir / file);
                if (mode == "original" && (from != a || until != b))
                    clip["transform"] = "original_with_decode_context";
            } else {
                auto frames =
                    archive::select_frames(input, dir / file, from - a, until - a, false, stream->regions);
                fs::remove(input);
                if (!frames.count) {
                    add_gap(periods, from, until, "no_frame");
                    continue;
                }
                add_gap(periods, from, a + frames.begin_us, "no_frame");
                from = a + frames.begin_us;
                until = a + frames.end_us;
                clip["transform"] = stream->regions.empty() ? "h264_frame_selection" : "h264_masked_selection";
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
            ++ordinal;
            if (mode != "selection" || sound) {
                clip["codec"] =
                    (clip.at("transform") == "original" || clip.at("transform") == "original_with_decode_context")
                        ? m.at("media").at("tracks").at(0).at("codec")
                        : Json("h264");
                clip["masks"] = stream->regions.empty() ? Json::array() : Json::parse(stream->regions);
                clip["audio"] =
                    sound ? export_audio(m, from, until, dir, "audio-" + std::to_string(ordinal) + ".wav")
                          : Json(nullptr);
                if (!clip.at("audio").is_null()) {
                    const auto bytes = clip.at("audio").at("size_bytes").get<std::int64_t>();
                    if (bytes > available)
                        throw Failure{413, "export_too_large"};
                    available -= bytes;
                    clips.push_back(clip.at("audio").at("file").get<std::string>());
                }
            }
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
        if (mode != "selection" || sound) {
            manifest["format"] = "bacuri.vms.export.v2";
            manifest["mode"] = mode;
            manifest["reason"] = reason;
            manifest["audio_requested"] = sound;
        }
        if (!alert.empty())
            manifest["alert_id"] = alert;
        const auto description = manifest.dump(2);
        if (description.size() > 4uz * 1024 * 1024)
            throw Failure{413, "export_too_large"};
        write_text(dir / "manifest.json", description);
        write_text(dir / "manifest.sig",
                   archive::sign_description(load_key(setting("BACURI_EXPORT_KEY_FILE")), description));
        if (!material.empty()) {
            stream->authorize();
            fs::rename(dir, material);
            return stream;
        }
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
        res.media = stream;
        return stream;
    } catch (const Failure& error) {
        events::record_export(auth, site, point, who.user, alert, "denied", error.code, details.dump());
        throw;
    } catch (const std::exception&) {
        events::record_export(auth, site, point, who.user, alert, "denied", "media_unavailable", details.dump());
        throw Failure{503, "media_unavailable"};
    }
}
void export_fragment(const Request& req, Response& res, const Sessions& sessions, const Key& key,
                     const std::string& site, const std::string& point)
{
    prepare_fragment(req, res, sessions, key, site, point, std::make_shared<Slot>());
}
void export_case(const Request& req, Response& res, const Sessions& sessions, const Key& key,
                 const std::string& site, const std::string& id)
{
    allow(req, res, {http::verb::post});
    Database auth;
    auto who = sessions.authenticate(auth, req, true);
    auto options = object_body(req, {"mode", "reason", "audio", "revision"});
    const auto reason = string_field(options, "reason", 512), mode = string_field(options, "mode", 16),
               revision = string_field(options, "revision", 19);
    if (mode != "original" && mode != "derivative")
        throw Failure{400, "invalid_request"};
    auto file = recordings::case_for_export(auth, site, id, who.user);
    if (file.at("revision") != revision)
        throw Failure{409, "revision_conflict"};
    auto capacity = std::make_shared<Slot>();
    auto stream = std::make_shared<ExportBody>(capacity);
    auto dir = stream->root / "data";
    fs::create_directory(dir);
    options.erase("revision");
    std::vector<std::shared_ptr<ExportBody>> grants;
    std::vector<std::string> paths;
    std::uintmax_t bytes = 0;
    auto manifest = Json{{"format", "bacuri.vms.case.v1"},
                         {"case", file},
                         {"export_id", archive::uuid()},
                         {"site_id", site},
                         {"reason", reason},
                         {"mode", mode},
                         {"parts", Json::array()}};
    for (const auto& item : file.at("materials")) {
        auto name = "material-" + std::to_string(paths.size() + 1);
        Request inner = req;
        options["interval"] = item.at("interval");
        inner.body() = options.dump();
        inner.prepare_payload();
        Response part;
        grants.push_back(
            prepare_fragment(inner, part, sessions, key, site, item.at("point_id"), capacity, dir / name));
        for (const auto& entry : fs::directory_iterator(dir / name)) {
            bytes += entry.file_size();
            if (bytes > archive::evidence_limit)
                throw Failure{413, "export_too_large"};
        }
        auto bounds = window(item.at("interval"));
        manifest["parts"].push_back(
            {{"directory", name},
             {"manifest_sha256", archive::file_digest(dir / name / "manifest.json").sha256},
             {"point_id", item.at("point_id")},
             {"begin_utc_us", bounds.start},
             {"end_utc_us", bounds.end}});
        paths.push_back(std::move(name));
    }
    const auto description = manifest.dump(2);
    write_text(dir / "manifest.json", description);
    write_text(dir / "manifest.sig",
               archive::sign_description(load_key(setting("BACURI_EXPORT_KEY_FILE")), description));
    pack(stream->root, paths);
    stream->group_lease = [grants = std::move(grants), site, id, user = who.user, revision] {
        Database db;
        if (recordings::case_for_export(db, site, id, user).at("revision") != revision)
            throw Failure{403, "forbidden"};
        auto deadline = std::chrono::steady_clock::time_point::max();
        for (const auto& grant : grants)
            deadline = std::min(deadline, grant->authorize());
        return deadline;
    };
    stream->deadline = stream->authorize();
    const auto tar = stream->root / "export.tar";
    stream->input.open(tar, std::ios::binary);
    if (!stream->input)
        throw Failure{503, "media_unavailable"};
    for (const auto& item : file.at("materials"))
        events::record_export(
            auth, site, item.at("point_id"), who.user, "", "applied", reason,
            Json{{"case", id}, {"revision", revision}, {"mode", mode}, {"interval", item.at("interval")}}.dump());
    res.result(http::status::ok);
    res.set(http::field::content_type, "application/x-tar");
    res.set(http::field::content_disposition, "attachment; filename=\"bacuri-case-" + id + ".tar\"");
    res.media_size = fs::file_size(tar);
    fs::remove_all(stream->root);
    res.media = std::move(stream);
}
} // namespace bacuri::vms::control
