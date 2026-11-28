#include "app/navigation.hpp"
#include "app/window_commands.hpp"
#include "commands/command_time.hpp"
#include <QCryptographicHash>
#include <QProcess>
#include "app/window_group.hpp"
#include "media/stream_group.hpp"
#include "live/live_preview.hpp"
#include "settings/preferences.hpp"

#include <QAccessible>
#include <QClipboard>
#include <QSignalSpy>
#include <QDBusInterface>
#include <QDesktopServices>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QQmlProperty>
#include <QQuickItem>
#include <QScreen>
#include <QSet>
#include <QStyleHints>
#include <QTest>
#include <QThreadPool>

#include <QUrlQuery>

#include <atomic>
#include <cmath>

#include <gst/gst.h>

namespace
{
using namespace bacuri::vms::client;

QQuickItem* item(QQuickItem* parent, const QString& name)
{
    if (parent->objectName() == name)
        return parent;
    for (auto* child : parent->childItems())
        if (auto* found = item(child, name))
            return found;
    return nullptr;
}

void selectOption(QQuickWindow* window, QQuickItem* choice, int index)
{
    QVERIFY(choice && choice->isVisible());
    choice->forceActiveFocus(Qt::TabFocusReason);
    QTest::keyClick(window, Qt::Key_Space);
    auto* popup = choice->property("popup").value<QObject*>();
    QVERIFY(popup && QTest::qWaitFor([&] { return popup->property("opened").toBool(); }));
    QTest::keyClick(window, Qt::Key_Home);
    for (int i = 0; i < index; ++i)
        QTest::keyClick(window, Qt::Key_Down);
    QTest::keyClick(window, Qt::Key_Return);
    QCOMPARE(choice->property("currentIndex").toInt(), index);
}

QJsonObject tree(QAccessibleInterface* node)
{
    QJsonArray children;
    for (int i = 0; i < node->childCount(); ++i) {
        auto* child = node->child(i);
        if (child && !child->state().invisible)
            children.append(tree(child));
    }
    QJsonObject result{
        {"role", QAccessible::staticMetaObject.enumerator(QAccessible::staticMetaObject.indexOfEnumerator("Role"))
                     .valueToKey(node->role())},
        {"name", node->text(QAccessible::Name)},
        {"children", children}};
    if (node->role() == QAccessible::ComboBox)
        result.insert("description", node->text(QAccessible::Description));
    return result;
}

void capture(QQuickWindow* window, const QString& name, const QString& expected, QPoint cursor = {1, 1})
{
    QTest::mouseMove(window, cursor);
    QTest::qWait(140);
    QCoreApplication::processEvents();
    const QDir dir(qEnvironmentVariable("BACURI_SHELL_REPORT"));
    QImage image;
    QVERIFY(QTest::qWaitFor([&] {
        image = window->grabWindow();
        return image.size() == (window->size() * window->devicePixelRatio());
    }));
    QVERIFY(image.save(dir.filePath(name + ".png")));
    auto* root = QAccessible::queryAccessibleInterface(window);
    QVERIFY(root);
    auto structure = tree(root);
    QJsonArray order;
    auto* start = window->contentItem();
    QSet<QQuickItem*> seen;
    for (auto* next = start->nextItemInFocusChain(true); next && !seen.contains(next);
         next = next->nextItemInFocusChain(true)) {
        seen.insert(next);
        if (!next->isVisible() || !next->isEnabled() || !next->activeFocusOnTab())
            continue;
        auto* accessible = QAccessible::queryAccessibleInterface(next);
        if (accessible)
            order.append(
                QJsonObject{{"name", accessible->text(QAccessible::Name)}, {"object", next->objectName()}});
    }
    structure.insert("focusOrder", order);
    if (name.startsWith("playback-")) {
        auto* media = item(window->contentItem(), "mediaSurface");
        QVERIFY(media && media->isVisible());
        const auto logical = media->mapRectToScene(media->boundingRect());
        const auto dpr = window->devicePixelRatio();
        structure.insert("videoRect", QJsonArray{qRound(logical.x() * dpr), qRound(logical.y() * dpr),
                                                 qRound(logical.width() * dpr), qRound(logical.height() * dpr)});
    }
    const auto bytes = QJsonDocument(structure).toJson();
    QVERIFY2(bytes.contains(expected.toUtf8()), bytes.constData());
    QFile file(dir.filePath(name + ".json"));
    QVERIFY(file.open(QFile::WriteOnly));
    QCOMPARE(file.write(bytes), bytes.size());
}

class ShellTest : public QObject
{
    Q_OBJECT
  public slots:
    void confirm(const QUrl& url)
    {
        const QUrlQuery query(url);
        QUrl callback(query.queryItemValue("redirect_uri", QUrl::FullyDecoded));
        QUrlQuery answer;
        answer.addQueryItem("state", query.queryItemValue("state"));
        answer.addQueryItem("code", "test-code");
        callback.setQuery(answer);
        auto* reply = browser_.get(QNetworkRequest(callback));
        connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
    }

  private:
    QNetworkAccessManager browser_;

  private slots:
    void init()
    {
        QTest::failOnWarning(QRegularExpression("qrc:|QQml|Binding loop|recursive rearrange|Untrusted OpenGL"));
    }
    void modelsAndIntents()
    {
        QVERIFY(SearchMatcher("`krf").matches("ёлка"));
        PointModel model;
        const QVariantMap gate{{"id", "g"}, {"name", "Ворота Gate"}, {"recording", "current"}};
        const QVariantMap hall{{"id", "h"}, {"name", "Hall"}};
        model.apply({gate, hall});
        const QPersistentModelIndex index = model.index(0);
        PointFilterModel proxy;
        proxy.setSourceModel(&model);
        for (const auto& query : {"djhjnf", "ворта", "gte", "Gtae", "Gate"}) {
            proxy.setQuery(query);
            QCOMPARE(proxy.count(), QString(query) == "gte" ? 0 : 1);
        }
        auto changed = gate;
        changed["name"] = "South";
        model.apply({changed});
        QVERIFY(index.isValid());
        QCOMPARE(model.rowFor("g"), 0);
        QCOMPARE(proxy.count(), 0);
        QCOMPARE(model.lookup("g").value("name"), "South");
        QVariantList fleet;
        for (int id = 0; id < 4096; ++id)
            fleet.append(QVariantMap{{"id", QString::number(id)}, {"name", QString("Camera %1").arg(id)}});
        model.apply(fleet);
        const QPersistentModelIndex selected = model.index(model.rowFor("4095"));
        const auto before = model.version();
        model.apply(fleet);
        QCOMPARE(model.version(), before);
        fleet.removeFirst();
        model.apply(fleet);
        QVERIFY(selected.isValid());
        QCOMPARE(selected.row(), model.rowFor("4095"));
        proxy.setQuery("Сфьукф");
        QCOMPARE(proxy.count(), 4095);
        QCOMPARE(proxy.idAt(proxy.rowFor("4095")), "4095");
        Navigation nav;
        nav.push(Navigation::Point, "g", {{"role", "point"}, {"id", "g"}});
        nav.push(Navigation::Settings, "", {{"role", "control"}, {"id", "back"}});
        WindowGroup windows({}, true);
        auto* restored = windows.open(nav.save());
        QVERIFY(restored);
        auto* intents = restored->property("navigation").value<Navigation*>();
        QVERIFY(intents);
        QCOMPARE(intents->save(), nav.save());
        Navigation copy;
        QVERIFY(copy.restore(nav.save()));
        copy.back();
        QCOMPARE(copy.pointId(), "g");
        QCOMPARE(copy.focusTarget().value("id"), "back");
        copy.back();
        QCOMPARE(copy.focusTarget().value("id"), "g");
        QVERIFY(!copy.restore({QVariantMap{{"page", "bogus"}}}));
    }

    void componentPreview()
    {
        WindowGroup group({}, true);
        auto* window = group.open();
        QVERIFY(window && QTest::qWaitForWindowExposed(window));
        window->requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(window));
        auto* button = item(window->contentItem(), "componentPreview");
        QVERIFY(button);
        button->forceActiveFocus(Qt::TabFocusReason);
        QTest::keyClick(window, Qt::Key_Space);
        auto* component = item(window->contentItem(), "previewComponent");
        auto* state = item(window->contentItem(), "previewState");
        auto* loader = item(window->contentItem(), "previewItem");
        QVERIFY(component && state && loader);
        QCOMPARE(component->property("count").toInt(), 10);
        QCOMPARE(state->property("count").toInt(), 5);
        for (int type = 0; type < component->property("count").toInt(); ++type) {
            QVERIFY(component->setProperty("currentIndex", type));
            for (int scenario = 0; scenario < 5; ++scenario) {
                QVERIFY(state->setProperty("currentIndex", scenario));
                auto* example = loader->property("item").value<QQuickItem*>();
                QVERIFY(example && example->isVisible());
                QCOMPARE(example->isEnabled(), scenario == 0);
            }
        }
    }

