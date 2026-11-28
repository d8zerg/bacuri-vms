#include "app/window_commands.hpp"
#include <QCoreApplication>
#include "commands/command_time.hpp"
#include <QGuiApplication>
#include <QScreen>
#include "media/native_frame.hpp"
#include <QSettings>
#include <algorithm>
#include <array>
#include <cmath>
#include <QtMath>

namespace bacuri::vms::client
{
namespace
{
using K = QKeySequence;
struct Sector {
    const char* id;
    int degrees;
    const char* description;
};
constexpr std::array sectors{Sector{"snapshot", -90, "Save the current frame"},
                             Sector{"expand", -45, "Show camera full screen"},
                             Sector{"archive", 0, "Open the camera archive"},
                             Sector{"export", 45, "Export a recording interval"},
                             Sector{"bookmark", 90, "Mark this moment for this window"},
                             Sector{"ptz", 180, "Control the camera"}};
QString label(const char* text)
{
    return QCoreApplication::translate("Shell", text);
}
QString groupName(const QString& kind)
{
    if (kind == "point")
        return label("Camera");
    if (kind == "case")
        return label("Case");
    if (kind == "layout")
        return label("Layout");
    if (kind == "alert")
        return label("Alarm");
    if (kind == "screen")
        return label("Screen");
    if (kind == "bookmark")
        return label("Temporary mark (this window)");
    return label("Action");
}
QString bindingError(const Command& command, const K& key)
{
    if (key.count() != 1 || key.isEmpty() || key[0].key() == Qt::Key_unknown)
        return label("Enter a keyboard shortcut.");
    const auto combo = key[0];
    if (combo.keyboardModifiers().testFlag(Qt::MetaModifier) || key == K("Alt+Tab") || key == K("Ctrl+Alt+T") ||
        key == K("Alt+Shift+Tab"))
        return label("Reserved by the desktop.");
    if (command.scope == CommandScope::Global &&
        !(combo.keyboardModifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) &&
        !(combo.key() >= Qt::Key_F1 && combo.key() <= Qt::Key_F35) && combo.key() != Qt::Key_Escape &&
        combo.key() != Qt::Key_Menu)
        return label("Use a modifier or a function key.");
    for (const auto standard : {K::Copy, K::Paste, K::Cut, K::Undo, K::Redo, K::SelectAll})
        if (K::keyBindings(standard).contains(key) && !(command.id == "undo" && standard == K::Undo))
            return label("Reserved for text editing.");
    return {};
}
bool overlaps(CommandScope a, CommandScope b)
{
    return a == b || a == CommandScope::Global || b == CommandScope::Global;
}
} // namespace

void installShellCommands(CommandRegistry&);
void WindowCommands::registerCommands(CommandRegistry& registry)
{
    const auto context = [](const WindowCommands& w) -> WindowContext& { return const_cast<WindowCommands&>(w); };
    installShellCommands(registry);
    addCameraCommand(registry, context, "context", "Camera actions", "ellipsis", {K("Shift+F10"), K(Qt::Key_Menu)},
                     ParameterType::None, [](WindowCommands& w) { emit w.triggered("context"); });
    addCameraCommand(registry, context, "expand", "Full screen", "expand", {K("F11")}, ParameterType::None,
                     [](WindowCommands& w) {
                         w.saveView();
                         if (w.window_->visibility() == QWindow::FullScreen)
                             w.window_->setVisibility(w.previousVisibility_);
                         else {
                             w.previousVisibility_ = w.window_->visibility();
                             w.window_->showFullScreen();
                         }
                     });
    LiveCommands::install(registry, context, [](const WindowCommands& w) -> LiveCommands& { return w.live_; });
    ArchiveCommands::install(registry, context,
                             [](const WindowCommands& w) -> ArchiveCommands& { return w.archive_; });
    installExportCommands(registry, context,
                          [](const WindowCommands& w) -> ExportJobs& { return *w.workspace_->exports(); });
}
QString WindowCommands::focusedCamera() const
{
    return navigation_ && navigation_->page() == Navigation::Point && workspace_->ready() && workspace_->signedIn()
               ? navigation_->pointId()
               : QString{};
}
QQuickItem* WindowCommands::frameFor(const QVariantMap& point) const
{
    return framePoint_ == point.value("id") && framePoint_ == focusedCamera() &&
                   frameRevision_ == point.value("access_revision")
               ? frame_.data()
               : nullptr;
}
void WindowCommands::stopMotion()
{
    motionKey_ = 0;
    live_.stopMotion();
}
void WindowCommands::attach(QQuickWindow* window, Navigation* navigation, Workspace* workspace)
{
    if (!window || !navigation || !workspace || workspace_)
        return;
    window_ = window;
    navigation_ = navigation;
    workspace_ = workspace;
    catalog_ = workspace->registry();
    window->installEventFilter(this);
    connect(&archive_, &ArchiveCommands::timeChanged, this, &WindowCommands::archiveTimeChanged);
    connect(&archive_, &ArchiveCommands::marksChanged, this, &WindowCommands::resultsChanged);
    connect(&archive_, &ArchiveCommands::loaded, this, &WindowCommands::archiveLoaded);
    connect(&live_, &LiveCommands::changed, this, &WindowCommands::update);
    connect(&live_, &LiveCommands::motionStopped, this, [this] { motionKey_ = 0; });
    ctrlTimer_.setSingleShot(true);
    connect(&ctrlTimer_, &QTimer::timeout, this, [this] {
        if (!modal_)
            setHints(true);
    });
    for (const auto& cmd : catalog_->commands()) {
        auto* shortcut = new QShortcut(window);
        shortcut->setAutoRepeat(false);
        shortcuts_.insert(cmd.id, shortcut);
        connect(shortcut, &QShortcut::activated, this, [this, id = QString(cmd.id)] { invoke(id); });
    }
    connect(qGuiApp, &QGuiApplication::screenAdded, this, &WindowCommands::resultsChanged);
    connect(qGuiApp, &QGuiApplication::screenRemoved, this, &WindowCommands::resultsChanged);
    connect(catalog_, &CommandRegistry::bindingsChanged, this, [this](const QString& profile) {
        if (profile == profile_)
            syncBindings();
    });
    connect(workspace, &Workspace::leavingReady, this, &WindowCommands::stopMotion);
    connect(workspace, &Workspace::changed, this, [this] {
        if (workspace_->state() != Workspace::Ready) {
            stopMotion();
            archive_.clear();
            emit tool("dismiss", {});
            if (!workspace_->signedIn() || workspace_->state() == Workspace::SigningOut) {
                archive_.clearMarks();
                history_.clear();
                temporaryLayout_.clear();
                live_.cancelCommands();
            }
        }
        if (workspace_->state() == Workspace::Ready && navigation_->page() == Navigation::Layout)
            workspace_->loadCommandObjects();
        update();
    });
    connect(workspace, &Workspace::layoutsChanged, this, &WindowCommands::layoutChanged);
    connect(workspace, &Workspace::commandObjectsChanged, this, &WindowCommands::resultsChanged);
    connect(workspace->exports(), &ExportJobs::feedback, this, &WindowCommands::feedback);
    connect(workspace, &Workspace::commandIndexFailed, this, &WindowCommands::feedback);
    connect(workspace->points(), &PointModel::revised, this, &WindowCommands::update);
    connect(navigation, &Navigation::changed, this, [this] {
        focusedAlert_.clear();
        stopMotion();
        archive_.clear();
        emit layoutChanged();
        if (navigation_->page() == Navigation::Layout && layout().isEmpty())
            workspace_->loadCommandObjects();
        update();
    });
    connect(window, &QQuickWindow::activeFocusItemChanged, this, [this] {
        if (motionKey_)
            stopMotion();
        updateShortcuts();
    });
    connect(window, &QWindow::activeChanged, this, [this, window] {
        if (!window->isActive())
            stopMotion();
    });
    connect(this, &WindowCommands::modalChanged, this, &WindowCommands::updateShortcuts);
    connect(this, &WindowCommands::archiveTimeChanged, this, &WindowCommands::update);
    connect(this, &WindowCommands::frameChanged, this, &WindowCommands::update);
    if (auto* engine = qmlEngine(this))
        connect(engine, &QQmlEngine::uiLanguageChanged, this, &WindowCommands::update);
    syncBindings();
}
void WindowCommands::syncBindings()
{
    profile_ = workspace_->profile(navigation_->pointId());
    const auto bindings = catalog_->bindings(profile_);
    const auto defaults = workspace_->siteRole(navigation_->pointId()).value("keymap").toMap();
    customized_.clear();
    QHash<QString, QList<K>> accepted;
    const auto assign = [&](const Command& cmd, const K& key) {
        if (!bindingError(cmd, key).isEmpty())
            return false;
        for (const auto& other : catalog_->commands())
            if (other.id != cmd.id && overlaps(cmd.scope, other.scope) && accepted.value(other.id).contains(key))
                return false;
        accepted[cmd.id].append(key);
        return true;
    };
    // Personal remaps take precedence when the administrator changes the site defaults.
    for (const auto& cmd : catalog_->commands())
        if (bindings.contains(cmd.id) &&
            assign(cmd, K::fromString(bindings.value(cmd.id).toString(), K::PortableText)))
            customized_.insert(cmd.id);
    for (const auto& cmd : catalog_->commands())
        if (!accepted.contains(cmd.id) && defaults.contains(cmd.id))
            assign(cmd, K::fromString(defaults.value(cmd.id).toString(), K::PortableText));
    for (const auto& cmd : catalog_->commands()) {
        if (!accepted.contains(cmd.id))
            for (const auto& key : cmd.defaults)
                assign(cmd, key);
        shortcuts_[cmd.id]->setKeys(accepted.value(cmd.id));
    }
    update();
}
QString WindowCommands::keyName(int key, int modifiers) const
{
    if (key == Qt::Key_unknown || key == Qt::Key_Control || key == Qt::Key_Alt || key == Qt::Key_Meta ||
        key == Qt::Key_Shift)
        return {};
    if (key >= 0x0400 && key <= 0x04ff &&
        (Qt::KeyboardModifiers(modifiers) & (Qt::ControlModifier | Qt::AltModifier))) {
        const auto latin = otherLayout(QString(QChar(key)).toLower()).toUpper();
        if (latin.size() == 1 && latin.front().unicode() < 128)
            key = latin.front().unicode();
    }
    return K(QKeyCombination(Qt::KeyboardModifiers(modifiers), Qt::Key(key))).toString(K::PortableText);
}
QString WindowCommands::setBinding(const QString& id, const QString& sequence, bool restoreDefault)
{
    const auto* it = catalog_ ? catalog_->find(id) : nullptr;
    if (!it || !workspace_)
        return label("Unknown command.");
    const auto key = K::fromString(sequence, K::PortableText);
    if (!restoreDefault) {
        const auto error = bindingError(*it, key);
        if (!error.isEmpty())
            return error;
    }
    const auto siteDefault = workspace_->siteRole(navigation_->pointId()).value("keymap").toMap().value(id);
    const auto defaults =
        siteDefault.isValid() ? QList<K>{K::fromString(siteDefault.toString(), K::PortableText)} : it->defaults;
    const auto keys = restoreDefault ? defaults : QList<K>{key};
    for (const auto& other : catalog_->commands()) {
        if (id == other.id || !overlaps(it->scope, other.scope))
            continue;
        for (const auto& proposed : keys)
            if (shortcuts_.value(other.id)->keys().contains(proposed))
                return label("Already assigned to %1.").arg(label(other.name));
    }
    catalog_->bind(profile_, id, restoreDefault || keys == defaults ? QString{} : key.toString(K::PortableText));
    return {};
}
bool WindowCommands::pointAvailable(const QString& id, const QVariantMap& point) const
{
    return navigation_ && navigation_->pointId() == point.value("id").toString() && !point.isEmpty() &&
           available(id);
}
bool WindowCommands::available(const QString& id) const
{
    const auto* descriptor = catalog_ ? catalog_->find(id) : nullptr;
    return descriptor && descriptor->available(*this);
}
CommandScope WindowCommands::focusedScope() const
{
    for (auto* item = window_->activeFocusItem(); item; item = item->parentItem()) {
        if (item->flags().testFlag(QQuickItem::ItemAcceptsInputMethod))
            return CommandScope::Text;
        const auto scope = item->property("commandScope").toString();
        if (scope == "timeline")
            return CommandScope::Timeline;
        if (scope == "video")
            return CommandScope::Video;
    }
    return CommandScope::Global;
}
void WindowCommands::updateShortcuts()
{
    if (!window_ || !catalog_)
        return;
    const auto scope = focusedScope();
    for (const auto& cmd : catalog_->commands()) {
        const auto* shortcut = shortcuts_.value(cmd.id);
        // Modified camera commands are also available from the camera page toolbar.
        const auto keys = shortcut->keys();
        const bool pageCommand =
            scope == CommandScope::Global && cmd.scope == CommandScope::Video &&
            navigation_->page() == Navigation::Point && std::ranges::all_of(keys, [](const K& key) {
                return !key.isEmpty() && (key[0].keyboardModifiers() != Qt::NoModifier ||
                                          (key[0].key() >= Qt::Key_F1 && key[0].key() <= Qt::Key_F35) ||
                                          key[0].key() == Qt::Key_Menu);
            });
        shortcuts_[cmd.id]->setEnabled(!cmd.hold && !modal_ &&
                                       (cmd.scope == CommandScope::Global || scope == cmd.scope || pageCommand) &&
                                       !(scope == CommandScope::Text && cmd.id == "undo") && available(cmd.id));
    }
}
void WindowCommands::update()
{
    if (workspace_ && profile_ != workspace_->profile(navigation_->pointId())) {
        syncBindings();
        return;
    }
    live_.readCameraControls();
    updateShortcuts();
    emit changed();
    emit resultsChanged();
}
void WindowCommands::remember(const QString& key, const QString& site)
{
    catalog_->remember(site.isEmpty() ? profile_ : workspace_->profileForSite(site), key);
}
void WindowCommands::openPalette()
{
    emit tool("search", {});
    if (workspace_)
        workspace_->loadCommandObjects();
}
void WindowCommands::invoke(const QString& id)
{
    if (!available(id) || catalog_->find(id)->hold)
        return;
    remember("command/" + id);
    catalog_->find(id)->run(*this);
}
QStringList WindowCommands::commandIds() const
{
    QStringList ids;
    if (!catalog_)
        return ids;
    for (const auto& cmd : catalog_->commands())
        ids.append(cmd.id);
    return ids;
}
QVariantMap WindowCommands::entries() const
{
    QVariantMap result;
    if (!catalog_)
        return result;
    for (const auto& cmd : catalog_->commands()) {
        QStringList bindings;
        if (auto* shortcut = shortcuts_.value(cmd.id))
            for (const auto& key : shortcut->keys())
                if (!key.isEmpty())
                    bindings.append(key.toString(K::PortableText));
        result.insert(cmd.id, QVariantMap{{"id", cmd.id},
                                          {"name", label(cmd.name)},
                                          {"icon", cmd.icon},
                                          {"bindings", bindings},
                                          {"customized", customized_.contains(cmd.id)},
                                          {"available", available(cmd.id)}});
    }
    return result;
}
QVariantList WindowCommands::radial() const
{
    if (!catalog_)
        return {};
    const auto entries = this->entries();
    QVariantList result;
    for (std::size_t i = 0; i < sectors.size(); ++i) {
        const auto& sector = sectors[i];
        const int previous = i == 0 ? sectors.back().degrees - 360 : sectors[i - 1].degrees;
        const int next = i + 1 == sectors.size() ? sectors.front().degrees + 360 : sectors[i + 1].degrees;
        auto entry = entries.value(sector.id).toMap();
        // The visual boundaries bisect adjacent gesture directions.
        entry["degrees"] = sector.degrees;
        entry["startAngle"] = (previous + sector.degrees) / 2.0;
        entry["endAngle"] = (sector.degrees + next) / 2.0;
        entry["description"] = label(sector.description);
        result.append(entry);
    }
    return result;
}
QString WindowCommands::direction(double x, double y) const
{
    if (std::hypot(x, y) < 24)
        return {};
    const double angle = qRadiansToDegrees(std::atan2(y, x));
    auto nearest = std::ranges::min_element(
        sectors, {}, [angle](auto s) { return std::abs(std::remainder(angle - s.degrees, 360.)); });
    return nearest->id;
}
QVariantList WindowCommands::screens() const
{
    QVariantList list;
    for (auto* screen : QGuiApplication::screens()) {
        const auto number = QString::number(list.size() + 1);
        list.append(
            QVariantMap{{"value", number}, {"text", groupName("screen") + " " + number + " · " + screen->name()}});
    }
    return list;
}
QVariantList WindowCommands::search(const QString& query) const
{
    if (!catalog_)
        return {};
    const auto term = query.section('@', 0, 0).trimmed().toCaseFolded();
    PaletteQuery matcher(term);
    const auto focus = navigation_->page() == Navigation::Point ? navigation_->pointId() : QString{};
    struct Hit {
        PaletteEntry row;
        QString hint;
        int score;
        int context;
        CommandRegistry::Usage usage;
    };
    auto better = [](const Hit& a, const Hit& b) {
        auto rank = [](const Hit& h) { return std::tie(h.score, h.context, h.usage.count, h.usage.last); };
        return rank(a) != rank(b) ? rank(a) > rank(b) : a.row.key < b.row.key;
    };
    QList<Hit> hits;
    hits.reserve(51);
    QHash<QString, int> groups;
    for (const auto& kind : {"point", "alert", "layout", "case", "screen", "bookmark", "command"})
        groups.insert(kind, matcher.score(groupName(kind).toCaseFolded()));
    QHash<QString, QString> profiles;
    const auto position = QDateTime::fromString(archive_.time(), Qt::ISODateWithMs).toSecsSinceEpoch();
    auto add = [&](const PaletteEntry& row, const QString& hint = QString{}, int context = 0) {
        int score = matcher.score(row.folded);
        if (score < 0)
            score = row.id.contains(term, Qt::CaseInsensitive) || groups.value(row.kind, -1) >= 0 ? 0 : -1;
        if (score < 0)
            return;
        if (!term.isEmpty() && row.folded == term)
            score += 100;
        auto profile = profile_;
        if (!row.site.isEmpty()) {
            auto it = profiles.constFind(row.site);
            if (it == profiles.cend())
                it = profiles.insert(row.site, workspace_->profileForSite(row.site));
            profile = it.value();
        }
        const auto usage = catalog_->usage(profile, row.key);
        if (hits.size() == 50) {
            const auto& last = hits.front();
            const auto rank = std::tie(score, context, usage.count, usage.last);
            const auto threshold = std::tie(last.score, last.context, last.usage.count, last.usage.last);
            if (rank < threshold || (rank == threshold && row.key >= last.row.key))
                return;
            std::pop_heap(hits.begin(), hits.end(), better);
            hits.pop_back();
        }
        hits.append({row, hint, score, context, usage});
        std::push_heap(hits.begin(), hits.end(), better);
    };
    for (const auto& cmd : catalog_->commands())
        if (cmd.id != "palette" && !cmd.hold && available(cmd.id)) {
            const auto title = label(cmd.name);
            const auto keys = shortcuts_.value(cmd.id)->keys();
            add({"command", "command/" + cmd.id, title, cmd.icon, title.toCaseFolded(), cmd.id, {}, {}},
                keys.isEmpty() ? QString{} : keys.first().toString(K::PortableText),
                !focus.isEmpty() && cmd.scope == CommandScope::Video
                    ? (position &&
                               (cmd.parameter == ParameterType::Time || cmd.parameter == ParameterType::Interval)
                           ? 9
                           : 8)
                    : 0);
        }
    for (const auto& v : screens()) {
        const auto m = v.toMap();
        const auto title = m.value("text").toString(), id = m.value("value").toString();
        add({"screen", "screen/" + id, title, "monitor", title.toCaseFolded(), id, {}, {}});
    }
    if (workspace_->signedIn() && workspace_->state() == Workspace::Ready) {
        for (const auto& row : workspace_->index().points())
            add(row, {}, !focus.isEmpty() && row.point == focus ? 2 : 0);
        for (const auto& row : workspace_->index().objects()) {
            int boost = 0;
            if (!focus.isEmpty() && row.point == focus) {
                boost = 1;
                if (position && row.observedAt && std::abs(row.observedAt - position) <= 300)
                    boost = 4;
                if (row.key == focusedAlert_)
                    boost = 6;
            }
            add(row, {}, boost);
        }
        for (const auto& v : archive_.marks()) {
            auto m = v.toMap();
            const auto title = m["name"].toString(), id = m["id"].toString();
            if (!workspace_->points()->lookup(id).isEmpty())
                add({"bookmark", m["key"].toString(), title, "map-pin", title.toCaseFolded(), id, id,
                     workspace_->points()->lookup(id).value("site_id").toString()},
                    m["at"].toString(), id == focus ? 1 : 0);
        }
    }
    std::sort_heap(hits.begin(), hits.end(), better);
    QVariantList result;
    for (const auto& hit : hits) {
        QList<int> indices;
        matcher.score(hit.row.folded, &indices);
        QString marked;
        for (int i = 0; i < hit.row.name.size(); ++i) {
            const auto ch = QString(hit.row.name.at(i)).toHtmlEscaped();
            marked += indices.contains(i) ? "<b>" + ch + "</b>" : ch;
        }
        result.append(QVariantMap{{"key", hit.row.key},
                                  {"name", hit.row.name},
                                  {"marked", marked},
                                  {"group", groupName(hit.row.kind)},
                                  {"icon", hit.row.icon},
                                  {"hint", hit.hint}});
    }
    return result;
}
QVariantMap WindowCommands::object(const QString& key) const
{
    if (!workspace_ || workspace_->state() != Workspace::Ready || !workspace_->signedIn())
        return {};
    return workspace_->index().object(key);
}
QVariantMap WindowCommands::argument(const QString& key, const QString& query) const
{
    const auto at = query.indexOf('@');
    if (at < 0)
        return {{"valid", true}, {"value", ""}, {"hint", ""}};
    const auto parameter = query.sliced(at + 1).trimmed();
    const auto* descriptor = catalog_ && key.startsWith("command/") ? catalog_->find(key.mid(8)) : nullptr;
    const auto type = descriptor                  ? descriptor->parameter
                      : key.startsWith("point/")  ? ParameterType::Time
                      : key.startsWith("layout/") ? ParameterType::Screen
                                                  : ParameterType::None;
    const auto id = key.startsWith("point/") ? key.mid(6) : navigation_->pointId();
    const auto zone = workspace_->timeZone(id);
    const auto now = QDateTime::currentDateTimeUtc();
    QString value, hint;
    switch (type) {
    case ParameterType::Time: {
        const auto time = commandTime(parameter, zone, now);
        if (time.isValid()) {
            value = time.toUTC().toString(Qt::ISODateWithMs);
            hint = workspace_->points()->lookup(id).value("name").toString() + " · " +
                   (descriptor ? label(descriptor->name) : label("Archive")) + " · " + displayTime(time, zone);
        } else
            hint = label("Enter a valid time. For an ambiguous local time, include its UTC offset.");
        break;
    }
    case ParameterType::Interval: {
        const auto range = commandInterval(parameter, zone, now);
        if (range.start.isValid() && range.end > range.start) {
            value = range.start.toString(Qt::ISODateWithMs) + "/" + range.end.toString(Qt::ISODateWithMs);
            hint = displayTime(range.start, zone) + " / " + displayTime(range.end, zone);
        } else
            hint = label("The end must be later than the start.");
        break;
    }
    case ParameterType::Screen: {
        const int screen = parameter.toInt();
        if (screen > 0 && screen <= QGuiApplication::screens().size()) {
            value = QString::number(screen);
            hint = groupName("screen") + " " + value;
        } else
            hint = label("Layout or screen unavailable.");
        break;
    }
    case ParameterType::Preset: {
        const int number = parameter.toInt();
        if (number >= 1 && number <= presets().size()) {
            value = QString::number(number);
            hint = ptzName("preset_goto") + " · " + presets().at(number - 1).toMap().value("label").toString();
        } else
            hint = label("This action is unavailable.");
        break;
    }
    case ParameterType::None:
        hint = label("This action does not take a parameter.");
        break;
    }
    return {{"valid", !value.isEmpty()}, {"value", value}, {"hint", hint}};
}
void WindowCommands::choose(const QString& key, const QString& parameter)
{
    if (key.startsWith("command/")) {
        const auto cmd = key.mid(8);
        if (!available(cmd))
            return;
        if (parameter.isEmpty()) {
            invoke(cmd);
            return;
        }
        const auto parsed = argument(key, "@" + parameter);
        if (!parsed.value("valid").toBool()) {
            emit feedback(parsed.value("hint").toString());
            return;
        }
        const auto value = parsed.value("value").toString();
        const auto id = navigation_->pointId();
        remember(key);
        switch (catalog_->find(cmd)->parameter) {
        case ParameterType::Time:
            pointAction(cmd, id, value);
            break;
        case ParameterType::Interval: {
            const auto parts = value.split('/');
            pointAction(cmd, id, parts.first(), parts.last());
            break;
        }
        case ParameterType::Preset:
            confirmPreset(value.toInt());
            break;
        default:
            break;
        }
        return;
    }
    if (key.startsWith("screen/")) {
        auto* screen = QGuiApplication::screens().value(key.mid(7).toInt() - 1);
        if (screen && window_->screen() != screen) {
            saveView();
            window_->setScreen(screen);
            window_->setPosition(screen->availableGeometry().topLeft());
            remember(key);
        }
        return;
    }
    if (!workspace_ || workspace_->state() != Workspace::Ready || !workspace_->signedIn())
        return;
    if (key.startsWith("point/")) {
        auto id = key.mid(6);
        if (workspace_->points()->lookup(id).isEmpty())
            return;
        remember(key, workspace_->points()->lookup(id).value("site_id").toString());
        emit pointChosen(id, parameter.isEmpty());
        if (!parameter.isEmpty())
            pointAction("archive", id, parameter);
        return;
    }
    if (key.startsWith("bookmark/")) {
        for (const auto& v : archive_.marks()) {
            auto m = v.toMap();
            if (m["key"] == key && !workspace_->points()->lookup(m["id"].toString()).isEmpty()) {
                remember(key);
                emit pointChosen(m["id"].toString(), false);
                pointAction("archive", m["id"].toString(), m["at"].toString());
                return;
            }
        }
        return;
    }
    auto obj = object(key);
    if (obj.isEmpty())
        return;
    const auto data = obj["data"].toMap();
    remember(key, obj.value("site").toString());
    if (obj["kind"] == "layout") {
        if (parameter.isEmpty()) {
            obj["key"] = key;
            emit tool("layout", obj);
        } else
            applyLayout(key, parameter);
    } else if (obj["kind"] == "alert") {
        auto id = data["point_id"].toString();
        if (!workspace_->points()->lookup(id).isEmpty()) {
            emit pointChosen(id, false);
            focusedAlert_ = key;
            pointAction("archive", id, data["observed_at"].toString());
        }
    } else
        emit tool("case", obj);
}
QVariantMap WindowCommands::layout() const
{
    if (navigation_ && navigation_->page() == Navigation::Layout && navigation_->currentId() == "temporary")
        return temporaryLayout_;
    return navigation_ && navigation_->page() == Navigation::Layout ? object(navigation_->currentId())
                                                                    : QVariantMap{};
}
void WindowCommands::applyLayout(const QString& key, const QString& screen)
{
    auto obj = object(key);
    auto* target = QGuiApplication::screens().value(screen.toInt() - 1);
    if (obj["kind"] != "layout" || !target) {
        emit feedback(label("Layout or screen unavailable."));
        return;
    }
    saveView();
    if (window_->screen() != target) {
        window_->setScreen(target);
        window_->setPosition(target->availableGeometry().topLeft());
    }
    emit layoutApplied(key);
}
} // namespace bacuri::vms::client
