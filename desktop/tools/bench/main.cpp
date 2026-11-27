// Video and map bench of Bacuri VMS Client. Plays one source in a grid of cells on a common timeline
// through the selected decoder into Qt Quick on OpenGL, seeks once and writes a JSON report with the
// decoder output memory, the frames that reached the display, the first frame after the
// start and after the seek, the frame time of the interface and the CPU time.

#include "diagnostics/frame_stats.hpp"
#include "map/map_view.hpp"
#include "media/graphics_probe.hpp"
#include "media/stream_group.hpp"
#include "media/video_stream.hpp"
#include "site/help.hpp"
#include "site/sign_in.hpp"
#include <QLocale>
#include "site/site_proxy.hpp"

#include <QCommandLineParser>
#include <QtQml/qqmlextensionplugin.h>
#include <QEventLoop>
#include <QJsonObject>
#include <QNetworkReply>
#include <QFile>
#include <QFileInfo>
#include <QUrl>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QQmlApplicationEngine>
#include <QShortcut>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTimer>

#include <gst/gst.h>
#include <sys/resource.h>

#include <chrono>
#include <cstdio>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

Q_IMPORT_QML_PLUGIN(Bacuri_Vms_Client_MapPlugin)

namespace
{
using namespace bacuri::vms::client;
using Clock = std::chrono::steady_clock;

QQuickItem* find_item(QQuickItem* root, const QString& name)
{
    if (root->objectName() == name)
        return root;
    for (auto* child : root->childItems()) {
        if (auto* found = find_item(child, name))
            return found;
    }
    return nullptr;
}

double cpu_seconds()
{
    rusage usage{};
    getrusage(RUSAGE_SELF, &usage);
    const auto seconds = [](const timeval& value) {
        return static_cast<double>(value.tv_sec) + static_cast<double>(value.tv_usec) / 1e6;
    };
    return seconds(usage.ru_utime) + seconds(usage.ru_stime);
}

QJsonObject to_json(const Distribution& value);

// Render count and render time of the map, or null without a map.
QJsonValue map_report(QQuickWindow* window)
{
    auto* map = find_item(window->contentItem(), QStringLiteral("map"));
    if (map == nullptr)
        return QJsonValue::Null;
    QVariantMap statistics;
    QMetaObject::invokeMethod(map, "statistics", Q_RETURN_ARG(QVariantMap, statistics));
    std::vector<double> renders;
    for (const auto& value : statistics.value("render_ms").toList())
        renders.push_back(value.toDouble());
    return QJsonObject{{"renders", static_cast<qint64>(renders.size())},
                       {"render_ms", to_json(distribution(renders))}};
}

QJsonObject to_json(const Distribution& value)
{
    return {{"count", static_cast<qint64>(value.count)},
            {"p50", value.p50},
            {"p95", value.p95},
            {"p99", value.p99},
            {"max", value.max}};
}

struct SiteMap {
    std::unique_ptr<SiteProxy> proxy;
    QString style;
    QJsonObject report;
};

// Signs in through the browser and reads the installed region of the first site of the user.
SiteMap open_site_map(const SignInOptions& options)
{
    SiteMap result;
    QEventLoop loop;
    SignIn sign_in{options};
    DesktopSession session;
    QObject::connect(&sign_in, &SignIn::signed_in, &loop, [&](const DesktopSession& value) {
        session = value;
        loop.quit();
    });
    QObject::connect(&sign_in, &SignIn::failed, &loop, [&](const QString& cause) {
        result.report.insert("error", cause);
        loop.quit();
    });
    sign_in.start();
    loop.exec();
    if (session.token.isEmpty())
        return result;
    const auto site = session.site_roles.first().toObject().value("site_id").toString();
    QNetworkAccessManager network;
    QNetworkRequest request{options.site.resolved(QUrl{QStringLiteral("/api/v1/sites/%1/maps").arg(site)})};
    request.setRawHeader("Authorization", "Bearer " + session.token.toLatin1());
    request.setSslConfiguration(site_tls(options.ca_file));
    auto* reply = network.get(request);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    reply->deleteLater();
    const auto region = QJsonDocument::fromJson(reply->readAll()).object().value("region").toObject();
    if (region.isEmpty()) {
        result.report.insert("error", "map_region_missing");
        return result;
    }
    result.report = QJsonObject{{"id", region.value("id")},
                                {"revision", region.value("revision")},
                                {"zoom", QJsonArray{region.value("min_zoom"), region.value("max_zoom")}},
                                {"attribution", region.value("attribution")}};
    result.proxy = std::make_unique<SiteProxy>(options.site, session.token, options.ca_file);
    if (!result.proxy->start()) {
        result.report.insert("error", "gateway_unavailable");
        result.proxy.reset();
        return result;
    }
    const auto style =
        options.site.resolved(QUrl{region.value("style_url").toString()}).toString(QUrl::FullyEncoded);
    result.style = QString::fromStdString(result.proxy->local(style.toStdString()));
    return result;
}

// Frame times of the scene graph, recorded on the render thread.
struct FrameTimes {
    std::mutex mutex;
    std::optional<Clock::time_point> previous_swap;
    std::optional<Clock::time_point> render_start;
    std::optional<Clock::time_point> measure_from;
    Clock::time_point started;
    std::vector<double> intervals;
    // The longest frame interval and its end in seconds since the start of the run.
    double slowest_ms{};
    double slowest_at_s{};
    std::vector<double> render;
    QString renderer;