    void inputLatency()
    {
        WindowGroup group({}, true);
        QJsonArray reports;
        const QList<QQuickWindow*> windows{group.open(), group.open()};
        for (int w = 0; w < windows.size(); ++w) {
            auto* window = windows[w];
            QVERIFY(window);
            window->requestActivate();
            QVERIFY(QTest::qWaitForWindowActive(window));
            auto* field = item(window->contentItem(), "filter");
            QVERIFY(field);
            field->forceActiveFocus(Qt::TabFocusReason);
            std::atomic_int requested{}, synchronized{}, presented{};
            std::atomic_llong elapsed{};
            QElapsedTimer timer;
            auto sync = connect(
                window, &QQuickWindow::afterSynchronizing, window, [&] { synchronized.store(requested.load()); },
                Qt::DirectConnection);
            auto swap = connect(
                window, &QQuickWindow::frameSwapped, window,
                [&] {
                    int frame = synchronized.load();
                    if (frame > presented.load()) {
                        elapsed.store(timer.nsecsElapsed());
                        presented.store(frame);
                    }
                },
                Qt::DirectConnection);
            QList<double> samples;
            for (int i = 1; i <= 100; ++i) {
                timer.start();
                QTest::keyClick(window, i % 2 ? Qt::Key_X : Qt::Key_Backspace);
                requested.store(i);
                window->update();
                if (!QTest::qWaitFor([&] { return presented.load() == i; }, 2000)) {
                    disconnect(sync);
                    disconnect(swap);
                    QFAIL("No presented frame after input");
                }
                samples.append(double(elapsed.load()) / 1e6);
            }
            disconnect(sync);
            disconnect(swap);
            std::ranges::sort(samples);
            reports.append(QJsonObject{
                {"window", w + 1}, {"samples", samples.size()}, {"p95_ms", samples[94]}, {"p99_ms", samples[98]}});
        }
        QFile report(QDir(qEnvironmentVariable("BACURI_SHELL_REPORT")).filePath("input-latency.json"));
        QVERIFY(report.open(QFile::WriteOnly));
        report.write(QJsonDocument(reports).toJson());
        for (const auto sample : reports) {
            const auto stats = sample.toObject();
            QVERIFY2(stats["p95_ms"].toDouble() <= 100 && stats["p99_ms"].toDouble() <= 150,
                     qPrintable(QString::fromUtf8(QJsonDocument(stats).toJson())));
        }
    }

