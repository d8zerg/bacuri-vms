#include "control/api.hpp"
#include "control/alerts.hpp"
#include "control/observations.hpp"
#include <boost/beast/websocket/rfc6455.hpp>
#include "control/delivery.hpp"
#include "control/archive_catalog.hpp"
#include "control/media_authority.hpp"
#include "control/playback.hpp"
#include <fstream>

#include <charconv>
#include <thread>

namespace bacuri::vms::control
{
void allow(const Request& req, Response& res, std::initializer_list<http::verb> methods)
{
    if (std::ranges::find(methods, req.method()) != methods.end()) {
        return;
    }
    std::string permitted;
    for (const auto method : methods) {
        if (!permitted.empty()) {
            permitted += ", ";
        }
        permitted += std::string{http::to_string(method)};
    }
    res.set(http::field::allow, permitted);
    throw Failure{405, "invalid_request"};
}

namespace
{
void route(const Request& req, Response& res, const std::string& peer, const Sessions& sessions,
           const CameraRegistry& registry, const Playback& playback, const Key& key, const std::string& origin)
{
    std::string_view path{req.target().data(), req.target().size()};
    path = path.substr(0, path.find('?'));
    if (path == "/alerts/" || path == "/alerts/app.js" || path == "/archive/" || path == "/archive/player.js" ||
        path == "/archive/app.js" || path == "/archive/style.css" || path == "/live/" || path == "/live/live.js" ||
        path == "/session.js") {
        allow(req, res, {http::verb::get});
        const bool html = path.ends_with('/');
        std::ifstream file{"web" + std::string{path} + (html ? "index.html" : "")};
        if (!file) {
            throw Failure{503, "dependency_unavailable"};
        }
        res.result(http::status::ok);
        res.set(http::field::content_type, html                     ? "text/html; charset=utf-8"
                                           : path.ends_with(".css") ? "text/css"
                                                                    : "text/javascript; charset=utf-8");
        res.set("Content-Security-Policy", "default-src 'self'; media-src 'self' blob:; object-src 'none'; "
                                           "frame-ancestors 'none'; base-uri 'none'");
        res.body().assign(std::istreambuf_iterator<char>{file}, {});
        return;
    }
    constexpr std::string_view media_prefix = "/api/v1/media-sessions/";
    if (path.starts_with(media_prefix)) {
        path.remove_prefix(media_prefix.size());
        auto slash = path.find('/');
        const std::string id{path.substr(0, slash)};
        if (!canonical_id(id)) {
            throw Failure{404, "not_found"};
        }
        return playback.session(req, res, id, slash == path.npos ? std::string_view{} : path.substr(slash + 1));
    }
    if (path == "/api/v1/session") {
        return sessions.handle(req, res, peer);
    }
    constexpr std::string_view prefix = "/api/v1/sites/";
    if (!path.starts_with(prefix)) {
        throw Failure{404, "not_found"};
    }
    path.remove_prefix(prefix.size());
    const auto separator = path.find('/');
    if (separator == path.npos || !canonical_id(path.substr(0, separator))) {
        throw Failure{404, "not_found"};
    }
    const std::string site{path.substr(0, separator)};
    path.remove_prefix(separator + 1);
    if (path == "alerts" || path == "alerts/stream") {
        allow(req, res, {http::verb::get});
        auto feed = std::make_shared<AlertFeed>(sessions, key, req, site);
        if (path == "alerts") {
            return respond(res, feed->snapshot());
        }
        if (req.count(http::field::origin) != 1 || req[http::field::origin] != origin) {
            throw Failure{403, "forbidden"};
        }
        if (!boost::beast::websocket::is_upgrade(req) || req.count(http::field::sec_websocket_protocol) != 1 ||
            req[http::field::sec_websocket_protocol] != alert_protocol || !req.body().empty()) {
            throw Failure{400, "invalid_request"};
        }
        feed->authorize();
        res.alerts = std::move(feed);
        return;
    }
    if (path.size() == 51 && path.starts_with("alerts/") && path.ends_with("/archive")) {
        path.remove_prefix(7);
        path.remove_suffix(8);
        if (!canonical_id(path)) {
            throw Failure{404, "not_found"};
        }
        return playback.alert_archive(req, res, site, std::string{path});
    }
    if (path.starts_with("points/")) {
        path.remove_prefix(7);
        const auto slash = path.find('/');
        const std::string point{path.substr(0, slash)};
        if (slash != path.npos && canonical_id(point)) {
            const auto operation = path.substr(slash + 1);
            if (operation == "archive" || operation == "media-sessions") {
                return playback.point(req, res, site, point, operation == "archive");
            }
        }
        throw Failure{404, "not_found"};
    }
    if (path == "points") {
        allow(req, res, {http::verb::get});
        return registry.points(req, res, site);
    }
    if (path == "cameras") {
        allow(req, res, {http::verb::get, http::verb::post});
        return registry.cameras(req, res, site, {});
    }
    constexpr std::string_view camera_prefix = "cameras/";
    if (path.starts_with(camera_prefix) && canonical_id(path.substr(camera_prefix.size()))) {
        allow(req, res, {http::verb::get, http::verb::put});
        return registry.cameras(req, res, site, std::string{path.substr(camera_prefix.size())});
    }
    throw Failure{404, "not_found"};
}

void problem(Response& res, const Failure& error, const char* id)
{
    if (error.status == 429 && !res.count(http::field::retry_after)) {
        res.set(http::field::retry_after, "1");
    }
    Json body{{"status", error.status},
              {"code", error.code},
              {"title", error.code},
              {"type", "urn:bacuri:vms:problem:" + error.code},
              {"request_id", id}};
    body.update(error.fields);
    respond(res, body, error.status);
    res.set(http::field::content_type, "application/problem+json");
}

unsigned short listen_port()
{
    auto value = setting("BACURI_PORT");
    unsigned short port{};
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), port);
    if (error != std::errc{} || end != value.data() + value.size() || port == 0) {
        throw std::runtime_error("Invalid HTTPS port");
    }
    return port;
}
} // namespace

