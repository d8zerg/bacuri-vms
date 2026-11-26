#include "application/export/package.hpp"
#include "application/export/periods.hpp"
#include "modules/media_sessions/operations.hpp"
#include "modules/events/operations.hpp"
#include "modules/recordings/operations.hpp"
#include "modules/access/operations.hpp"
#include "modules/media_sessions/playback.hpp"
#include "application/control_api/playback_time.hpp"
#include "archive/evidence.hpp"
#include "media/clip.hpp"
#include "platform/scope_exit.hpp"

namespace bacuri::vms::control
{
namespace
{
void catalog_gaps(postgresql::Database& db, Json& periods, const std::string& site, const std::string& point,
                  const std::string& revision, std::int64_t a, std::int64_t b)
{
    if (a >= b)
        return;
    auto spans = recordings::coverage(db, site, point, std::to_string(a), std::to_string(b), "[]", revision);
    for (const auto& span : spans) {
        if (span.begin != a || span.state == "recorded")
            throw Failure{ErrorCode::media_unavailable};
        const auto end = span.end;
        add_gap(periods, a, end, span.reason);
        a = end;
    }
    if (a != b)
        throw Failure{ErrorCode::media_unavailable};
}
} // namespace
std::shared_ptr<ExportBody> prepare_fragment(const Request& req, Response& res, const Sessions& sessions,
                                             const Key& key, const std::string& site, const std::string& point,
                                             const std::shared_ptr<ExportSlot>& capacity, const fs::path& material)
{
    allow(req, res, {http::verb::post});
    postgresql::Database auth;
    const auto who = sessions.authenticate(auth, req, true);
    const auto browser = fingerprint(key, "session", who.cookie);
    const auto body = object_body(req, {"interval", "alert_id", "mode", "reason", "audio"});
    if (!body.contains("interval"))
        throw Failure{ErrorCode::invalid_request};
    const auto bounds = window(body.at("interval"));
    const auto mode = body.contains("mode") ? string_field(body, "mode", 16) : std::string{"selection"};
    if (mode != "selection" && mode != "original" && mode != "derivative")
        throw Failure{ErrorCode::invalid_request};
    if (body.contains("audio") && !body.at("audio").is_boolean())
        throw Failure{ErrorCode::invalid_request};
    const bool sound = body.value("audio", false), derivative = mode == "derivative";
    const auto reason = (mode != "selection" || sound || body.contains("reason"))
                            ? string_field(body, "reason", 512)
                            : std::string{};
    if ((mode != "selection" || sound) && reason.find_first_not_of(" \t\r\n") == reason.npos)
        throw Failure{ErrorCode::invalid_request};
    std::string alert;
    if (body.contains("alert_id")) {
        alert = string_field(body, "alert_id", 36);
        if (!canonical_id(alert))
            throw Failure{ErrorCode::invalid_request};
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
            throw Failure{ErrorCode::forbidden};
        std::string revision;
        if (!alert.empty()) {
            auto anchor = events::archive_anchor(db, site, alert);
            if (!anchor || anchor->point != point)
                throw Failure{ErrorCode::not_found};
            const auto allowed = alert_window(anchor->time);
            if (bounds.start < allowed.start || bounds.end > allowed.end)
                throw Failure{ErrorCode::forbidden};
            revision = anchor->revision;
        }
        recordings::require_available(db, site);
        const auto sources = recordings::manifests_for_export(db, site, point, start, end, revision);
        if (sources.empty())
            throw Failure{ErrorCode::media_unavailable};
        if (sources.size() > 256)
            throw Failure{ErrorCode::export_too_large};
        auto hashes = Json::object();
        std::int64_t available = archive::evidence_limit;
        std::optional<std::int64_t> previous_end;
        for (const auto& source : sources) {
            if (source.size_bytes > available)
                throw Failure{ErrorCode::export_too_large};
            available -= source.size_bytes;
            if (previous_end && *previous_end > source.begin_us())
                throw Failure{ErrorCode::media_unavailable};
            previous_end = source.end_us();
            hashes[source.recording.block().id.to_string()] = archive::digest_hex(source.sha256);
        }
        stream->hashes = hashes.dump();
        if (mode == "original") {
            const auto first = std::min(bounds.start, sources.front().begin_us());
            const auto last = std::max(bounds.end, sources.back().end_us());
            if (!alert.empty()) {
                auto anchor = events::archive_anchor(db, site, alert);
                if (!anchor || first < alert_window(anchor->time).start || last > alert_window(anchor->time).end)
                    throw Failure{ErrorCode::forbidden};
            }
            stream->begin = std::to_string(first);
            stream->end = std::to_string(last);
            stream->grant = access::export_permission(db, site, point, who.user, stream->begin, stream->end);
        }
        Json private_scope{{"export_grant", stream->grant}, {"begin", stream->begin},   {"end", stream->end},
                           {"regions", stream->regions},    {"derivative", derivative}, {"sound", sound}};
        const auto session =
            media_sessions::open_export(db, browser, site, point, std::to_string(sources.front().begin_us()),
                                        std::to_string(sources.back().end_us()), private_scope.dump());
        platform::ScopeExit close_session{[&db, &session] {
            // The session also expires if the database goes offline during preparation.
            try {
                media_sessions::close_export(db, session);
            } catch (const std::exception&) {
                std::fputs("Export session cleanup deferred until expiry\n", stderr);
            }
        }};
        const auto dir = stream->root / "data";
        fs::create_directory(dir);
        auto periods = Json::array();
        std::vector<std::string> clips;
        unsigned ordinal{};
        auto cursor = bounds.start;
        available = archive::evidence_limit;
        for (const auto& m : sources) {
            stream->authorize();
            auto a = m.begin_us(), b = m.end_us();
            auto from = std::max(a, bounds.start), until = std::min(b, bounds.end);
            catalog_gaps(db, periods, site, point, revision, cursor, from);
            cursor = until;
            const auto file = "clip-" + std::to_string(ordinal + 1) + ".mp4";
            const auto input = stream->root / "source.mp4";
            copy_export_block(session, m, browser, input);
            if (archive::digest_hex(m.sha256) != archive::file_digest(input).sha256)
                throw Failure{ErrorCode::media_service_unavailable};
            Json clip{{"source", archive::manifest_json(m)},
                      {"state", "recorded"},
                      {"file", file},
                      {"transform", "original"}};
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
                throw Failure{ErrorCode::export_too_large};
            available -= digest.bytes;
            clip.update(Json{{"sha256", digest.sha256},
                             {"size_bytes", digest.bytes},
                             {"begin_utc_us", from},
                             {"end_utc_us", until}});
            ++ordinal;
            if (mode != "selection" || sound) {
                clip["codec"] =
                    (clip.at("transform") == "original" || clip.at("transform") == "original_with_decode_context")
                        ? m.tracks.front().codec
                        : "h264";
                clip["masks"] = stream->regions.empty() ? Json::array() : Json::parse(stream->regions);
                clip["audio"] =
                    sound ? export_audio(m, from, until, dir, "audio-" + std::to_string(ordinal) + ".wav")
                          : Json(nullptr);
                if (!clip.at("audio").is_null()) {
                    const auto bytes = clip.at("audio").at("size_bytes").get<std::int64_t>();
                    if (bytes > available)
                        throw Failure{ErrorCode::export_too_large};
                    available -= bytes;
                    clips.push_back(clip.at("audio").at("file").get<std::string>());
                }
            }
            clips.push_back(file);
            periods.push_back(std::move(clip));
            add_gap(periods, until, cursor, "no_frame");
        }
        if (clips.empty())
            throw Failure{ErrorCode::media_unavailable};
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
            throw Failure{ErrorCode::export_too_large};
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
        events::record_export(auth, site, point, who.user, alert, "denied", std::string{error.code()},
                              details.dump());
        throw;
    } catch (const std::exception&) {
        events::record_export(auth, site, point, who.user, alert, "denied", "media_unavailable", details.dump());
        throw Failure{ErrorCode::media_service_unavailable};
    }
}
void export_fragment(const Request& req, Response& res, const Sessions& sessions, const Key& key,
                     const std::string& site, const std::string& point)
{
    prepare_fragment(req, res, sessions, key, site, point, std::make_shared<ExportSlot>());
}
} // namespace bacuri::vms::control
