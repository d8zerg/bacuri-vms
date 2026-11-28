#include "app/navigation.hpp"
#include "app/window_group.hpp"
#include "media/stream_group.hpp"
#include "settings/preferences.hpp"

#include <QAccessible>
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

void capture(QQuickWindow* window, const QString& name, const QString& expected)
{
    QTest::mouseMove(window, QPoint(1, 1));
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
        QTest::failOnWarning(QRegularExpression("qrc:|QQml|Binding loop|Untrusted OpenGL"));
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
        window->setScreen(target);
        window->setPosition(target->geometry().topLeft() + QPoint(40, 40));
        QVERIFY(QTest::qWaitFor([&] { return window->screen() == target; }));
        QVERIFY(QTest::qWaitFor([&] { return window->devicePixelRatio() != before; }));
        QCOMPARE(origin->property("text").toString(), "https://workstation.example");
        capture(window, "second-screen", "Site address");
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