    void largePalette()
    {
        class LargeWorkspace : public Workspace
        {
          public:
            using Workspace::setCommandObjects;
            using Workspace::update;
            bool signedIn() const override
            {
                return true;
            }
            void loadCommandObjects() override {}
        } workspace;
        QVariantList points, alerts;
        for (int i = 0; i < 10000; ++i) {
            const auto id = QString::number(i);
            points.append(QVariantMap{{"id", id},
                                      {"name", "Ворота " + id},
                                      {"site_id", "large"},
                                      {"connection", "online"},
                                      {"recording", "current"}});
        }
        workspace.points()->apply(points);
        for (int i = 0; i < 50000; ++i) {
            const auto id = QString::number(i);
            alerts.append(
                QVariantMap{{"kind", "alert"}, {"site", "large"}, {"id", id}, {"name", "Парковка " + id}});
        }
        workspace.setCommandObjects(alerts);
        workspace.update(Workspace::Ready);
        WindowGroup group({}, false);
        QQmlComponent shell(&group.engine());
        shell.loadFromModule("Bacuri.Vms.Client.App", "Shell");
        std::unique_ptr<QQuickWindow> window(qobject_cast<QQuickWindow*>(
            shell.createWithInitialProperties({{"workspace", QVariant::fromValue<Workspace*>(&workspace)}})));
        QVERIFY2(window, qPrintable(shell.errorString()));
        window->requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(window.get()));
        auto* commands = window->findChild<WindowCommands*>("commandRegistry");
        QSignalSpy swap(window.get(), &QQuickWindow::frameSwapped);
        QElapsedTimer clock;
        clock.start();
        QTest::keyClick(window.get(), Qt::Key_K, Qt::ControlModifier);
        auto* list = item(window->contentItem(), "commandResults");
        QVERIFY(list &&
                QTest::qWaitFor([&] { return list->isVisible() && list->property("count").toInt() == 50; }));
        window->update();
        QVERIFY(QTest::qWaitFor([&] { return swap.count() > 0; }));
        const auto first = clock.elapsed();
        clock.restart();
        const auto matches = commands->search("vorota 9999");
        const auto typed = clock.elapsed();
        QVERIFY(!matches.isEmpty());
        QCOMPARE(matches.first().toMap().value("key").toString(), "point/9999");
        QVERIFY(!commands->search("parkovka").isEmpty());
        QFile report(QDir(qEnvironmentVariable("BACURI_SHELL_REPORT")).filePath("large-palette.json"));
        QVERIFY(report.open(QIODevice::WriteOnly));
        report.write(QJsonDocument(QJsonObject{{"points", 10000},
                                               {"alerts", 50000},
                                               {"first_results_frame_ms", first},
                                               {"typed_search_ms", typed}})
                         .toJson());
    }
    void commandProfiles()
    {
        const QString first = "test-user-1-site-1", second = "test-user-1-site-2", other = "test-user-2-site-1";
        {
            CommandRegistry registry;
            registry.bind(first, "archive", "Alt+J");
            registry.remember(first, "point/gate");
            registry.remember(first, "point/gate");
            registry.remember(second, "point/lobby");
        }
        CommandRegistry restored;
        QCOMPARE(restored.bindings(first).value("archive").toString(), "Alt+J");
        QCOMPARE(restored.usage(first, "point/gate").count, 2);
        QCOMPARE(restored.usage(second, "point/gate").count, 0);
        QCOMPARE(restored.usage(second, "point/lobby").count, 1);
        QVERIFY(restored.bindings(second).isEmpty());
        QVERIFY(restored.bindings(other).isEmpty());
        QCOMPARE(restored.usage(other, "point/gate").count, 0);
    }
    void commandTimes()
    {
        const QTimeZone zone("Europe/Moscow");
        const auto now = QDateTime::fromString("2020-02-03T00:15:00+03:00", Qt::ISODate);
        for (const auto& text : {"14:30", "14:30 MSK", "вчера 14:30", "yesterday 2:30pm"}) {
            const auto time = commandTime(text, zone, now);
            QCOMPARE(time.toString(Qt::ISODate), "2020-02-02T11:30:00Z");
        }
        QCOMPARE(commandTime("-10m", zone, now), now.addSecs(-600));
        const auto range = commandInterval("23:50+5m", zone, now);
        QCOMPARE(range.start.secsTo(range.end), 300);
        const auto midnight = commandInterval("23:50/00:05", zone, now);
        QCOMPARE(midnight.start.secsTo(midnight.end), 900);
        const auto yesterday = commandInterval("вчера 14:30/yesterday 2:35pm", zone, now);
        QCOMPARE(yesterday.start.secsTo(yesterday.end), 300);
        const auto duringEvent = QDateTime::fromString("2020-02-03T14:32:00+03:00", Qt::ISODate);
        const auto ongoing = commandInterval("14:30/14:35", zone, duringEvent);
        QCOMPARE(ongoing.start.toString(Qt::ISODate), "2020-02-03T11:30:00Z");
        QCOMPARE(ongoing.end, duringEvent.toUTC());
        QCOMPARE(commandInterval("14:30+5m", zone, duringEvent).end, ongoing.end);
        const auto earlierDay = commandInterval("14:33/14:35", zone, duringEvent);
        QCOMPARE(earlierDay.start.toString(Qt::ISODate), "2020-02-02T11:33:00Z");
        QCOMPARE(earlierDay.end.toString(Qt::ISODate), "2020-02-02T11:35:00Z");
        const QTimeZone berlin("Europe/Berlin");
        const auto fall = QDateTime::fromString("2020-10-25T16:00:00Z", Qt::ISODate);
        QVERIFY(!commandTime("02:30", berlin, fall).isValid());
        QVERIFY(commandTime("2020-10-25T02:30:00+01:00", berlin, fall).isValid());
        QVERIFY(!commandTime("24:80", zone, now).isValid());
    }
    void commandWorkflow()
    {
        WindowGroup group(
            {QUrl(qEnvironmentVariable("BACURI_TEST_SITE")), qEnvironmentVariable("BACURI_TEST_CA"), {}, 8},
            false);
        auto* preferences =
            group.engine().singletonInstance<Preferences*>("Bacuri.Vms.Client.Settings", "Preferences");
        preferences->setLanguage("en");
        preferences->setTextScale(1.);
        preferences->setTheme("light");
        auto* first = group.open();
        auto* window = group.open();
        QVERIFY(first && window);
        auto* registry = window->findChild<WindowCommands*>("commandRegistry");
        auto* peer = first->findChild<WindowCommands*>("commandRegistry");
        auto* panel = window->findChild<QObject*>("commandPanel");
        auto* nav = window->property("navigation").value<Navigation*>();
        auto* site = window->property("workspace").value<Workspace*>();
        QVERIFY(registry && peer && panel && nav && site);
        auto find = [&](const QString& term, const QString& prefix) {
            for (const auto& row : registry->search(term)) {
                const auto key = row.toMap().value("key").toString();
                if (key.startsWith(prefix))
                    return key;
            }
            return QString{};
        };
        QVERIFY(find("Gate", "point/").isEmpty());
        QDesktopServices::setUrlHandler("https", this, "confirm");
        site->signIn(site->address());
        QVERIFY(QTest::qWaitFor([&] { return site->state() == Workspace::Ready; }));
        window->requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(window));
        QElapsedTimer timer;
        timer.start();
        QSignalSpy swaps(window, &QQuickWindow::frameSwapped);
        QTest::keyClick(window, Qt::Key_K, Qt::ControlModifier);
        auto* query = item(window->contentItem(), "commandQuery");
        auto* list = item(window->contentItem(), "commandResults");
        QVERIFY(query && list);
        QVERIFY(QTest::qWaitFor(
            [&] { return query->hasActiveFocus() && list->isVisible() && list->property("count").toInt() > 0; }));
        window->update();
        QVERIFY(swaps.wait(1000));
        const auto paletteMs = timer.elapsed();
        QVERIFY(QTest::qWaitFor([&] { return site->index().objects().size() == 3; }));
        QVERIFY(!find("Entrance", "layout/").isEmpty());
        QVERIFY(!find("alarm", "alert/").isEmpty());
        QVERIFY(!find("CASE-72", "case/").isEmpty());
        QVERIFY(!find("screen", "screen/").isEmpty());
        QVERIFY(find("forbidden-alarm", "alert/").isEmpty());
        const auto signalFeed = [&](QString name) {
            QFile signal(QDir(qEnvironmentVariable("BACURI_COMMAND_FILES")).filePath(name));
            return signal.open(QIODevice::WriteOnly);
        };
        QVERIFY(signalFeed("upsert"));
        QVERIFY(QTest::qWaitFor([&] { return !find("streamed", "alert/").isEmpty(); }));
        QVERIFY(signalFeed("remove"));
        QVERIFY(QTest::qWaitFor([&] { return find("streamed", "alert/").isEmpty(); }));
        QCOMPARE(find("Пфеу", "point/"), find("Gate", "point/"));
        QCOMPARE(find("Gtae", "point/"), find("Gate", "point/"));
        auto paste = [&](const QString& text) {
            QGuiApplication::clipboard()->setText(text);
            QTest::keyClick(window, Qt::Key_A, Qt::ControlModifier);
            QTest::keyClick(window, Qt::Key_V, Qt::ControlModifier);
        };
        QSignalSpy archive(registry, &WindowCommands::archiveLoaded);
        paste("Gate A @ 2000-01-01T12:15:00Z");
        capture(window, "command-palette-en", "Camera · Gate A");
        auto* panelBackground = panel->property("background").value<QQuickItem*>();
        QVERIFY(panelBackground);
        const auto lightPanel = panelBackground->property("color").value<QColor>();
        preferences->setTheme("dark");
        QVERIFY(QTest::qWaitFor([&] { return panelBackground->property("color").value<QColor>() != lightPanel; }));
        capture(window, "command-palette-dark-en", "Camera · Gate A");
        QVERIFY(query->hasActiveFocus());
        preferences->setTheme("light");
        timer.restart();
        QTest::keyClick(window, Qt::Key_Return);
        QVERIFY(QTest::qWaitFor([&] { return archive.size() == 1; }));
        QCOMPARE(panel->property("mode").toString(), "intervals");
        QCOMPARE(archive.last()[1].toString().mid(11), "12:15:00.000Z");
        QVERIFY(archive.last()[2].toList().size() == 1);
        const auto archiveMs = timer.elapsed();
        const auto id = nav->pointId();
        QCOMPARE(nav->page(), Navigation::Point);
        QTest::keyClick(window, Qt::Key_Escape);
        QVERIFY(QTest::qWaitFor([&] { return !panel->property("visible").toBool(); }));
        const auto firstKey = registry->search("").first().toMap().value("key").toString();
        QVERIFY(firstKey.startsWith("command/"));
        QVERIFY(!registry->setBinding("archive", "Ctrl+K").isEmpty());
        QVERIFY(!registry->setBinding("archive", "Ctrl+C").isEmpty());
        registry->invoke("bindings");
        QCOMPARE(nav->page(), Navigation::Settings);
        auto* disclosure = item(window->contentItem(), "shortcutDisclosure");
        auto* table = item(window->contentItem(), "shortcutTable");
        QVERIFY(disclosure && table);
        QVERIFY(QTest::qWaitFor([&] { return table->isVisible() && disclosure->hasActiveFocus(); }));
        QVERIFY(!panel->property("visible").toBool());
        QTest::keyClick(window, Qt::Key_Space);
        QVERIFY(!table->isVisible());
        QTest::keyClick(window, Qt::Key_Space);
        QVERIFY(table->isVisible());
        capture(window, "shortcut-table-en", "Change shortcut for Command palette");
        auto* edit = item(table, "shortcut-archive");
        QVERIFY(edit);
        edit->forceActiveFocus(Qt::TabFocusReason);
        QTest::keyClick(window, Qt::Key_Space);
        QTest::keyClick(window, Qt::Key_K, Qt::ControlModifier);
        QCOMPARE(nav->page(), Navigation::Settings);
        QCOMPARE(edit->property("text").toString(), "Press a shortcut");
        const auto accessibleTable = tree(QAccessible::queryAccessibleInterface(table));
        QVERIFY(QJsonDocument(accessibleTable).toJson().contains("Already assigned to Command palette."));
        QVERIFY(!panel->property("visible").toBool());
        QTest::keyClick(window, Qt::Key_Escape);
        QCOMPARE(nav->page(), Navigation::Settings);
        QCOMPARE(edit->property("text").toString(), "Unassigned");
        QTest::keyClick(window, Qt::Key_Space);
        QTest::keyClick(window, Qt::Key_J, Qt::ControlModifier | Qt::AltModifier);
        QCOMPARE(edit->property("text").toString(), "Ctrl+Alt+J");
        auto* reset = item(table, "restore-archive");
        QVERIFY(reset && reset->isVisible());
        reset->forceActiveFocus(Qt::TabFocusReason);
        QTest::keyClick(window, Qt::Key_Space);
        QCOMPARE(edit->property("text").toString(), "Unassigned");
        QVERIFY(!reset->isVisible() && edit->hasActiveFocus());
        QTest::keyClick(window, Qt::Key_Space);
        QTest::keyClick(window, Qt::Key_J, Qt::ControlModifier | Qt::AltModifier);
        QTest::keyClick(window, Qt::Key_Escape);
        QCOMPARE(nav->page(), Navigation::Point);
        QCOMPARE(peer->entries()["archive"].toMap()["bindings"].toStringList().first(), "Ctrl+Alt+J");
        QVERIFY(site->registry()->bindings(site->profile(id)).contains("archive"));
        QTest::keyClick(window, Qt::Key_J, Qt::ControlModifier | Qt::AltModifier);
        QVERIFY(QTest::qWaitFor([&] { return panel->property("mode").toString() == "archive"; }));
        QTest::keyClick(window, Qt::Key_Escape);
        auto* area = item(window->contentItem(), "cameraContextArea");
        QVERIFY(area && area->isVisible());
        const auto center = area->mapToScene(area->boundingRect().center()).toPoint();
        QSignalSpy tools(registry, &WindowCommands::tool);
        QTest::mousePress(window, Qt::RightButton, {}, center);
        QTest::mouseRelease(window, Qt::RightButton, {}, center + QPoint(60, 0));
        QVERIFY(QTest::qWaitFor([&] { return tools.size() == 1; }));
        QCOMPARE(tools.last()[0], "archive");
        QTest::keyClick(window, Qt::Key_Escape);
        QTest::mouseClick(window, Qt::RightButton, {}, center);
        auto* action = item(window->contentItem(), "radial-archive");
        QVERIFY2(QTest::qWaitFor([&] { return action && action->isVisible(); }),
                 qPrintable(QString("radial %1, panel %2, page %3")
                                .arg(item(window->contentItem(), "cameraMenu")->isVisible())
                                .arg(panel->property("visible").toBool())
                                .arg(nav->page())));
        QCOMPARE(action->property("shortcutHint").toString(), "Ctrl+Alt+J");
        auto* menu = item(window->contentItem(), "cameraMenu");
        auto* description = item(menu, "radialDescription");
        QVERIFY(description && description->isVisible());
        const QPoint hub =
            menu->mapToScene({menu->property("cx").toDouble(), menu->property("cy").toDouble()}).toPoint();
        const int radius = qRound(menu->property("outerRadius").toDouble()) - 6;
        const QPoint archiveSector = hub + QPoint(radius, 0);
        QVERIFY(!action->contains(action->mapFromScene(archiveSector)));
        QTest::mouseMove(window, hub - QPoint(0, radius));
        QVERIFY(QTest::qWaitFor([&] { return description->property("text") == "Save the current frame"; }));
        QTest::mouseClick(window, Qt::LeftButton, {}, hub - QPoint(0, radius));
        QVERIFY(menu->isVisible());
        QCOMPARE(tools.size(), 1);
        QTest::mouseMove(window, archiveSector);
        QVERIFY(QTest::qWaitFor([&] { return description->property("text") == "Open the camera archive"; }));
        auto* ring = item(menu, "cameraActionRing");
        QVERIFY(ring);
        const auto sectorPixel = hub + QPoint(radius - 6, 0);
        const auto pointerRadius = menu->property("innerRadius").toDouble() * 0.93;
        const auto archivePointer = hub + QPoint(qRound(pointerRadius), 0);
        const auto diagonal = qRound(pointerRadius / std::sqrt(2.));
        const auto fullScreenPointer = hub + QPoint(diagonal, -diagonal);
        auto selectedArchive = [&] {
            const auto image = window->grabWindow();
            const auto scale = window->devicePixelRatio();
            return image.pixelColor(sectorPixel * scale).rgb() ==
                       ring->property("highlight").value<QColor>().rgb() &&
                   image.pixelColor(archivePointer * scale).rgb() ==
                       ring->property("focusOutline").value<QColor>().rgb();
        };
        QVERIFY(QTest::qWaitFor(selectedArchive));
        capture(window, "camera-actions-en", "Open archive", archiveSector);
        QTest::keyClick(window, Qt::Key_Left);
        QCOMPARE(description->property("text").toString(), "Show camera full screen");
        QVERIFY(QTest::qWaitFor([&] {
            const auto image = window->grabWindow();
            const auto scale = window->devicePixelRatio();
            const auto outline = ring->property("focusOutline").value<QColor>().rgb();
            return image.pixelColor(fullScreenPointer * scale).rgb() == outline &&
                   image.pixelColor(archivePointer * scale).rgb() != outline;
        }));
        QTest::keyClick(window, Qt::Key_Right);
        QVERIFY(QTest::qWaitFor(selectedArchive));
        const auto lightInk = description->property("color").value<QColor>();
        preferences->setTheme("dark");
        QVERIFY(QTest::qWaitFor([&] { return description->property("color").value<QColor>() != lightInk; }));
        QVERIFY(QTest::qWaitFor(selectedArchive));
        capture(window, "camera-actions-dark-en", "Open archive", archiveSector);
        QCOMPARE(menu->property("selection").toString(), "archive");
        preferences->setTheme("light");
        QTest::mouseClick(window, Qt::LeftButton, {}, archiveSector);
        QVERIFY(QTest::qWaitFor([&] { return tools.size() == 2; }));
        QCOMPARE(tools.last(), tools.first());
        QTest::keyClick(window, Qt::Key_Escape);
        QTest::mousePress(window, Qt::RightButton, {}, center);
        QVERIFY(QTest::qWaitFor([&] { return action->isVisible(); }));
        QTest::mouseRelease(window, Qt::RightButton, {}, archiveSector);
        QVERIFY(QTest::qWaitFor([&] { return tools.size() == 3; }));
        QCOMPARE(tools.last(), tools.first());
        QTest::keyClick(window, Qt::Key_Escape);
        QTest::keyClick(window, Qt::Key_Menu);
        QVERIFY(QTest::qWaitFor([&] { return action->isVisible(); }));
        QTest::keyClick(window, Qt::Key_Escape);
        QTest::keyClick(window, Qt::Key_F10, Qt::ShiftModifier);
        QVERIFY(QTest::qWaitFor([&] { return action->isVisible(); }));
        QCOMPARE(description->property("text").toString(), "Show camera full screen");
        QTest::keyClick(window, Qt::Key_Right);
        QCOMPARE(description->property("text").toString(), "Open the camera archive");
        QTest::keyClick(window, Qt::Key_Return);
        QVERIFY(QTest::qWaitFor([&] { return !action->isVisible(); }));
        QTest::keyClick(window, Qt::Key_Escape);
        QCOMPARE(registry->setBinding("archive", "", true), QString{});
        QSignalSpy feedback(registry, &WindowCommands::feedback);
        registry->pointAction("bookmark", id, "12:15", "", "Shift note");
        const auto bookmark = find("Shift note", "bookmark/");
        QVERIFY(!bookmark.isEmpty());
        registry->choose(bookmark);
        QVERIFY(QTest::qWaitFor([&] { return archive.size() == 2; }));
        QTest::keyClick(window, Qt::Key_Escape);
        auto* timeline = item(window->contentItem(), "timelinePosition");
        QVERIFY(timeline && timeline->isVisible());
        timeline->forceActiveFocus();
        const auto beforeStep = QDateTime::fromString(registry->archiveTime(), Qt::ISODateWithMs);
        QTest::keyClick(window, Qt::Key_Left);
        QVERIFY(QTest::qWaitFor([&] {
            return QDateTime::fromString(registry->archiveTime(), Qt::ISODateWithMs) == beforeStep.addSecs(-60);
        }));
        QVERIFY(!panel->property("visible").toBool());
        registry->pointAction("ptz", id, "right");
        QCOMPARE(feedback.last().first().toString(), "This action is unavailable.");
        auto* camera = item(window->contentItem(), "cameraCard");
        QVERIFY(camera);
        auto* cameraInput = item(camera, "cameraInput");
        QVERIFY(cameraInput);
        cameraInput->forceActiveFocus();
        QCOMPARE(window->activeFocusItem(), cameraInput);
        auto motionLog = [] {
            QFile file(QDir(qEnvironmentVariable("BACURI_COMMAND_FILES")).filePath("ptz.log"));
            return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
        };
        QTest::keyPress(window, Qt::Key_Left);
        QVERIFY2(QTest::qWaitFor([&] { return motionLog().count("left") >= 2; }),
                 qPrintable(QString("PTZ log %1; focus %2; modal %3")
                                .arg(QString::fromUtf8(motionLog()), window->activeFocusItem()->objectName())
                                .arg(registry->property("modal").toBool())));
        QVERIFY(!motionLog().contains("stop"));
        QTest::keyRelease(window, Qt::Key_Left);
        QVERIFY(QTest::qWaitFor([&] { return motionLog().contains("stop"); }));
        const auto movesBeforeDeactivate = motionLog().count("left");
        QTest::keyPress(window, Qt::Key_Left);
        QVERIFY(QTest::qWaitFor([&] { return motionLog().count("left") > movesBeforeDeactivate; }));
        const auto stopsBeforeDeactivate = motionLog().count("stop");
        first->requestActivate();
        QVERIFY(QTest::qWaitFor([&] { return !window->isActive(); }));
        QVERIFY(QTest::qWaitFor([&] { return motionLog().count("stop") > stopsBeforeDeactivate; }));
        window->requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(window));
        QTest::keyRelease(window, Qt::Key_Left);
        const auto cardCenter = camera->mapToScene(camera->boundingRect().center()).toPoint();
        const QList<QPoint> presetDirections{{0, -60}, {60, -60}, {60, 0},  {60, 60},
                                             {0, 60},  {-60, 60}, {-60, 0}, {-60, -60}};
        QCOMPARE(registry->presets().size(), presetDirections.size());
        for (int index = 0; index < presetDirections.size(); ++index) {
            const auto pivot = cardCenter + QPoint(-75, 0);
            const auto direction = presetDirections.at(index);
            QTest::mousePress(window, Qt::RightButton, {}, cardCenter);
            QTest::mouseMove(window, pivot);
            // Two strokes in the same direction are separated by a dwell.
            if (direction == QPoint(-60, 0))
                QVERIFY(QTest::qWaitFor([&] { return menu->property("presets").toBool(); }));
            for (int step = 1; step <= 10; ++step)
                QTest::mouseMove(window, pivot + direction * step / 10);
            QTest::mouseRelease(window, Qt::RightButton, {}, pivot + direction);
            QCOMPARE(panel->property("mode").toString(), "ptz");
            const auto command = "preset_goto position-" + QByteArray::number(index + 1);
            QVERIFY(!motionLog().contains(command));
            QVERIFY(QMetaObject::invokeMethod(panel, "submit"));
            QVERIFY2(QTest::qWaitFor([&] { return motionLog().contains(command); }), command.constData());
        }
        registry->choose("command/export", "12:15/12:16");
        QVERIFY(QTest::qWaitFor([&] { return feedback.last().first().toString().startsWith("Saved to "); }));
        QDir files(qEnvironmentVariable("BACURI_COMMAND_FILES"));
        auto packages = files.entryList({"*.tar"});
        QCOMPARE(packages.size(), 1);
        QFile package(files.filePath(packages.first()));
        QVERIFY(package.open(QIODevice::ReadOnly));
        QCOMPARE(package.readAll(), "test export payload");
        // Closing the origin window leaves both the download and its queue in the workspace.
        auto* origin = group.open();
        auto* originCommands = origin->findChild<WindowCommands*>("commandRegistry");
        originCommands->choose("point/" + id);
        originCommands->choose("command/export", "2001-01-01T12:00:00Z/2001-01-01T16:00:00Z");
        originCommands->choose("command/export", "2001-01-01T16:00:00Z/2001-01-01T17:00:00Z");
        auto exportRows = site->exports()->rows();
        QCOMPARE(exportRows.size(), 3);
        QCOMPARE(exportRows.at(1).toMap().value("state").toString(), "running");
        const auto queued = exportRows.last().toMap().value("id").toString();
        site->exports()->cancel(queued);
        QCOMPARE(site->exports()->rows().last().toMap().value("state").toString(), "cancelled");
        site->exports()->retry(queued);
        QPointer<QQuickWindow> closing = origin;
        origin->close();
        QVERIFY(QTest::qWaitFor([&] { return !closing && !site->exports()->busy(); }));
        for (const auto& job : site->exports()->rows())
            QCOMPARE(job.toMap().value("state").toString(), "complete");
        window->requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(window));
        QVERIFY(find("snapshot", "command/snapshot").isEmpty());
        auto* surface = item(window->contentItem(), "mediaSurface");
        QVERIFY(surface);
        QQmlComponent frame(&group.engine());
        frame.setData("import QtQuick; Rectangle { width: 64; height: 48; color: 'red' }", QUrl{});
        std::unique_ptr<QQuickItem> video(qobject_cast<QQuickItem*>(frame.create()));
        QVERIFY(video);
        video->setParentItem(surface);
        QVERIFY(find("snapshot", "command/snapshot").isEmpty());
        NativeFrame native;
        auto* pipeline = gst_parse_launch("videotestsrc num-buffers=1 pattern=red ! "
                                          "video/x-raw,format=RGBA,width=320,height=180 ! fakesink name=capture",
                                          nullptr);
        QVERIFY(pipeline);
        gst_element_set_state(pipeline, GST_STATE_PAUSED);
        QCOMPARE(gst_element_get_state(pipeline, nullptr, nullptr, 3 * GST_SECOND), GST_STATE_CHANGE_SUCCESS);
        auto* sink = gst_bin_get_by_name(GST_BIN(pipeline), "capture");
        GstSample* sample{};
        g_object_get(sink, "last-sample", &sample, nullptr);
        QVERIFY(sample);
        auto* pad = gst_element_get_static_pad(sink, "sink");
        native.receive(pad, gst_sample_get_buffer(sample));
        native.attach(video.get());
        gst_sample_unref(sample);
        gst_object_unref(pad);
        gst_object_unref(sink);
        gst_element_set_state(pipeline, GST_STATE_NULL);
        gst_object_unref(pipeline);
        QVERIFY(!find("snapshot", "command/snapshot").isEmpty());
        registry->invoke("snapshot");
        QVERIFY(QTest::qWaitFor([&] { return files.entryList({"*.png"}).size() == 1; }));
        QImage image(files.filePath(files.entryList({"*.png"}).first()));
        QCOMPARE(image.size(), QSize(320, 180));
        QCOMPARE(image.text("point_id"), id);
        QCOMPARE(image.text("access_revision"), "1");
        QCOMPARE(image.text("pts_ns"), "0");
        QCOMPARE(image.text("audit_receipt"), "e8af2570-ef06-42f4-a70a-5831317a9e7b");
        const auto rgba = image.convertToFormat(QImage::Format_RGBA8888);
        QCOMPARE(image.text("sha256_rgba8").toLatin1(),
                 QCryptographicHash::hash(
                     QByteArrayView(reinterpret_cast<const char*>(rgba.constBits()), rgba.sizeInBytes()),
                     QCryptographicHash::Sha256)
                     .toHex());
        video.reset();
        registry->openPalette();
        auto* preview = window->property("previewStream").value<LivePreview*>();
        QVERIFY(preview);
        QVERIFY(QTest::qWaitFor([&] { return query->hasActiveFocus(); }));
        paste("Gate A");
        // Hover the result and transfer the same decoded stream to the camera card.
        auto* currentRow = list->property("currentItem").value<QQuickItem*>();
        QVERIFY(currentRow);
        QTest::mouseMove(window, currentRow->mapToScene(currentRow->boundingRect().center()).toPoint());
        QVERIFY(QTest::qWaitFor([&] { return preview->property("ready").toBool(); }, 12000));
        auto* source = NativeFrame::source(preview);
        QVERIFY(source && source->sample());
        const auto decoded = preview->report();
        const auto copies = decoded.value("system_memory_copies_per_frame").toInt(-1);
        QVERIFY(copies == 0 || copies == 1);
        if (qEnvironmentVariableIsSet("BACURI_TEST_REQUIRE_GPU_VIDEO"))
            QCOMPARE(copies, 0);
        auto showsVideo = [&] {
            const auto viewport = preview->boundingRect();
            const auto width = std::min(viewport.width(), viewport.height() * 16. / 9.);
            const auto videoPosition = viewport.center() + QPointF(width * .3, -viewport.height() * .28);
            const auto rendered = window->grabWindow();
            const auto pixel =
                rendered.pixelColor((preview->mapToScene(videoPosition) * window->devicePixelRatio()).toPoint());
            return pixel.red() > 150 && pixel.green() < 100 && pixel.blue() < 100;
        };
        QVERIFY(QTest::qWaitFor(showsVideo));
        QTest::keyClick(window, Qt::Key_Return);
        QVERIFY(QTest::qWaitFor([&] { return preview->parentItem() == surface && preview->isVisible(); }));
        QVERIFY(preview->ready());
        QCOMPARE(NativeFrame::source(preview), source);
        QCOMPARE(preview->report().value("first_frame"), decoded.value("first_frame"));
        QVERIFY(showsVideo());
        registry->invoke("snapshot");
        QVERIFY(QTest::qWaitFor([&] { return files.entryList({"*.png"}).size() == 2; }));
        for (const auto& file : files.entryList({"*.png"})) {
            const QImage saved(files.filePath(file));
            QCOMPARE(saved.size(), QSize(320, 180));
            QCOMPARE(saved.text("point_id"), id);
            QCOMPARE(saved.text("audit_receipt"), "e8af2570-ef06-42f4-a70a-5831317a9e7b");
        }
        const auto cameraGestures = registry->gestureCounts();
        registry->choose("point/second");
        QVERIFY(!registry->entries().value("export").toMap().value("available").toBool());
        QVERIFY(find("ptz", "command/ptz").isEmpty());
        registry->pointAction("export", "second", "12:00", "12:01");
        QCOMPARE(feedback.last().first().toString(), "This action is unavailable.");
        preferences->setLanguage("ru");
        preferences->setTextScale(2.);
        window->resize(420, 350);
        QTest::keyClick(window, Qt::Key_F10, Qt::ShiftModifier);
        QVERIFY(QTest::qWaitFor([&] { return menu->isVisible(); }));
        QCOMPARE(description->property("text").toString(), "Развернуть камеру на весь экран");
        QVERIFY(QTest::qWaitFor([&] {
            return description->property("contentWidth").toDouble() <= description->width() + 1 &&
                   description->property("contentHeight").toDouble() <= description->height() + 1;
        }));
        QTest::keyClick(window, Qt::Key_Escape);
        preferences->setLanguage("en");
        preferences->setTextScale(1.);
        QTest::keyClick(window, Qt::Key_K, Qt::ControlModifier);
        QVERIFY(QTest::qWaitFor([&] { return query->hasActiveFocus(); }));
        paste("Gate A");
        QTest::keyClick(window, Qt::Key_Space, Qt::ControlModifier);
        paste("South");
        QTest::keyClick(window, Qt::Key_Space, Qt::ControlModifier);
        QCOMPARE(panel->property("selectedPoints").toStringList().size(), 2);
        QTest::keyClick(window, Qt::Key_Return);
        QCOMPARE(nav->page(), Navigation::Layout);
        QCOMPARE(registry->layout().value("data").toMap().value("points").toList().size(), 2);
        auto* liveTile = item(window->contentItem(), "layoutVideo-0");
        QVERIFY(liveTile);
        QVERIFY(QTest::qWaitFor([&] { return liveTile->property("ready").toBool(); }, 12000));
        QTest::keyClick(window, Qt::Key_Z, Qt::ControlModifier);
        QCOMPARE(nav->page(), Navigation::Point);
        QTest::keyPress(window, Qt::Key_Control);
        QVERIFY(QTest::qWaitFor([&] { return registry->hints(); }));
        QTest::keyRelease(window, Qt::Key_Control);
        QVERIFY(!registry->hints());
        QVERIFY(!registry->setBinding("archive", "Alt+Tab").isEmpty());
        QVERIFY(!registry->setBinding("archive", "Ctrl+Alt+T").isEmpty());
        registry->choose(find("Entrance", "layout/"), "1");
        QCOMPARE(nav->page(), Navigation::Layout);
        QCOMPARE(registry->layout().value("data").toMap().value("points").toList().size(), 2);
        registry->choose(find("CASE-72", "case/"));
        QCOMPARE(panel->property("mode"), "case");
        QCOMPARE(panel->property("data").toMap()["data"].toMap()["notes"], "Gate inspection");
        const auto gestures = cameraGestures;
        QVERIFY(gestures.value("archive").toMap().value("stroke").toInt() > 0);
        QVERIFY(gestures.value("archive").toMap().value("menu").toInt() > 0);
        site->signOut();
        QVERIFY(QTest::qWaitFor([&] { return site->state() == Workspace::Disconnected; }));
        QVERIFY(!panel->property("visible").toBool());
        QVERIFY(registry->layout().isEmpty() && registry->search("Gate").isEmpty());
        QFile report(QDir(qEnvironmentVariable("BACURI_SHELL_REPORT")).filePath("commands.json"));
        QVERIFY(report.open(QIODevice::WriteOnly));
        report.write(QJsonDocument(QJsonObject{{"palette_first_frame_ms", paletteMs},
                                               {"parameter_archive_response_ms", archiveMs},
                                               {"marking_menu", QJsonObject::fromVariantMap(gestures)},
                                               {"webrtc", decoded}})
                         .toJson());
        QDesktopServices::unsetUrlHandler("https");
    }

    void siteSession()
    {
        const QUrl origin(qEnvironmentVariable("BACURI_TEST_SITE"));
        QVERIFY(!site_request(origin, {}).hasRawHeader("Authorization"));
        QCOMPARE(site_request(origin, {}, "sample").rawHeader("Authorization"), QByteArray("Bearer sample"));
        Workspace site({origin, qEnvironmentVariable("BACURI_TEST_CA"), {}, 8});
        QDesktopServices::setUrlHandler("https", this, "confirm");
        site.signIn(origin.toString());
        QCOMPARE(site.state(), Workspace::Authorizing);
        QVERIFY(QTest::qWaitFor([&] { return site.state() == Workspace::Ready; }));
        QVERIFY(site.signedIn());
        QCOMPARE(site.points()->count(), 2);
        QCOMPARE(site.points()->index(0).data(PointModel::Point).toMap().value("name").toString(), "Gate A");
        const QPersistentModelIndex stable = site.points()->index(0);
        const auto revision = site.points()->version();
        site.refresh();
        QVERIFY(stable.isValid() && site.points()->count() == 2);
        QVERIFY(QTest::qWaitFor([&] { return site.state() == Workspace::Failed; }));
        QCOMPARE(site.points()->count(), 2);
        QCOMPARE(site.cause(), "dependency_unavailable");
        QCOMPARE(site.failureText(), "A site service is unavailable. Try again later.");
        site.refresh();
        QVERIFY(QTest::qWaitFor([&] { return site.state() == Workspace::Ready; }));
        QVERIFY(stable.isValid());
        QCOMPARE(site.points()->version(), revision);
        site.refresh();
        site.signOut();
        QVERIFY(QTest::qWaitFor([&] { return site.state() == Workspace::Disconnected; }));
        QVERIFY(!site.signedIn());
        QCOMPARE(site.points()->count(), 0);
        site.signIn(origin.toString());
        site.cancel();
        QCOMPARE(site.state(), Workspace::Disconnected);
        Workspace untrusted({origin, {}, {}, 8});
        untrusted.signIn(origin.toString());
        QVERIFY(QTest::qWaitFor([&] { return untrusted.state() == Workspace::Failed; }));
        QCOMPARE(untrusted.cause(), "certificate_untrusted");
        QCOMPARE(untrusted.failureText(), "The site certificate is not trusted.");
        QVERIFY(!untrusted.signedIn());

        WindowGroup windows({origin, qEnvironmentVariable("BACURI_TEST_CA"), {}, 8}, false);
        auto* first = windows.open();
        auto* second = windows.open();
        QVERIFY(first && second);
        auto* shared = first->property("workspace").value<Workspace*>();
        QCOMPARE(shared, second->property("workspace").value<Workspace*>());
        shared->signIn(origin.toString());
        QVERIFY(QTest::qWaitFor([&] { return shared->state() == Workspace::Ready; }));
        auto* list = item(second->contentItem(), "pointList");
        QVERIFY(list);
        QVERIFY2(QTest::qWaitFor([&] { return list->isVisible(); }),
                 qPrintable(QString("model %1, view %2, connected %3, page %4")
                                .arg(shared->points()->count())
                                .arg(list->property("count").toInt())
                                .arg(second->property("connected").toBool())
                                .arg(second->property("overview").toBool())));
        QCOMPARE(list->property("count").toInt(), 2);
        auto* search = item(second->contentItem(), "filter");
        QVERIFY(search && search->isVisible());
        search->setProperty("text", "003  gAtE");
        QCOMPARE(list->property("count").toInt(), 1);
        list->forceActiveFocus(Qt::TabFocusReason);
        const auto scroll = list->property("contentY");
        shared->refresh();
        QVERIFY(list->isVisible() && list->hasActiveFocus());
        QVERIFY(QTest::qWaitFor([&] { return shared->state() == Workspace::Ready; }));
        QVERIFY(list->hasActiveFocus());
        QCOMPARE(list->property("contentY"), scroll);
        QCOMPARE(search->property("text").toString(), "003  gAtE");
        search->setProperty("text", "unmatched");
        QCOMPARE(list->property("count").toInt(), 0);
        QCOMPARE(item(first->contentItem(), "pointList")->property("count").toInt(), 2);
        windows.finish();
        QVERIFY(QTest::qWaitFor([&] { return !shared->signedIn(); }));
        QVERIFY(!list->isVisible());
        QDesktopServices::unsetUrlHandler("https");
    }
    void productWindow()
    {
        WindowGroup group({}, false);
        auto* window = group.open();
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        window->requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(window));
        auto* prefs = group.engine().singletonInstance<Preferences*>("Bacuri.Vms.Client.Settings", "Preferences");
        QCOMPARE(prefs->language(), "en");
        QVERIFY(!QSettings().contains("appearance/language"));
        prefs->setLanguage("en");
        QCOMPARE(QSettings().value("appearance/language").toString(), "en");
        QCOMPARE(prefs->theme(), "system");
        QVERIFY(QTest::qWaitFor([&] { return prefs->accent() == QColor::fromRgbF(.1f, .3f, .7f); }));
        auto* tokens = group.engine().singletonInstance<QObject*>("Bacuri.Vms.Client.Ui", "Tokens");
        QVERIFY(tokens);
        QDBusInterface appearance("org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop",
                                  "org.bacuri.TestSettings");
        QVERIFY(appearance.isValid());
        for (bool high : {true, false}) {
            appearance.call("Set", "contrast", uint(high));
            QVERIFY(QTest::qWaitFor([&] { return prefs->highContrast() == high; }));
            for (const auto& theme : {QString("light"), QString("dark")}) {
                prefs->setTheme(theme);
                QJsonObject colors;
                const auto* meta = tokens->metaObject();
                for (int i = 0; i < meta->propertyCount(); ++i) {
                    auto property = meta->property(i);
                    if (property.metaType() == QMetaType::fromType<QColor>())
                        colors.insert(property.name(), property.read(tokens).value<QColor>().name());
                }
                QFile palette(
                    QDir(qEnvironmentVariable("BACURI_SHELL_REPORT"))
                        .filePath("palette-" + (high ? QString("contrast-") : QString()) + theme + ".json"));
                QVERIFY(palette.open(QFile::WriteOnly));
                QVERIFY(palette.write(QJsonDocument(colors).toJson()) > 0);
            }
        }
        prefs->setTheme("system");
        auto* origin = item(window->contentItem(), "origin");
        QVERIFY(origin);
        for (int i = 0; i < 12 && window->activeFocusItem() != origin; ++i)
            QTest::keyClick(window, Qt::Key_Tab);
        QCOMPARE(window->activeFocusItem(), origin);
        origin->setProperty("text", "http://invalid");
        QTest::keyClick(window, Qt::Key_Return);
        capture(window, "sign-in-error-en", "Enter the HTTPS address of the site.");
        prefs->setLanguage("ru");
        QCOMPARE(origin->property("text").toString(), "http://invalid");
        QCOMPARE(window->activeFocusItem(), origin);
        capture(window, "sign-in-error-ru", "Введите HTTPS-адрес площадки.");
        auto* languageGroup = item(window->contentItem(), "languageSetting");
        QVERIFY(!languageGroup);
        QVERIFY(!item(window->contentItem(), "languageSwitch"));
        auto* settings = item(window->contentItem(), "settings");
        settings->forceActiveFocus(Qt::TabFocusReason);
        QTest::keyClick(window, Qt::Key_Space);
        QVERIFY(QTest::qWaitFor([&] { return window->title().startsWith("Настройки"); }));
        auto* russian = item(window->contentItem(), "languageSetting-1");
        auto* english = item(window->contentItem(), "languageSetting-0");
        QVERIFY(russian && english);
        const QHash<QString, QString> names{{"back", "Назад"},
                                            {"newWindow", "Новое окно"},
                                            {"settings", "Настройки"},
                                            {"themeSetting-0", "Системная"},
                                            {"themeSetting-1", "Светлая"},
                                            {"themeSetting-2", "Тёмная"}};
        QSet<QUrl> icons;
        for (auto it = names.cbegin(); it != names.cend(); ++it) {
            auto* button = item(window->contentItem(), it.key());
            QVERIFY(button && button->isVisible());
            QCOMPARE(button->property("display").toInt(), int(Qt::ToolButtonIconOnly));
            const auto icon = QQmlProperty::read(button, "icon.source").toUrl();
            QVERIFY(icon.isValid() && !icon.isEmpty());
            icons.insert(icon);
            auto* accessible = QAccessible::queryAccessibleInterface(button);
            QVERIFY(accessible);
            QCOMPARE(accessible->text(QAccessible::Name), it.value());
        }
        QCOMPARE(icons.size(), names.size());
        for (auto* button : {english, russian}) {
            QVERIFY(button->isVisible());
            QCOMPARE(button->property("display").toInt(), int(Qt::ToolButtonTextOnly));
            QCOMPARE(button->property("text").toString(), button == english ? "EN" : "RU");
            auto* accessible = QAccessible::queryAccessibleInterface(button);
            QVERIFY(accessible);
            QCOMPARE(accessible->text(QAccessible::Name), button == english ? "English" : "Русский");
        }

        const auto rowBounds = [&](const QString& name, bool stacked = false) {
            auto* control = item(window->contentItem(), name);
            auto* label = item(window->contentItem(), name + "Label");
            if (!control || !label || !label->isVisible() ||
                label->property("text").toString() != control->property("label").toString() + ":")
                return QRectF{};
            const auto left = label->mapRectToScene(label->boundingRect());
            const auto right = control->mapRectToScene(control->boundingRect());
            if (left.isEmpty() || left.left() < 0 || right.right() > window->width())
                return QRectF{};
            if (stacked ? (left.bottom() > right.top() || std::abs(left.left() - right.left()) > 1)
                        : (left.right() > right.left() || std::abs(left.center().y() - right.center().y()) > 1))
                return QRectF{};
            return right;
        };
        const auto separateRows = [&] {
            const auto language = rowBounds("languageSetting");
            const auto theme = rowBounds("themeSetting");
            const auto scale = rowBounds("scaleSetting");
            return !language.isEmpty() && !theme.isEmpty() && !scale.isEmpty() &&
                   language.bottom() < theme.top() && theme.bottom() < scale.top();
        };
        QVERIFY(QTest::qWaitFor(separateRows));
        capture(window, "settings-ru-100", "Язык интерфейса");
        russian->forceActiveFocus(Qt::TabFocusReason);
        QTest::keyClick(window, Qt::Key_Left);
        QCOMPARE(prefs->language(), "en");
        QCOMPARE(window->activeFocusItem(), english);
        QVERIFY(QTest::qWaitFor(separateRows));
        QCOMPARE(item(window->contentItem(), "languageSettingLabel")->property("text").toString(),
                 "Interface language:");
        capture(window, "settings-en-100", "Interface language");
        QTest::keyClick(window, Qt::Key_Right);
        QCOMPARE(prefs->language(), "ru");
        QCOMPARE(window->activeFocusItem(), russian);

        QTest::keyClick(window, Qt::Key_Tab);
        QCOMPARE(window->activeFocusItem(), item(window->contentItem(), "themeSetting-0"));
        auto* dark = item(window->contentItem(), "themeSetting-2");
        QVERIFY(dark);
        QTest::mouseClick(window, Qt::LeftButton, {}, dark->mapToScene(dark->boundingRect().center()).toPoint());
        QCOMPARE(prefs->theme(), "dark");
        dark->forceActiveFocus(Qt::TabFocusReason);
        QTest::keyClick(window, Qt::Key_Tab);
        QCOMPARE(window->activeFocusItem(), item(window->contentItem(), "scaleSetting-0"));
        QTest::keyClick(window, Qt::Key_End);
        QCOMPARE(prefs->textScale(), 2.);
        const auto originalSize = window->size();
        window->resize(420, 820);
        auto* theme = item(window->contentItem(), "themeSetting");
        QVERIFY(theme);
        QVERIFY(QTest::qWaitFor([&] {
            return !theme->property("vertical").toBool() && !rowBounds("languageSetting", true).isEmpty() &&
                   !rowBounds("themeSetting", true).isEmpty() && !rowBounds("scaleSetting", true).isEmpty();
        }));
        capture(window, "settings-narrow-ru", "Размер текста");
        auto* disclosure = item(window->contentItem(), "shortcutDisclosure");
        QVERIFY(disclosure && !disclosure->property("checked").toBool());
        disclosure->forceActiveFocus(Qt::TabFocusReason);
        QTest::keyClick(window, Qt::Key_Space);
        auto* editor = item(window->contentItem(), "shortcut-sign-out");
        QVERIFY(editor && editor->isVisible());
        editor->forceActiveFocus(Qt::TabFocusReason);
        QVERIFY(QTest::qWaitFor([&] {
            const auto bounds = editor->mapRectToScene(editor->boundingRect());
            return bounds.width() > 0 && bounds.left() >= 0 && bounds.right() <= window->width() &&
                   bounds.top() >= 0 && bounds.bottom() <= window->height();
        }));
        disclosure->forceActiveFocus(Qt::BacktabFocusReason);
        QTest::keyClick(window, Qt::Key_Space);
        item(window->contentItem(), "scaleSetting-3")->forceActiveFocus(Qt::TabFocusReason);
        window->resize(originalSize);
        QVERIFY(QTest::qWaitFor([&] { return !theme->property("vertical").toBool(); }));
        QTest::keyClick(window, Qt::Key_Left);
        QCOMPARE(prefs->textScale(), 1.5);
        for (int i = 0; i < 4; ++i) {
            auto* segment = item(window->contentItem(), "scaleSetting-" + QString::number(i));
            QVERIFY(segment);
            auto* accessible = QAccessible::queryAccessibleInterface(segment);
            QVERIFY(accessible);
            QCOMPARE(segment->property("display").toInt(), int(Qt::ToolButtonTextOnly));
            QCOMPARE(accessible->role(), QAccessible::RadioButton);
            QCOMPARE(bool(accessible->state().checked), i == 2);
        }
        auto* label = dark->property("contentItem").value<QQuickItem*>();
        QVERIFY(label);
        QVERIFY(QTest::qWaitFor([&] { return label->width() >= label->implicitWidth(); }));
        capture(window, "settings-ru", "Размер текста");
        auto* back = item(window->contentItem(), "back");
        QTest::mouseClick(window, Qt::LeftButton, {}, back->mapToScene(back->boundingRect().center()).toPoint());
        QVERIFY(QTest::qWaitFor([&] { return window->title().startsWith("Вход на площадку"); }));
        QCOMPARE(window->activeFocusItem(), settings);
        QTest::keyClick(window, Qt::Key_N, Qt::ControlModifier);
        QVERIFY(QTest::qWaitFor([&] { return QGuiApplication::topLevelWindows().count() == 2; }));
        auto others = QGuiApplication::topLevelWindows();
        others.removeAll(window);
        QPointer<QWindow> second = others.first();
        QVERIFY(QTest::qWaitForWindowExposed(second));
        second->requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(second));
        QTest::keyClick(second, Qt::Key_W, Qt::ControlModifier);
        QVERIFY(QTest::qWaitFor([&] { return second.isNull(); }));
        QVERIFY(window->isVisible());
    }

    void catalogAndPlayback()
    {
        WindowGroup group({}, true);
        auto* prefs = group.engine().singletonInstance<Preferences*>("Bacuri.Vms.Client.Settings", "Preferences");
        QCOMPARE(prefs->language(), "ru");
        QCOMPARE(prefs->theme(), "dark");
        QCOMPARE(prefs->textScale(), 1.5);
        prefs->setTextScale(1);
        auto* videoWindow = group.open();
        auto* formWindow = group.open();
        QVERIFY(videoWindow && formWindow);
        QVERIFY(QTest::qWaitForWindowExposed(videoWindow));
        QVERIFY(QTest::qWaitForWindowExposed(formWindow));
        formWindow->requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(formWindow));
        auto* form = item(formWindow->contentItem(), "filter");
        QVERIFY(form);
        auto* results = item(formWindow->contentItem(), "pointList");
        auto* disclosure = item(formWindow->contentItem(), "filterOptions");
        auto* connection = item(formWindow->contentItem(), "filter-connection");
        auto* recording = item(formWindow->contentItem(), "filter-recording");
        auto* reset = item(formWindow->contentItem(), "filterReset");
        QVERIFY(results && disclosure && connection && recording && reset);
        QCOMPARE(results->property("count").toInt(), 3);
        QVERIFY(!connection->isVisible() && !reset->isVisible());
        disclosure->forceActiveFocus(Qt::TabFocusReason);
        QTest::keyClick(formWindow, Qt::Key_Space);
        selectOption(formWindow, connection, 1);
        QCOMPARE(results->property("count").toInt(), 2);
        selectOption(formWindow, recording, 2);
        QCOMPARE(results->property("count").toInt(), 1);
        capture(formWindow, "point-filters-ru", "Freight yard");
        const auto formSize = formWindow->size();
        formWindow->resize(420, 820);
        prefs->setTextScale(2);
        QVERIFY2(QTest::qWaitFor([&] {
                     const auto upper = connection->mapRectToScene(connection->boundingRect());
                     const auto lower = recording->mapRectToScene(recording->boundingRect());
                     return upper.bottom() < lower.top() && lower.left() >= 0 &&
                            lower.right() <= formWindow->width();
                 }),
                 qPrintable(QString("connection %1,%2 %3x%4 recording %5,%6 %7x%8 window %9")
                                .arg(connection->mapToScene({}).x())
                                .arg(connection->mapToScene({}).y())
                                .arg(connection->width())
                                .arg(connection->height())
                                .arg(recording->mapToScene({}).x())
                                .arg(recording->mapToScene({}).y())
                                .arg(recording->width())
                                .arg(recording->height())
                                .arg(formWindow->width())));
        capture(formWindow, "point-filters-narrow-ru", "Состояние записи");
        prefs->setTextScale(1);
        formWindow->resize(formSize);
        prefs->setLanguage("en");
        QCOMPARE(connection->property("currentValue").toString(), "online");
        QCOMPARE(recording->property("displayText").toString(), "Recording delayed");
        prefs->setLanguage("ru");
        results->forceActiveFocus(Qt::TabFocusReason);
        QTest::keyClick(formWindow, Qt::Key_Return);
        capture(formWindow, "filtered-point-ru", "Freight yard");
        QTest::keyClick(formWindow, Qt::Key_Escape);
        QCOMPARE(results->property("currentIndex").toInt(), 0);
        disclosure->forceActiveFocus(Qt::TabFocusReason);
        QTest::keyClick(formWindow, Qt::Key_Space);
        QVERIFY(!recording->isVisible());
        QCOMPARE(results->property("count").toInt(), 1);
        QTest::keyClick(formWindow, Qt::Key_F, Qt::ControlModifier);
        QCOMPARE(formWindow->activeFocusItem(), form);
        QTest::keyClick(formWindow, Qt::Key_X);
        QCOMPARE(results->property("count").toInt(), 0);
        capture(formWindow, "no-filter-results-ru", "Нет точек наблюдения, подходящих под фильтры.");
        reset->forceActiveFocus(Qt::TabFocusReason);
        QTest::keyClick(formWindow, Qt::Key_Space);
        QCOMPARE(formWindow->activeFocusItem(), form);
        QCOMPARE(form->property("text").toString(), "");
        QCOMPARE(results->property("count").toInt(), 3);
        form->forceActiveFocus(Qt::TabFocusReason);
        for (auto key : {Qt::Key_W, Qt::Key_E, Qt::Key_S, Qt::Key_T})
            QTest::keyClick(formWindow, key);
        QCOMPARE(form->property("text").toString(), "west");
        auto* list = item(videoWindow->contentItem(), "pointList");
        QVERIFY(list);
        videoWindow->requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(videoWindow));
        list->forceActiveFocus(Qt::TabFocusReason);
        QTest::keyClick(videoWindow, Qt::Key_Return);
        QVERIFY(QTest::qWaitFor([&] { return videoWindow->title().startsWith("Точка наблюдения"); }));

        {
            auto stream = std::make_unique<VideoStream>(
                0, StreamOptions{qEnvironmentVariable("BACURI_SHELL_VIDEO"), "h264", DecoderChoice::software});
            stream->create();
            QVERIFY2(stream->error().isEmpty(), qPrintable(stream->error()));
            QQmlComponent media(&group.engine());
            media.setData("import QtQuick\nimport org.freedesktop.gstreamer.Qt6GLVideoItem\nGstGLQt6VideoItem { "
                          "anchors.fill: parent }",
                          {});
            std::unique_ptr<QQuickItem> surface(qobject_cast<QQuickItem*>(media.create()));
            QVERIFY2(surface, qPrintable(media.errorString()));
            surface->setParentItem(item(videoWindow->contentItem(), "mediaSurface"));
            stream->attach(surface.get(), videoWindow);
            StreamGroup playback(videoWindow, 40'000'000);
            auto& cell = playback.add(std::move(stream));
            playback.start();
            QVERIFY(QTest::qWaitFor([&] { return cell.last_pts().value_or(0) > 0; }, 10000));
            playback.seek_to_frame(100);
            QVERIFY(QTest::qWaitFor([&] { return cell.last_pts().value_or(0) >= 4'000'000'000; }, 5000));

            QDBusInterface portal("org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop",
                                  "org.bacuri.TestSettings");
            QVERIFY(portal.isValid());
            prefs->setTheme("system");
            auto* settings = item(formWindow->contentItem(), "settings");
            QVERIFY(settings);
            formWindow->requestActivate();
            QVERIFY(QTest::qWaitForWindowActive(formWindow));
            for (const auto& language : {QString("en"), QString("ru")}) {
                const auto before = cell.last_pts().value();
                const bool russian = language == "ru";
                settings->forceActiveFocus(Qt::TabFocusReason);
                QTest::keyClick(formWindow, Qt::Key_Space);
                auto* toggle =
                    item(formWindow->contentItem(), russian ? "languageSetting-0" : "languageSetting-1");
                QVERIFY(toggle && toggle->isVisible());
                toggle->forceActiveFocus(Qt::TabFocusReason);
                QTest::keyClick(formWindow, russian ? Qt::Key_Right : Qt::Key_Left);
                QCOMPARE(prefs->language(), language);
                QCOMPARE(formWindow->activeFocusItem(),
                         item(formWindow->contentItem(), russian ? "languageSetting-1" : "languageSetting-0"));
                portal.call("Set", "color-scheme", uint(russian ? 1 : 2));
                portal.call("Set", "reduced-motion", uint(russian));
                QVERIFY(QTest::qWaitFor([&] { return prefs->dark() == russian; }));
                QVERIFY(QTest::qWaitFor([&] { return prefs->motion() == !russian; }));
                QCOMPARE(videoWindow->color(), QColor(russian ? "#1e1e1e" : "#f7f4ed"));
                QCOMPARE(formWindow->color(), videoWindow->color());
                QTest::keyClick(formWindow, Qt::Key_Escape);
                QVERIFY(form->isVisible());
                QCOMPARE(formWindow->activeFocusItem(), settings);
                QCOMPARE(form->property("text").toString(), "west");
                QVERIFY(cell.last_pts().value_or(0) >= before);
                QVERIFY(QTest::qWaitFor([&] { return cell.last_pts().value_or(0) > before; }, 1000));
                capture(videoWindow, "playback-" + language, russian ? "Тревога" : "Alarm");
                capture(formWindow, "form-" + language,
                        russian ? "Поиск по названию или ID" : "Search by name or ID");
            }

            QThreadPool::globalInstance()->waitForDone();
        }
        videoWindow->requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(videoWindow));
        QTest::keyClick(videoWindow, Qt::Key_Escape);
        QVERIFY(QTest::qWaitFor([&] { return videoWindow->title().startsWith("Каталог"); }));
        QCOMPARE(list->property("currentIndex").toInt(), 0);
        QVERIFY(list->hasActiveFocus());
        auto* catalog = videoWindow->property("workspace").value<Workspace*>();
        QVERIFY(catalog);
        for (const auto& language : {QString("en"), QString("ru")}) {
            prefs->setLanguage(language);
            const QStringList expected =
                language == "ru"
                    ? QStringList{"Точки наблюдения",           "Загрузка точек",   "Нет доступных",
                                  "Служба площадки недоступна", "Точки наблюдения", "Недостаточно прав"}
                    : QStringList{"Observation points",    "Loading observation points",
                                  "No observation points", "A site service is unavailable",
                                  "Observation points",    "You do not have permission"};
            for (const auto& theme : {QString("light"), QString("dark")}) {
                prefs->setTheme(theme);
                for (int i = 0; i < expected.size(); ++i) {
                    if (i == 4) {
                        auto* retry = item(videoWindow->contentItem(), "stateAction");
                        QVERIFY(retry && retry->isVisible());
                        retry->forceActiveFocus(Qt::TabFocusReason);
                        QTest::keyClick(videoWindow, Qt::Key_Space);
                    } else {
                        selectOption(videoWindow, item(videoWindow->contentItem(), "catalogScenario"), i);
                    }
                    QCOMPARE(catalog->property("scenario").toInt(), i);
                    capture(videoWindow, language + "-" + theme + "-state-" + QString::number(i), expected[i]);
                }
            }
        }
        selectOption(videoWindow, item(videoWindow->contentItem(), "catalogScenario"), 4);
        prefs->setTextScale(2);
        videoWindow->resize(520, 700);
        capture(videoWindow, "large-text", "Восстановлено");
    }

    void displayMetrics()
    {
        WindowGroup group({}, false);
        auto* window = group.open();
        QVERIFY(window && QTest::qWaitForWindowExposed(window));
        auto* prefs = group.engine().singletonInstance<Preferences*>("Bacuri.Vms.Client.Settings", "Preferences");
        prefs->setTextScale(1);
        auto* field = item(window->contentItem(), "origin");
        QVERIFY(field);
        auto* tokens = group.engine().singletonInstance<QObject*>("Bacuri.Vms.Client.Ui", "Tokens");
        const auto font = field->property("font").value<QFont>();
        const auto dpr = window->devicePixelRatio();
        const QJsonObject metrics{{"devicePixelRatio", dpr},
                                  {"logicalDpi", window->screen()->logicalDotsPerInchY()},
                                  {"systemTextScale", prefs->systemTextScale()},
                                  {"bodyToken", tokens->property("textBody").toDouble()},
                                  {"logicalFontPixels", font.pixelSize()},
                                  {"physicalFontPixels", dpr * font.pixelSize()}};
        QFile output(QDir(qEnvironmentVariable("BACURI_SHELL_REPORT")).filePath("display-metrics.json"));
        QVERIFY(output.open(QIODevice::WriteOnly));
        QVERIFY(output.write(QJsonDocument(metrics).toJson()) > 0);
    }
    void mixedDpi()
    {
        const auto screens = QGuiApplication::screens();
        QCOMPARE(screens.size(), 2);
        WindowGroup group({}, false);
        auto* window = group.open();
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto* origin = item(window->contentItem(), "origin");
        QVERIFY(origin);
        origin->setProperty("text", "https://workstation.example");
        const auto before = window->devicePixelRatio();
        capture(window, "first-screen", "Site address");
        auto* target = screens[0] == window->screen() ? screens[1] : screens[0];
        auto* commands = window->findChild<WindowCommands*>("commandRegistry");
        QVERIFY(commands);
        commands->choose("screen/" + QString::number(screens.indexOf(target) + 1));
        window->setPosition(target->geometry().topLeft() + QPoint(40, 40));
        QVERIFY(QTest::qWaitFor([&] { return window->screen() == target; }));
        QVERIFY(QTest::qWaitFor([&] { return window->devicePixelRatio() != before; }));
        QCOMPARE(origin->property("text").toString(), "https://workstation.example");
        capture(window, "second-screen", "Site address");
        commands->invoke("undo");
        QVERIFY(
            QTest::qWaitFor([&] { return window->screen() != target && window->devicePixelRatio() == before; }));
        WindowGroup examples({}, true);
        auto* monitor = examples.open();
        QVERIFY(monitor && QTest::qWaitForWindowExposed(monitor));
        auto* layoutCommands = monitor->findChild<WindowCommands*>("commandRegistry");
        auto* nav = monitor->property("navigation").value<Navigation*>();
        const auto initialScreen = monitor->screen();
        QVERIFY(layoutCommands && nav);
        QVERIFY(layoutCommands->temporaryLayout({"point/north", "point/west", "point/yard"}, 2));
        QCOMPARE(monitor->screen(), screens[1]);
        QCOMPARE(nav->page(), Navigation::Layout);
        QCOMPARE(layoutCommands->layout()["data"].toMap()["points"].toList().size(), 3);
        layoutCommands->invoke("undo");
        QCOMPARE(monitor->screen(), initialScreen);
        QCOMPARE(nav->page(), Navigation::Overview);
    }
};
} // namespace

int main(int argc, char** argv)
{
    gst_init(&argc, &argv);
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
    QGuiApplication app(argc, argv);
    app.styleHints()->setCursorFlashTime(0);
    app.setOrganizationName("bacuri.vms");
    app.setApplicationName("bacuri-vms-client");
    QAccessible::setActive(true);
    ShellTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "shell_test.moc"
