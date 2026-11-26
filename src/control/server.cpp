#include "control/events/video_rules.hpp"
#include "control/inventory/telemetry.hpp"
#include "control/inventory/registry.hpp"
#include "control/export.hpp"
#include "control/api.hpp"
#include "control/recordings/policies.hpp"
#include "control/recordings/search.hpp"
#include "control/access/admin.hpp"
#include "control/access/oidc.hpp"
#include "control/maps.hpp"
#include "control/alerts.hpp"
#include "control/alert_work.hpp"
#include "control/observations.hpp"
#include "control/inventory/camera_events.hpp"
#include <boost/beast/websocket/rfc6455.hpp>
#include "control/delivery.hpp"
#include "control/archive_catalog.hpp"
#include "control/media_authority.hpp"
#include "control/playback.hpp"
#include "control/routes.hpp"
#include <fstream>

#include <charconv>
#include <thread>
#include <utility>

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
bool workspace_page(std::string_view path)
{
    return path == "/access" || path == "/audit" || path == "/" || path == "/maps" || path == "/monitor" ||
           path == "/cameras" || path == "/investigations";
}

void workspace_file(const Request& req, Response& res, std::string_view path)
{
    allow(req, res, {http::verb::get});
    std::string file_name = "build/web-client/index.html";
    std::string_view mime = "text/html; charset=utf-8";
    if (!workspace_page(path)) {
        const auto name = path.substr(std::string_view{"/assets/"}.size());
        if (name.empty() ||
            name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_.") !=
                name.npos ||
            name.find("..") != name.npos) {
            throw Failure{404, "not_found"};
        }
        if (name.ends_with(".js")) {
            mime = "text/javascript; charset=utf-8";
        } else if (name.ends_with(".css")) {
            mime = "text/css";
        } else if (name.ends_with(".png")) {
            mime = "image/png";
        } else if (name.ends_with(".svg")) {
            mime = "image/svg+xml";
        } else if (name.ends_with(".webm")) {
            mime = "video/webm";
        } else if (name.ends_with(".woff2")) {
            mime = "font/woff2";
        } else {
            throw Failure{404, "not_found"};
        }
        file_name = "build/web-client/assets/" + std::string{name};
    }
    std::ifstream file{file_name, std::ios::binary};
    if (!file) {
        throw workspace_page(path) ? Failure{503, "dependency_unavailable"} : Failure{404, "not_found"};
    }
    res.result(http::status::ok);
    res.set(http::field::content_type, mime);
    res.set("Content-Security-Policy", "default-src 'self'; worker-src 'self' blob:; img-src 'self' data: blob:; "
                                       "media-src 'self' blob:; object-src 'none'; "
                                       "frame-ancestors 'none'; base-uri 'none'; form-action 'self'");
    res.body().assign(std::istreambuf_iterator<char>{file}, {});
}

// Scheduling class of a request. A misclassified request only runs on another pool.
Workload workload(const Request& req)
{
    std::string_view path{req.target().data(), req.target().size()};
    path = path.substr(0, path.find('?'));
    const bool read = req.method() == http::verb::get || req.method() == http::verb::head;
    // Password login and local account provisioning hash passwords.
    if (!read && (path == "/api/v1/session" || path.ends_with("/users")))
        return Workload::credential;
    // Camera commissioning and backfill, map transfers and exports wait on devices, remote sources or disks.
    if (path.contains("/exports") || (!read && (path.ends_with("/edge-archive") || path.contains("/onvif/") ||
                                                path.contains("/camera-batches/") || path.contains("/maps/"))))
        return Workload::long_running;
    return Workload::interactive;
}

// The alert snapshot, or the upgrade of the request to the alert feed WebSocket.
void alert_feed(const Request& req, Response& res, const Sessions& sessions, const Key& key,
                const std::string& site, bool stream, const std::string& origin)
{
    allow(req, res, {http::verb::get});
    auto feed = std::make_shared<AlertFeed>(sessions, key, req, site);
    if (!stream)
        return respond(res, feed->snapshot());
    if (req.count(http::field::origin) != 1 || req[http::field::origin] != origin) {
        throw Failure{403, "forbidden"};
    }
    if (!boost::beast::websocket::is_upgrade(req) || req.count(http::field::sec_websocket_protocol) != 1 ||
        req[http::field::sec_websocket_protocol] != alert_protocol || !req.body().empty()) {
        throw Failure{400, "invalid_request"};
    }
    feed->authorize();
    res.alerts = std::move(feed);
}