void serve()
{
    const auto key = load_key(setting("BACURI_KEY_FILE"));
    const auto origin = setting("BACURI_ORIGIN");
    if (!origin.starts_with("https://")) {
        throw std::runtime_error("Expected an HTTPS origin");
    }
    for (const auto* field : {"BACURI_SITE_ID", "BACURI_NODE_ID"}) {
        if (!canonical_id(setting(field))) {
            throw std::runtime_error("Invalid assignment ID");
        }
    }
    setting("BACURI_SOCKET");
    ArchiveCatalog catalog{{setting("BACURI_SITE_ID"), setting("BACURI_NODE_ID")}};
    MediaAuthority authority;
    Observations observations;
    const Sessions sessions{key, origin};
    const Playback playback{sessions, key};
    const CameraRegistry registry{sessions, key, CameraNetworks{setting("BACURI_CAMERA_NETWORKS")}};
    std::jthread worker{[&key](std::stop_token stop) { deliver(stop, key); }};
    const auto handle = [&](const Request& req, const std::string& peer) {
        Response res;
        std::unique_ptr<char, decltype(&g_free)> id{g_uuid_string_random(), g_free};
        res.set(http::field::cache_control, "no-store");
        res.set("X-Content-Type-Options", "nosniff");
        res.set("X-Request-Id", id.get());
        try {
            if (req.version() == 11 && (req.count(http::field::host) != 1 || req[http::field::host].empty())) {
                throw Failure{400, "invalid_request"};
            }
            route(req, res, peer, sessions, registry, playback, key, origin);
        } catch (const Failure& failure) {
            problem(res, failure, id.get());
        } catch (const std::exception&) {
            std::fputs("Control request dependency unavailable\n", stderr);
            problem(res, Failure{503, "authorization_unavailable"}, id.get());
        }
        return res;
    };
    listen_https({.address = setting("BACURI_LISTEN"),
                  .port = listen_port(),
                  .certificate_chain = setting("BACURI_TLS_CERT"),
                  .private_key = setting("BACURI_TLS_KEY")},
                 handle);
}
} // namespace bacuri::vms::control
