#include "application/export/package.hpp"
#include "application/control_api/playback_time.hpp"
#include "modules/investigations/cases.hpp"
#include "modules/events/operations.hpp"
#include "archive/evidence.hpp"

namespace bacuri::vms::control
{
void export_case(const Request& req, Response& res, const Sessions& sessions, const Key& key,
                 const std::string& site, const std::string& id)
{
    allow(req, res, {http::verb::post});
    postgresql::Database auth;
    auto who = sessions.authenticate(auth, req, true);
    auto options = object_body(req, {"mode", "reason", "audio", "revision"});
    const auto reason = string_field(options, "reason", 512), mode = string_field(options, "mode", 16),
               revision = string_field(options, "revision", 19);
    if (mode != "original" && mode != "derivative")
        throw Failure{ErrorCode::invalid_request};
    auto file = investigations::case_for_export(auth, site, id, who.user);
    if (file.at("revision") != revision)
        throw Failure{ErrorCode::revision_conflict};
    auto capacity = std::make_shared<ExportSlot>();
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
                throw Failure{ErrorCode::export_too_large};
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
        postgresql::Database db;
        if (investigations::case_for_export(db, site, id, user).at("revision") != revision)
            throw Failure{ErrorCode::forbidden};
        auto deadline = std::chrono::steady_clock::time_point::max();
        for (const auto& grant : grants)
            deadline = std::min(deadline, grant->authorize());
        return deadline;
    };
    stream->deadline = stream->authorize();
    const auto tar = stream->root / "export.tar";
    stream->input.open(tar, std::ios::binary);
    if (!stream->input)
        throw Failure{ErrorCode::media_service_unavailable};
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
