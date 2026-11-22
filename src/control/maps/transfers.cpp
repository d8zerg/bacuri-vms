#include "control/maps/packages.hpp"

#include <archive.h>
#include <archive_entry.h>
#include <curl/curl.h>
#include <sodium.h>
#include <array>
#include <fstream>
#include <set>
#include <thread>

namespace bacuri::vms::control::map_packages
{
namespace
{
using namespace std::chrono_literals;
constexpr std::uint64_t chunk_size = 4ULL << 20;
struct PackageError : std::runtime_error {
    using std::runtime_error::runtime_error;
};
struct NetworkError : std::runtime_error {
    using std::runtime_error::runtime_error;
};
void require(bool ok)
{
    if (!ok)
        throw std::invalid_argument("invalid_package");
}

struct Download {
    Database& db;
    const std::string& job;
    std::ofstream& file;
    std::uint64_t offset;
    std::uint64_t total;
    std::stop_token stop;
    std::chrono::steady_clock::time_point next_poll{};
};
struct Reply {
    CURL* curl;
    std::size_t limit;
    Download* download;
    std::string request_range, content_range, data;
    std::size_t written{};
    bool invalid{}, disk_error{};

    bool accepts(long status) const
    {
        if (!download)
            return status == 200;
        if (status == 200)
            return download->offset == 0 && limit == download->total;
        return status == 206 && content_range == "bytes " + request_range + "/" + std::to_string(download->total);
    }
};
size_t body_callback(char* data, size_t size, size_t count, void* ctx)
{
    auto& reply = *static_cast<Reply*>(ctx);
    const auto n = size * count;
    long status{};
    curl_easy_getinfo(reply.curl, CURLINFO_RESPONSE_CODE, &status);
    if (status >= 400)
        return 0;
    try {
        if (!reply.accepts(status) || n > reply.limit - reply.written) {
            reply.invalid = true;
            return 0;
        }
        if (reply.download) {
            reply.download->file.write(data, static_cast<std::streamsize>(n));
            reply.download->file.flush();
        } else
            reply.data.append(data, n);
        reply.written += n;
        return n;
    } catch (...) {
        reply.disk_error = true;
        return 0;
    }
}
size_t header_callback(char* data, size_t size, size_t count, void* ctx)
{
    auto& reply = *static_cast<Reply*>(ctx);
    std::string_view line(data, size * count);
    if (line.starts_with("HTTP/"))
        reply.content_range.clear();
    constexpr std::string_view key = "Content-Range:";
    if (line.size() > key.size() && g_ascii_strncasecmp(line.data(), key.data(), key.size()) == 0) {
        line.remove_prefix(key.size());
        while (line.starts_with(' '))
            line.remove_prefix(1);
        while (line.ends_with('\r') || line.ends_with('\n') || line.ends_with(' '))
            line.remove_suffix(1);
        try {
            reply.content_range = line;
        } catch (...) {
            return 0;
        }
    }
    return size * count;
}
int download_progress(void* ctx, curl_off_t, curl_off_t, curl_off_t, curl_off_t)
{
    auto& reply = *static_cast<Reply*>(ctx);
    auto& download = *reply.download;
    if (download.stop.stop_requested())
        return 1;
    const auto now = std::chrono::steady_clock::now();
    if (now < download.next_poll)
        return 0;
    download.next_poll = now + 1s;
    try {
        return !download.db
                    .exec("UPDATE bacuri_maps.map_jobs SET received=$2 WHERE id=$1 AND state='downloading' "
                          "RETURNING id",
                          {download.job, std::to_string(download.offset + reply.written)})
                    .size();
    } catch (...) {
        return 1;
    }
}
std::string fetch(const std::string& url, std::size_t limit, Download* download = nullptr)
{
    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl{curl_easy_init(), curl_easy_cleanup};
    if (!curl)
        throw NetworkError("catalog_unavailable");
    Reply reply{curl.get(), limit, download, {}, {}, {}};
    curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_PROTOCOLS_STR, "https");
    curl_easy_setopt(curl.get(), CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_CONNECTTIMEOUT, 8L);
    curl_easy_setopt(curl.get(), CURLOPT_USERAGENT, "Bacuri-VMS-Maps/1");
    if (const auto* ca = std::getenv("BACURI_MAP_CA_FILE"))
        curl_easy_setopt(curl.get(), CURLOPT_CAINFO, ca);
    if (download) {
        reply.request_range =
            std::to_string(download->offset) + "-" + std::to_string(download->offset + limit - 1);
        curl_easy_setopt(curl.get(), CURLOPT_RANGE, reply.request_range.c_str());
        curl_easy_setopt(curl.get(), CURLOPT_LOW_SPEED_LIMIT, 1L);
        curl_easy_setopt(curl.get(), CURLOPT_LOW_SPEED_TIME, 30L);
        curl_easy_setopt(curl.get(), CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl.get(), CURLOPT_XFERINFOFUNCTION, download_progress);
        curl_easy_setopt(curl.get(), CURLOPT_XFERINFODATA, &reply);
    } else
        curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT, 45L);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, body_callback);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &reply);
    curl_easy_setopt(curl.get(), CURLOPT_HEADERFUNCTION, header_callback);
    curl_easy_setopt(curl.get(), CURLOPT_HEADERDATA, &reply);
    auto result = curl_easy_perform(curl.get());
    long status{};
    curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &status);
    if (reply.disk_error)
        throw PackageError("storage_unavailable");
    if (reply.invalid)
        throw PackageError(download ? "invalid_range" : "invalid_catalog");
    if (result == CURLE_ABORTED_BY_CALLBACK || status >= 500 || status == 429 || status == 408 ||
        (!status && result != CURLE_OK))
        throw NetworkError("network_unavailable");
    if (!reply.accepts(status))
        throw PackageError(download && (status == 200 || status == 206) ? "invalid_range" : "catalog_unavailable");
    if (result != CURLE_OK || (download && reply.written != limit))
        throw NetworkError("network_unavailable");
    return reply.data;
}
bool hex_digest(const std::string& text)
{
    return text.size() == 64 && text.find_first_not_of("0123456789abcdef") == std::string::npos;
}
Json catalog(const std::string& url)
{
    const auto doc = Json::parse(fetch(url, 2u << 20));
    const auto& entries = doc.at("regions");
    require(doc.at("version") == 1 && entries.is_array() && entries.size() <= 1024);
    Json list = Json::array();
    std::set<std::string> keys;
    for (const auto& entry : entries) {
        Json e;
        for (const auto* k : {"id", "name", "source_time", "sha256"}) {
            const auto& s = entry.at(k).get_ref<const std::string&>();
            require(!s.empty() && s.size() <= 256 && !s.contains('\0'));
            e[k] = s;
        }
        require(keys.insert(e.at("id")).second && hex_digest(e.at("sha256")));
        std::unique_ptr<GDateTime, decltype(&g_date_time_unref)> ts{
            g_date_time_new_from_iso8601(e.at("source_time").get_ref<const std::string&>().c_str(), nullptr),
            g_date_time_unref};
        require(ts != nullptr && entry.at("bytes").is_number_unsigned());
        auto size = entry.at("bytes").get<std::uint64_t>();
        require(size > 0 && size <= max_region);
        const auto& bounds = entry.at("bounds");
        require(bounds.is_array() && bounds.size() == 4);
        for (size_t i = 0; i < 4; i++)
            require(bounds[i].is_number() && std::abs(bounds[i].get<double>()) <= (i % 2 ? 85.051129 : 180.));
        require(bounds[0] < bounds[2] && bounds[1] < bounds[3]);
        e["bounds"] = bounds;
        e["bytes"] = size;
        list.push_back(std::move(e));
    }
    return list;
}
std::string digest(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    std::array<char, 65536> buf;
    crypto_hash_sha256_state state;
    crypto_hash_sha256_init(&state);
    while (in.read(buf.data(), buf.size()) || in.gcount())
        crypto_hash_sha256_update(&state, reinterpret_cast<const unsigned char*>(buf.data()), in.gcount());
    if (!in.eof())
        throw std::runtime_error("map_read_failed");
    std::array<unsigned char, crypto_hash_sha256_BYTES> hash;
    crypto_hash_sha256_final(&state, hash.data());
    char text[2 * crypto_hash_sha256_BYTES + 1];
    return sodium_bin2hex(text, sizeof text, hash.data(), hash.size());
}
Json unpack(const fs::path& dir)
{
    auto upload = dir / "upload";
    std::array<char, 16> magic{};
    std::ifstream(upload, std::ios::binary).read(magic.data(), magic.size());
    if (std::string_view(magic.data(), 15) == "SQLite format 3") {
        auto entry = inspect(upload);
        entry["file"] = "upload";
        return Json::array({entry});
    }
    std::unique_ptr<struct archive, decltype(&archive_read_free)> reader{archive_read_new(), archive_read_free};
    require(reader && archive_read_support_format_zip(reader.get()) == ARCHIVE_OK &&
            archive_read_open_filename(reader.get(), upload.c_str(), 65536) == ARCHIVE_OK);
    Json list = Json::array();
    std::set<std::string> ids;
    std::uint64_t total{};
    archive_entry* member{};
    int status{};
    while ((status = archive_read_next_header(reader.get(), &member)) == ARCHIVE_OK) {
        const char* name = archive_entry_pathname(member);
        if (!name || archive_entry_filetype(member) != AE_IFREG || fs::path(name).extension() != ".mbtiles")
            continue;
        const auto bytes = archive_entry_size(member);
        require(bytes > 0 && static_cast<std::uint64_t>(bytes) <= max_region && total + bytes <= max_archive &&
                list.size() < 1024);
        const auto filename = std::to_string(list.size()) + ".mbtiles";
        std::ofstream file(dir / filename, std::ios::binary | std::ios::trunc);
        file.exceptions(std::ios::badbit | std::ios::failbit);
        std::array<char, 65536> buf;
        std::int64_t written{};
        la_ssize_t count{};
        while ((count = archive_read_data(reader.get(), buf.data(), buf.size())) > 0) {
            written += count;
            require(written <= bytes);
            file.write(buf.data(), count);
        }
        require(count == 0 && written == bytes);
        file.close();
        total += written;
        auto entry = inspect(dir / filename);
        require(ids.insert(entry.at("id")).second);
        entry["file"] = filename;
        list.push_back(std::move(entry));
    }
    require(status == ARCHIVE_EOF && !list.empty());
    return list;
}
void scan(Database& db, const std::string& id, const std::string& kind, const std::string& url)
{
    Json entries;
    try {
        entries = kind == "online" ? catalog(url) : unpack(directory(id));
    } catch (const std::exception&) {
        db.exec("UPDATE bacuri_maps.map_sources SET state='failed',error=$2 WHERE id=$1",
                {id, kind == "online" ? "catalog_unavailable" : "invalid_package"});
        if (kind == "file")
            fs::remove_all(directory(id));
        return;
    }
    db.exec("UPDATE bacuri_maps.map_sources SET entries=$2,state='ready',touched=now() WHERE id=$1",
            {id, entries.dump()});
    if (kind == "file" && entries.front().at("file") != "upload")
        fs::remove(directory(id) / "upload");
}
void advance(Database& db, const Rows& row, std::stop_token stop)
{
    auto id = row.get(0), site = row.get(0, 1), source = row.get(0, 2), mode = row.get(0, 3), url = row.get(0, 4);
    auto e = Json::parse(row.get(0, 5));
    const bool remote = !url.empty();
    auto dir = directory(source);
    const auto file = dir / (remote ? id + ".part" : e.at("file").get<std::string>());
    const auto total = e.at("bytes").get<std::uint64_t>();
    bool transaction = false;
    try {
        if (remote) {
            auto offset = fs::exists(file) ? fs::file_size(file) : 0;
            require(offset <= total);
            if (offset < total) {
                if (!db.exec("UPDATE bacuri_maps.map_jobs SET state='downloading',received=$2,error='' WHERE "
                             "id=$1 AND state<>'paused' RETURNING id",
                             {id, std::to_string(offset)})
                         .size())
                    return;
                const auto n = std::min(total - offset, chunk_size);
                std::ofstream out(file, std::ios::app | std::ios::binary);
                out.exceptions(std::ios::badbit | std::ios::failbit);
                Download download{db, id, out, offset, total, stop};
                const auto filename = e.at("sha256").get<std::string>() + ".mbtiles";
                std::unique_ptr<char, decltype(&g_free)> address{
                    g_uri_resolve_relative(url.c_str(), filename.c_str(), G_URI_FLAGS_NONE, nullptr), g_free};
                if (!address)
                    throw PackageError("catalog_unavailable");
                fetch(address.get(), n, &download);
                out.close();
                db.exec("UPDATE bacuri_maps.map_jobs SET received=$2,next_attempt=now() WHERE id=$1",
                        {id, std::to_string(offset + n)});
                if (offset + n < total)
                    return;
            }
            if (digest(file) != e.at("sha256").get<std::string>())
                throw PackageError("checksum_mismatch");
        }
        if (!db.exec("UPDATE bacuri_maps.map_jobs SET state='installing',received=size_bytes,error='' WHERE id=$1 "
                     "AND state<>'paused' RETURNING id",
                     {id})
                 .size())
            return;
        db.exec("BEGIN");
        transaction = true;
        install(db, file, site, e, mode);
        db.exec("UPDATE bacuri_maps.map_jobs SET state='installed' WHERE id=$1", {id});
        db.exec("COMMIT");
    } catch (const NetworkError&) {
        db.exec("UPDATE bacuri_maps.map_jobs SET received=$2, state=CASE WHEN state='downloading' THEN 'retrying' "
                "ELSE state END, "
                "error=CASE WHEN state='downloading' THEN 'network_unavailable' ELSE '' END, "
                "next_attempt=CASE WHEN state='downloading' THEN now()+interval '5 seconds' ELSE next_attempt END "
                "WHERE id=$1",
                {id, std::to_string(fs::exists(file) ? fs::file_size(file) : 0)});
        return;
    } catch (const PackageError& error) {
        db.exec("UPDATE bacuri_maps.map_jobs SET state='failed',error=$2 WHERE id=$1", {id, error.what()});
    } catch (const std::exception&) {
        if (transaction)
            db.exec("ROLLBACK");
        db.exec("UPDATE bacuri_maps.map_jobs SET state='failed',error='installation_failed' WHERE id=$1", {id});
    }
    if (remote)
        fs::remove(file);
}
} // namespace
void work(std::stop_token stop)
{
    auto site = setting("BACURI_SITE_ID");
    while (!stop.stop_requested()) {
        try {
            Database db;
            if (db.exec("SELECT pg_try_advisory_lock(hashtextextended($1,170017))", {site}).get(0) == "t") {
                while (!stop.stop_requested()) {
                    auto pending = db.exec("SELECT id,kind,url FROM bacuri_maps.map_sources WHERE site_id=$1 AND "
                                           "state='pending' ORDER BY touched LIMIT 1",
                                           {site});
                    if (pending.size()) {
                        scan(db, pending.get(0), pending.get(0, 1), pending.get(0, 2));
                        continue;
                    }
                    auto job = db.exec(
                        "SELECT j.id,j.site_id,j.source_id,j.mode,s.url,e FROM "
                        "bacuri_maps.map_jobs j JOIN bacuri_maps.map_sources s ON s.id=j.source_id "
                        "CROSS JOIN LATERAL jsonb_array_elements(s.entries) e WHERE j.site_id=$1 "
                        "AND e->>'id'=j.region_id AND j.state IN ('queued','downloading','retrying','installing') "
                        "AND j.next_attempt<=now() ORDER BY j.next_attempt,j.created LIMIT 1",
                        {site});
                    if (job.size()) {
                        advance(db, job, stop);
                        continue;
                    }
                    auto expired = db.exec("DELETE FROM bacuri_maps.map_sources s WHERE s.site_id=$1 AND "
                                           "s.touched<now()-interval '7 days' AND NOT EXISTS "
                                           "(SELECT FROM bacuri_maps.map_jobs j WHERE j.source_id=s.id AND "
                                           "j.state NOT IN ('installed','failed')) RETURNING id",
                                           {site});
                    for (const auto& id : expired.column(0))
                        fs::remove_all(directory(id));
                    std::this_thread::sleep_for(500ms);
                }
            }
        } catch (const std::exception&) {
            std::fputs("Map package worker waiting for storage\n", stderr);
        }
        std::this_thread::sleep_for(500ms);
    }
}
} // namespace bacuri::vms::control::map_packages
