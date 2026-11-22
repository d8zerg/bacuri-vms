#include "control/maps/packages.hpp"
#include <fstream>
#include <set>
#include <curl/curl.h>

namespace bacuri::vms::control::map_packages
{
namespace
{
void check(bool valid)
{
    if (!valid)
        throw Failure{400, "invalid_request"};
}
Json management(Database& db, const std::string& site)
{
    auto settings = db.exec("SELECT url FROM bacuri_maps.map_catalog_settings WHERE site_id=$1", {site});
    auto jobs = db.exec(
        "SELECT coalesce(jsonb_agg(to_jsonb(latest) ORDER BY created DESC),'[]') FROM ("
        "SELECT DISTINCT ON (j.region_id) j.id,j.region_id,j.name,j.mode,j.state,j.received,j.size_bytes AS bytes,"
        "j.error,j.created,j.source_id,s.kind='online' AS online FROM bacuri_maps.map_jobs j "
        "JOIN bacuri_maps.map_sources s ON s.id=j.source_id WHERE j.site_id=$1 "
        "ORDER BY j.region_id,j.created DESC,j.id) latest",
        {site});
    auto url = std::getenv("BACURI_MAP_CATALOG_URL");
    return {{"catalog_url", settings.size() ? settings.get(0)
                            : url           ? url
                                            : ""},
            {"jobs", Json::parse(jobs.get(0))}};
}
void validate_url(const std::string& url)
{
    std::unique_ptr<CURLU, decltype(&curl_url_cleanup)> parsed{curl_url(), curl_url_cleanup};
    check(url.starts_with("https://") && !url.contains('#') && !url.contains('?') && url.find('\0') == url.npos &&
          parsed && curl_url_set(parsed.get(), CURLUPART_URL, url.c_str(), CURLU_DISALLOW_USER) == CURLUE_OK);
}
void create_source(Database& db, const Request& req, Response& res, const std::string& site)
{
    auto body = object_body(req, {"kind", "url", "bytes"});
    auto kind = string_field(body, "kind", 12);
    std::string url;
    std::uint64_t size{};
    if (kind == "online") {
        url = string_field(body, "url", 2048);
        validate_url(url);
        db.exec("INSERT INTO bacuri_maps.map_catalog_settings(site_id,url) VALUES($1,$2) ON CONFLICT(site_id) DO "
                "UPDATE SET url=excluded.url",
                {site, url});
    } else {
        check(kind == "file" && body.contains("bytes") && body.at("bytes").is_number_unsigned());
        size = body.at("bytes");
        check(size > 0 && size <= max_archive);
    }
    auto id = db.exec("INSERT INTO bacuri_maps.map_sources(site_id,kind,url,state,size_bytes) "
                      "VALUES($1,$2,$3,$4,$5) RETURNING id",
                      {site, kind, url, kind == "online" ? "pending" : "uploading", std::to_string(size)})
                  .get(0);
    respond(res, {{"id", id}}, 201);
}
void append(Database& db, const Request& req, Response& res, const std::string& site, const std::string& id)
{
    auto rows = db.exec("SELECT received,size_bytes,state FROM bacuri_maps.map_sources WHERE site_id=$1 AND id=$2 "
                        "AND kind='file' FOR UPDATE",
                        {site, id});
    if (!rows.size())
        throw Failure{404, "not_found"};
    auto offset = std::stoull(rows.get(0)), total = std::stoull(rows.get(0, 1));
    auto header = req["X-Upload-Offset"];
    if (header != std::to_string(offset) || rows.get(0, 2) != "uploading")
        throw Failure{409, "conflict", {{"received", offset}}};
    check(!req.body().empty() && req.body().size() <= 1048576 && req.body().size() <= total - offset);
    const auto file = directory(id) / "upload";
    if (!offset) {
        std::ofstream create(file, std::ios::binary | std::ios::trunc);
        if (!create)
            throw std::runtime_error("map spool unavailable");
    } else if (fs::file_size(file) < offset)
        throw std::runtime_error("Incomplete map upload");
    fs::resize_file(file, offset);
    std::ofstream out(file, std::ios::binary | std::ios::app);
    out.exceptions(std::ios::badbit | std::ios::failbit);
    out.write(req.body().data(), static_cast<std::streamsize>(req.body().size()));
    out.close();
    offset += req.body().size();
    db.exec("UPDATE bacuri_maps.map_sources SET received=$2,state=$3,touched=now() WHERE id=$1",
            {id, std::to_string(offset), offset == total ? "pending" : "uploading"});
    res.result(http::status::no_content);
}
void queue(Database& db, const Request& req, Response& res, const std::string& site)
{
    auto b = object_body(req, {"source_id", "mode", "regions"});
    auto source = string_field(b, "source_id", 36), mode = string_field(b, "mode", 10);
    check(canonical_id(source) && (mode == "install" || mode == "update") && b.contains("regions") &&
          b["regions"].is_array() && !b["regions"].empty() && b["regions"].size() <= 1024);
    auto result = db.exec("UPDATE bacuri_maps.map_sources SET touched=now() WHERE site_id=$1 AND id=$2 AND "
                          "state='ready' RETURNING entries",
                          {site, source});
    if (!result.size())
        throw Failure{409, "source_unavailable"};
    auto entries = Json::parse(result.get(0));
    std::set<std::string> seen;
    for (const auto& id : b["regions"]) {
        check(id.is_string());
        if (!seen.insert(id.get<std::string>()).second)
            continue;
        auto entry = std::ranges::find_if(entries, [&](const Json& e) { return e.at("id") == id; });
        check(entry != entries.end());
        auto current = db.exec(
            "SELECT source_time<$3::timestamptz FROM bacuri_maps.map_regions WHERE site_id=$1 AND region_id=$2",
            {site, id, entry->at("source_time")});
        if (mode == "install" ? current.size() != 0 : !current.size() || current.get(0) != "t")
            continue;
        auto inserted =
            db.exec("INSERT INTO bacuri_maps.map_jobs(site_id,source_id,region_id,name,mode,state,size_bytes) "
                    "VALUES($1,$2,$3,$4,$5,'queued',$6) ON CONFLICT DO NOTHING RETURNING id",
                    {site, source, id, entry->at("name"), mode, entry->at("bytes").dump()});
        if (!inserted.size() && !db.exec("SELECT FROM bacuri_maps.map_jobs WHERE site_id=$1 AND region_id=$2 AND "
                                         "source_id=$3 AND mode=$4 AND state NOT IN ('installed','failed')",
                                         {site, id, source, mode})
                                     .size())
            throw Failure{409, "region_busy"};
    }
    res.result(http::status::no_content);
}
} // namespace
fs::path directory(const std::string& id)
{
    if (!canonical_id(id))
        throw std::invalid_argument("Invalid spool ID");
    auto root = std::getenv("BACURI_MAP_DIRECTORY");
    fs::path dir = (root ? fs::path(root) : fs::path("/var/lib/bacuri-core/maps")) / id;
    fs::create_directories(dir);
    return dir;
}
void manage(const Request& req, Response& res, Database& db, const std::string& site, std::string_view path)
{
    if (path == "maps/management") {
        allow(req, res, {http::verb::get});
        respond(res, management(db, site));
    } else if (path == "maps/sources") {
        allow(req, res, {http::verb::post});
        create_source(db, req, res, site);
    } else if (path == "maps/jobs") {
        allow(req, res, {http::verb::post});
        queue(db, req, res, site);
    } else if (path.starts_with("maps/jobs/")) {
        allow(req, res, {http::verb::put});
        auto part = path.substr(std::string_view{"maps/jobs/"}.size());
        if (part.size() != 45 || !part.ends_with("/download") || !canonical_id(part.substr(0, 36)))
            throw Failure{404, "not_found"};
        auto body = object_body(req, {"paused"});
        check(body.contains("paused") && body["paused"].is_boolean());
        const bool paused = body["paused"];
        auto changed = db.exec("UPDATE bacuri_maps.map_jobs j SET state=CASE WHEN $3::boolean THEN 'paused' WHEN "
                               "j.state='paused' THEN 'queued' ELSE j.state END, "
                               "error='',next_attempt=now() FROM bacuri_maps.map_sources s WHERE j.source_id=s.id "
                               "AND s.kind='online' AND j.site_id=$1 AND j.id=$2 "
                               "AND j.state IN ('queued','downloading','retrying','paused') RETURNING j.id",
                               {site, std::string(part.substr(0, 36)), paused ? "true" : "false"});
        if (!changed.size())
            throw Failure{409, "download_unavailable"};
        res.result(http::status::no_content);
    } else {
        auto part = path.substr(std::string_view{"maps/sources/"}.size());
        auto id = std::string(part.substr(0, 36));
        if (!canonical_id(id))
            throw Failure{404, "not_found"};
        if (part.substr(36) == "/chunks") {
            allow(req, res, {http::verb::put});
            append(db, req, res, site, id);
        } else {
            if (part.size() != 36)
                throw Failure{404, "not_found"};
            allow(req, res, {http::verb::get});
            auto r = db.exec(
                "SELECT state,received,error,entries FROM bacuri_maps.map_sources WHERE site_id=$1 AND id=$2",
                {site, id});
            if (!r.size())
                throw Failure{404, "not_found"};
            auto entries = Json::parse(r.get(0, 3));
            for (auto& e : entries) {
                e.erase("file");
                e.erase("sha256");
            }
            respond(res, {{"state", r.get(0)},
                          {"received", std::stoull(r.get(0, 1))},
                          {"error", r.get(0, 2)},
                          {"regions", entries}});
        }
    }
}
} // namespace bacuri::vms::control::map_packages