    void before_rendering()
    {
        std::scoped_lock lock{mutex};
        render_start = Clock::now();
        if (renderer.isEmpty()) {
            if (auto* context = QOpenGLContext::currentContext()) {
                const auto* name = context->functions()->glGetString(GL_RENDERER);
                const auto* version = context->functions()->glGetString(GL_VERSION);
                renderer = QStringLiteral("%1, %2").arg(reinterpret_cast<const char*>(name),
                                                        reinterpret_cast<const char*>(version));
            }
        }
    }

    void swapped()
    {
        std::scoped_lock lock{mutex};
        const auto now = Clock::now();
        const bool measuring = measure_from && now >= *measure_from;
        if (measuring && previous_swap) {
            intervals.push_back(std::chrono::duration<double, std::milli>(now - *previous_swap).count());
            if (intervals.back() > slowest_ms) {
                slowest_ms = intervals.back();
                slowest_at_s = std::chrono::duration<double>(now - started).count();
            }
        }
        if (measuring && render_start)
            render.push_back(std::chrono::duration<double, std::milli>(now - *render_start).count());
        previous_swap = now;
    }
};
} // namespace

int main(int argc, char* argv[])
{
    gst_init(&argc, &argv);
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
    QGuiApplication application(argc, argv);

    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOptions({
        {"language", "Workstation interface language (en or ru).", "language",
         QLocale().language() == QLocale::Russian ? "ru" : "en"},
        {"theme", "Workstation theme (system, light or dark).", "theme", "system"},
    });
    parser.addOptions({
        {"source", "MP4 file or rtsp:// address.", "source"},
        {"codec", "h264 or h265.", "codec", "h264"},
        {"decoder", "auto, hardware or software.", "decoder", "auto"},
        {"fault-decoder-at", "Seconds after the start to report a hardware decoder failure, -1 to skip.",
         "seconds", "-1"},
        {"cells", "Number of cells showing the source.", "cells", "1"},
        {"seconds", "Duration of the run.", "seconds", "20"},
        {"warmup", "Seconds before the frame time is measured.", "warmup", "3"},
        {"seek-frame", "Frame to seek to in the middle of the run, -1 to skip.", "frame", "-1"},
        {"site", "Site origin: sign in through the browser and show the installed map of the site.", "url"},
        {"ca", "Certificates trusted for the site.", "file"},
        {"browser", "Command that opens the sign-in page instead of the default browser.", "command"},
        {"report", "Path of the JSON report.", "report"},
        {"screenshot", "Path of a PNG capture of the window taken before the end.", "path"},
    });
    parser.process(application);
    const auto cell_count = parser.value("cells").toInt();
    const auto seconds = parser.value("seconds").toInt();
    const auto seek_frame = parser.value("seek-frame").toLongLong();
    if (parser.value("source").isEmpty() || cell_count < 1 || seconds < 2) {
        std::fputs("Expected --source, --cells of at least 1 and --seconds of at least 2\n", stderr);
        return 2;
    }

    // A station whose OpenGL renderer is a software rasterizer has no usable GPU: the cells decode in
    // software with a limited picture and frame rate, so the interface stays responsive.
    const auto graphics = probe_graphics();
    auto choice = parser.value("decoder") == "hardware"   ? DecoderChoice::hardware
                  : parser.value("decoder") == "software" ? DecoderChoice::software
                                                          : DecoderChoice::automatic;
    StreamOptions stream_options{parser.value("source"), parser.value("codec"), choice};
    if (graphics.software && choice == DecoderChoice::automatic) {
        stream_options.decoder = DecoderChoice::software;
        stream_options.max_size = QSize{640, 360};
        stream_options.max_rate = 15;
    }
    std::vector<std::unique_ptr<VideoStream>> cells;
    for (int index = 0; index < cell_count; ++index) {
        cells.push_back(std::make_unique<VideoStream>(index, stream_options));
        cells.back()->create();
    }

    // The map comes from the site: the client signs in, reads the map scene and shows the
    // installed region through the local gateway that adds the session token.
    QString map_style;
    std::unique_ptr<SiteProxy> proxy;
    QJsonObject site_map;
    if (parser.isSet("site")) {
        const SignInOptions options{QUrl{parser.value("site")}, parser.value("ca"), parser.value("browser"), 120};
        auto opened = open_site_map(options);
        site_map = opened.report;
        if (!opened.proxy) {
            std::fprintf(stderr, "The map of the site is unavailable: %s\n",
                         qPrintable(site_map.value("error").toString()));
            return 1;
        }
        proxy = std::move(opened.proxy);
        map_style = opened.style;
        MapView::set_resource_transform(
            [gateway = proxy.get()](const std::string& url) { return gateway->local(url); });
    }

    QQmlApplicationEngine engine;
    engine.setInitialProperties({{"cellCount", cell_count}, {"mapStyle", map_style}});
    engine.loadFromModule("Bacuri.Vms.Client.Bench", "Bench");
    if (engine.rootObjects().isEmpty())
        return 1;
    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().constFirst());

    if (parser.isSet("site")) {
        auto* help = new QShortcut(QKeySequence::HelpContents, window);
        QObject::connect(help, &QShortcut::activated, window, [&parser] {
            if (!open_help(QUrl{parser.value("site")}, parser.value("language"), parser.value("theme"),
                           HelpPage::site_map))
                qWarning("Could not open the site help portal");
        });
    }

    FrameTimes times;
    const auto started = Clock::now();
    times.started = started;
    times.measure_from = started + std::chrono::seconds{parser.value("warmup").toInt()};
    QObject::connect(
        window, &QQuickWindow::beforeRendering, window, [&times] { times.before_rendering(); },
        Qt::DirectConnection);
    QObject::connect(
        window, &QQuickWindow::frameSwapped, window, [&times] { times.swapped(); }, Qt::DirectConnection);

    StreamGroup group{window, 1'000'000'000LL * stream_options.rate_denominator / stream_options.rate_numerator};
    for (int index = 0; index < cell_count; ++index) {
        auto& cell = group.add(std::move(cells[static_cast<std::size_t>(index)]));
        cell.attach(find_item(window->contentItem(), QStringLiteral("video%1").arg(index)), window);
    }
    cells.clear();
    group.start();

    const auto cpu_start = cpu_seconds();
    if (seek_frame >= 0) {
        QTimer::singleShot(std::chrono::milliseconds{seconds * 500}, window,
                           [&group, seek_frame] { group.seek_to_frame(seek_frame); });
    }
    const auto fault_at = parser.value("fault-decoder-at").toInt();
    if (fault_at >= 0) {
        QTimer::singleShot(std::chrono::seconds{fault_at}, window, [&group] {
            for (auto& cell : group.streams())
                cell->inject_decoder_fault();
        });
    }
    if (parser.isSet("screenshot")) {
        QTimer::singleShot(std::chrono::milliseconds{seconds * 1000 - 1500}, window,
                           [window, path = parser.value("screenshot")] { window->grabWindow().save(path); });
    }
    int status = 0;
    QTimer::singleShot(std::chrono::seconds{seconds}, window, [&] {
        const auto wall = std::chrono::duration<double>(Clock::now() - started).count();
        QJsonArray reports;
        for (const auto& cell : group.streams())
            reports.append(cell->report());
        QJsonObject report;
        {
            std::scoped_lock lock{times.mutex};
            report = QJsonObject{
                {"source", parser.value("source").startsWith("rtsp://") ? QStringLiteral("rtsp")
                                                                        : parser.value("source").section('/', -1)},
                {"codec", parser.value("codec")},
                {"decoder", parser.value("decoder")},
                {"cells", cell_count},
                {"seconds", wall},
                {"graphics", times.renderer},
                {"graphics_mode", graphics.software ? QStringLiteral("software") : QStringLiteral("hardware")},
                {"software_limits", stream_options.max_rate > 0},
                {"grid", group.report()},
                {"platform", QGuiApplication::platformName()},
                // Without the DRI devices of the host the NVIDIA driver renders but cannot present to the screen.
                {"presentation_devices", QFileInfo::exists(QStringLiteral("/dev/dri"))},
                {"ui_frame_interval_ms", to_json(distribution(times.intervals))},
                {"slowest_frame", QJsonObject{{"interval_ms", times.slowest_ms}, {"at_s", times.slowest_at_s}}},
                {"render_ms", to_json(distribution(times.render))},
                {"cpu_percent_of_one_core", (cpu_seconds() - cpu_start) / wall * 100.0},
                {"cells_report", reports},
                {"map", map_report(window)},
                {"site_map", proxy ? QJsonObject{{"region", site_map},
                                                 {"forwarded", proxy->forwarded()},
                                                 {"failed", proxy->failed()}}
                                   : QJsonValue{}},
            };
        }
        const auto json = QJsonDocument(report).toJson();
        if (parser.isSet("report")) {
            QFile file{parser.value("report")};
            if (!file.open(QIODevice::WriteOnly) || file.write(json) != json.size())
                status = 1;
        } else {
            std::fwrite(json.constData(), 1, static_cast<std::size_t>(json.size()), stdout);
        }
        for (const auto& cell : group.streams()) {
            if (!cell->error().isEmpty())
                status = 1;
        }
        QGuiApplication::exit(status);
    });
    return QGuiApplication::exec();
}