void route(const Request& req, Response& res, const std::string& peer, const Sessions& sessions,
           const CameraRegistry& registry, const Playback& playback, const Key& key, const std::string& origin)
{
    std::string_view path{req.target().data(), req.target().size()};
    path = path.substr(0, path.find('?'));
    if (workspace_page(path) || path.starts_with("/assets/")) {
        return workspace_file(req, res, path);
    }
    if (path == "/alerts/" || path == "/alerts/app.js" || path == "/archive/" || path == "/archive/player.js" ||
        path == "/archive/app.js" || path == "/archive/style.css" || path == "/live/" || path == "/live/live.js" ||
        path == "/session.js" || path == "/webauthn.js") {
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
    if (path.starts_with("/api/v1/media-sessions/")) {
        Routes media;
        media
            .add("api/v1/media-sessions/{id}",
                 [&](const PathMatch& m) { playback.session(req, res, m.values[0], {}); })
            .add("api/v1/media-sessions/{id}/*",
                 [&](const PathMatch& m) { playback.session(req, res, m.values[0], m.rest); });
        if (!media.dispatch(path.substr(1)))
            throw Failure{404, "not_found"};
        return;
    }
    constexpr std::string_view oidc = "/api/v1/session/oidc/";
    if (path.starts_with(oidc)) {
        res.set("Referrer-Policy", "no-referrer");
        try {
            return sessions.corporate(req, res, path.substr(oidc.size()));
        } catch (const Failure& e) {
            if (!std::string_view(req[http::field::accept]).contains("text/html") ||
                (e.code != "invalid_identity" && e.code != "identity_unavailable"))
                throw;
            res.result(static_cast<unsigned>(e.status));
            res.set(http::field::content_type, "text/html; charset=utf-8");
            res.set("Content-Security-Policy", "default-src 'none'; frame-ancestors 'none'; base-uri 'none'");
            res.body() = "<!doctype html><html lang=ru><meta charset=utf-8><meta name=viewport "
                         "content='width=device-width,initial-scale=1'>"
                         "<title>Вход | Bacuri VMS</title><main><h1>Корпоративный вход</h1><p>";
            res.body() += e.status == 503 ? "Провайдер недоступен."
                                          : "Не удалось подтвердить личность или требуемый уровень MFA.";
            res.body() +=
                "</p><p>" + e.code + "</p><a href='/monitor'>Вернуться к локальному входу</a></main></html>";
            return;
        }
    }
    if (path == "/api/v1/session/webauthn") {
        allow(req, res, {http::verb::post});
        return sessions.factor(req, res);
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
    if (access::administration(req, res, sessions, key, site, path))
        return;
    const std::string relative{path};
    // The paths below are relative to /api/v1/sites/{siteId}/. A handler receives the identifiers of its path.
    Routes routes;
    routes
        .add("investigation-assignees",
             [&](const PathMatch&) { recordings::case_recipients(req, res, sessions, site); })
        .add("investigations", [&](const PathMatch&) { recordings::investigation(req, res, sessions, site, {}); })
        .add("investigations/{id}",
             [&](const PathMatch& m) { recordings::investigation(req, res, sessions, site, m.values[0]); })
        .add("investigations/{id}/exports",
             [&](const PathMatch& m) { export_case(req, res, sessions, key, site, m.values[0]); })
        .add("archive-search", [&](const PathMatch&) { recordings::search_index(req, res, sessions, site); })
        .add("recording-policies", [&](const PathMatch&) { recordings::policies(req, res, sessions, site, {}); })
        .add("recording-policies/{id}",
             [&](const PathMatch& m) { recordings::policies(req, res, sessions, site, m.values[0]); })
        .add("storage/volumes", [&](const PathMatch&) { recordings::volumes(req, res, sessions, site); })
        .add("monitor-layouts", [&](const PathMatch&) { playback.layouts(req, res, site, {}); })
        .add("monitor-layouts/{id}", [&](const PathMatch& m) { playback.layouts(req, res, site, m.values[0]); })
        .add("maps", [&](const PathMatch&) { maps(req, res, sessions, site, relative, origin); })
        .add("maps/*", [&](const PathMatch&) { maps(req, res, sessions, site, relative, origin); })
        .add("alerts", [&](const PathMatch&) { alert_feed(req, res, sessions, key, site, false, origin); })
        .add("alerts/stream", [&](const PathMatch&) { alert_feed(req, res, sessions, key, site, true, origin); })
        .add("alerts/{id}/archive",
             [&](const PathMatch& m) { playback.alert_archive(req, res, site, m.values[0]); })
        .add("alerts/*", [&](const PathMatch&) { alert_work(req, res, sessions, key, site, relative); })
        .add("alert-audit/search", [&](const PathMatch&) { alert_work(req, res, sessions, key, site, relative); })
        .add("alert-delegations", [&](const PathMatch&) { alert_work(req, res, sessions, key, site, relative); })
        .add("alert-delegations/*", [&](const PathMatch&) { alert_work(req, res, sessions, key, site, relative); })
        .add("points",
             [&](const PathMatch&) {
                 allow(req, res, {http::verb::get});
                 registry.points(req, res, site);
             })
        .add("points/{id}/recording",
             [&](const PathMatch& m) { recordings::manual_recording(req, res, sessions, site, m.values[0]); })
        .add("points/{id}/video-rule",
             [&](const PathMatch& m) { events::video_rule(req, res, sessions, site, m.values[0], false); })
        .add("points/{id}/video-rule/simulation",
             [&](const PathMatch& m) { events::video_rule(req, res, sessions, site, m.values[0], true); })
        .add("points/{id}/holds",
             [&](const PathMatch& m) { recordings::holds(req, res, sessions, site, m.values[0]); })
        .add("points/{id}/exports",
             [&](const PathMatch& m) { export_fragment(req, res, sessions, key, site, m.values[0]); })
        .add("points/{id}/audio",
             [&](const PathMatch& m) {
                 allow(req, res, {http::verb::post});
                 registry.audio(req, res, site, m.values[0]);
             })
        .add("points/{id}/ptz",
             [&](const PathMatch& m) {
                 allow(req, res, {http::verb::get, http::verb::post});
                 registry.ptz(req, res, site, m.values[0]);
             })
        .add("points/{id}/archive", [&](const PathMatch& m) { playback.point(req, res, site, m.values[0], true); })
        .add("points/{id}/media-sessions",
             [&](const PathMatch& m) { playback.point(req, res, site, m.values[0], false); })
        .add("onvif/discovery",
             [&](const PathMatch&) {
                 allow(req, res, {http::verb::post});
                 registry.onvif(req, res, site, false);
             })
        .add("onvif/profiles",
             [&](const PathMatch&) {
                 allow(req, res, {http::verb::post});
                 registry.onvif(req, res, site, true);
             })
        .add("camera-templates",
             [&](const PathMatch&) {
                 allow(req, res, {http::verb::get});
                 registry.templates(req, res, site, {});
             })
        .add("camera-templates/{id}",
             [&](const PathMatch& m) {
                 allow(req, res, {http::verb::put});
                 registry.templates(req, res, site, m.values[0]);
             })
        .add("camera-batches",
             [&](const PathMatch&) {
                 allow(req, res, {http::verb::get});
                 registry.batch(req, res, site, {}, {}, {});
             })
        .add("camera-batches/{id}",
             [&](const PathMatch& m) {
                 allow(req, res, {http::verb::get});
                 registry.batch(req, res, site, m.values[0], {}, {});
             })
        .add("camera-batches/{id}/{id}",
             [&](const PathMatch& m) {
                 allow(req, res, {http::verb::put});
                 registry.batch(req, res, site, m.values[0], m.values[1], {});
             })
        .add("camera-batches/{id}/{id}/{name}",
             [&](const PathMatch& m) {
                 const auto& action = m.values[2];
                 if (action != "apply" && action != "rollback")
                     throw Failure{404, "not_found"};
                 allow(req, res, {http::verb::post});
                 registry.batch(req, res, site, m.values[0], m.values[1], action);
             })
        .add("cameras",
             [&](const PathMatch&) {
                 allow(req, res, {http::verb::get, http::verb::post});
                 registry.cameras(req, res, site, {});
             })
        .add("cameras/{id}",
             [&](const PathMatch& m) {
                 allow(req, res, {http::verb::get, http::verb::put});
                 registry.cameras(req, res, site, m.values[0]);
             })
        .add("cameras/{id}/edge-archive",
             [&](const PathMatch& m) {
                 allow(req, res, {http::verb::get, http::verb::post});
                 registry.edge_archive(req, res, site, m.values[0]);
             })
        .add("cameras/{id}/previous-recording",
             [&](const PathMatch& m) {
                 allow(req, res, {http::verb::get});
                 registry.previous_recording(req, res, site, m.values[0]);
             })
        .add("cameras/{id}/stream-issues", [&](const PathMatch& m) {
            allow(req, res, {http::verb::get});
            registry.issues(req, res, site, m.values[0]);
        });
    if (routes.dispatch(relative))
        return;
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
    std::jthread identities{[&](const std::stop_token& stop) { access::refresh_identities(stop, key); }};
    const Playback playback{sessions, key};
    auto networks = CameraNetworks::configured();
    auto policy = Json(networks.legacy_addresses()).dump();
    const CameraRegistry registry{sessions, key, networks};
    auto monitor_position = [&](const std::stop_token& stop) { inventory::track_positions(stop, networks, key); };
    std::jthread position_reader{monitor_position}, second_position_reader{monitor_position};
    std::jthread map_worker{map_packages::work};
    std::jthread reactions{[&](const std::stop_token& stop) {
        auto site = setting("BACURI_SITE_ID");
        events::dispatch_presets(stop, site, [&](const auto&... job) { registry.preset(site, job...); });
    }};
    std::jthread event_reader{[&](const std::stop_token& stop) { camera_events(stop, networks, key); }};
    std::jthread worker{
        [&key, policy = std::move(policy)](const std::stop_token& stop) { deliver(stop, key, policy); }};
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
        } catch (const std::exception& error) {
            // The client receives a generic problem. The cause stays in the service journal. The query
            // string is omitted because it can carry an authorization code.
            const std::string_view target{req.target().data(), req.target().size()};
            const std::string path{target.substr(0, target.find('?'))};
            std::fprintf(stderr, "control request %s %s failed: %s\n", id.get(), path.c_str(), error.what());
            problem(res, Failure{503, "authorization_unavailable"}, id.get());
        }
        return res;
    };
    listen_https({.address = setting("BACURI_LISTEN"),
                  .port = listen_port(),
                  .certificate_chain = setting("BACURI_TLS_CERT"),
                  .private_key = setting("BACURI_TLS_KEY")},
                 handle, workload);
}
} // namespace bacuri::vms::control
