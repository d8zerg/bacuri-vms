// Video prototype of the desktop client. Plays one source in a grid of cells through the
// selected decoder into Qt Quick on OpenGL, seeks once and writes a JSON report with the
// decoder output memory, the frames that reached the display, the first frame after the
// start and after the seek, the frame time of the interface and the CPU time.

#include "prototype/measure.hpp"
#include "prototype/video_cell.hpp"

#include <QCommandLineParser>
#include <QFile>
#include <QFileInfo>
#include <QUrl>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QQmlApplicationEngine>
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

namespace
{
using namespace bacuri::desktop;
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

// Frame times of the scene graph, recorded on the render thread.
struct FrameTimes {
    std::mutex mutex;
    std::optional<Clock::time_point> previous_swap;
    std::optional<Clock::time_point> render_start;
    std::optional<Clock::time_point> measure_from;
    std::vector<double> intervals;
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
        if (measuring && previous_swap)
            intervals.push_back(std::chrono::duration<double, std::milli>(now - *previous_swap).count());
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
        {"source", "MP4 file or rtsp:// address.", "source"},
        {"codec", "h264 or h265.", "codec", "h264"},
        {"decoder", "auto, hardware or software.", "decoder", "auto"},
        {"fault-decoder-at", "Seconds after the start to report a hardware decoder failure, -1 to skip.",
         "seconds", "-1"},
        {"cells", "Number of cells showing the source.", "cells", "1"},
        {"seconds", "Duration of the run.", "seconds", "20"},
        {"warmup", "Seconds before the frame time is measured.", "warmup", "3"},
        {"seek-frame", "Frame to seek to in the middle of the run, -1 to skip.", "frame", "-1"},
        {"map", "MapLibre style shown next to the grid.", "style"},
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

    std::vector<std::unique_ptr<VideoCell>> cells;
    for (int index = 0; index < cell_count; ++index) {
        const auto choice = parser.value("decoder") == "hardware"   ? DecoderChoice::hardware
                            : parser.value("decoder") == "software" ? DecoderChoice::software
                                                                    : DecoderChoice::automatic;
        cells.push_back(std::make_unique<VideoCell>(
            index, CellOptions{parser.value("source"), parser.value("codec"), choice}));
        cells.back()->create();
    }

    QQmlApplicationEngine engine;
    const auto map_style = parser.isSet("map")
                               ? QUrl::fromLocalFile(QFileInfo{parser.value("map")}.absoluteFilePath()).toString()
                               : QString{};
    engine.setInitialProperties({{"cellCount", cell_count}, {"mapStyle", map_style}});
    engine.loadFromModule("Bacuri.Prototype", "Main");
    if (engine.rootObjects().isEmpty())
        return 1;
    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().constFirst());

    FrameTimes times;
    const auto started = Clock::now();
    times.measure_from = started + std::chrono::seconds{parser.value("warmup").toInt()};
    QObject::connect(
        window, &QQuickWindow::beforeRendering, window, [&times] { times.before_rendering(); },
        Qt::DirectConnection);
    QObject::connect(
        window, &QQuickWindow::frameSwapped, window, [&times] { times.swapped(); }, Qt::DirectConnection);

    for (int index = 0; index < cell_count; ++index) {
        cells[static_cast<std::size_t>(index)]->attach(
            find_item(window->contentItem(), QStringLiteral("video%1").arg(index)));
        cells[static_cast<std::size_t>(index)]->start(window);
    }

    const auto cpu_start = cpu_seconds();
    if (seek_frame >= 0) {
        QTimer::singleShot(std::chrono::milliseconds{seconds * 500}, window, [&cells, seek_frame] {
            for (auto& cell : cells)
                cell->seek_to_frame(seek_frame);
        });
    }
    const auto fault_at = parser.value("fault-decoder-at").toInt();
    if (fault_at >= 0) {
        QTimer::singleShot(std::chrono::seconds{fault_at}, window, [&cells] {
            for (auto& cell : cells)
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
        for (const auto& cell : cells)
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
                {"platform", QGuiApplication::platformName()},
                {"ui_frame_interval_ms", to_json(distribution(times.intervals))},
                {"render_ms", to_json(distribution(times.render))},
                {"cpu_percent_of_one_core", (cpu_seconds() - cpu_start) / wall * 100.0},
                {"cells_report", reports},
                {"map", map_report(window)},
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
        for (const auto& cell : cells) {
            if (!cell->error().isEmpty())
                status = 1;
        }
        QGuiApplication::exit(status);
    });
    return QGuiApplication::exec();
}
