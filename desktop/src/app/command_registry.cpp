#include "app/command_registry.hpp"
#include <array>
#include <QCoreApplication>

namespace bacuri::vms::client
{
namespace
{
struct Command {
    const char* id;
    const char* name;
    const char* icon;
    QKeySequence::StandardKey key;
    const char* extra;
};
constexpr std::array commands{
    Command{"new", "New window", "new-window", QKeySequence::New, ""},
    Command{"close", "Close window", "circle-x", QKeySequence::Close, ""},
    Command{"back", "Back", "arrow-left", QKeySequence::Back, "Escape"},
    Command{"settings", "Settings", "settings-2", QKeySequence::Preferences, "Ctrl+,"},
    Command{"find", "Find", "search", QKeySequence::Find, ""},
    Command{"refresh", "Refresh", "rotate-cw", QKeySequence::Refresh, "F5"},
    Command{"sign-out", "Sign out", "log-out", QKeySequence::UnknownKey, "Ctrl+Shift+Q"}};
} // namespace
void CommandRegistry::attach(QQuickWindow* window, Navigation* navigation, Workspace* workspace)
{
    if (!window || !navigation || !workspace || workspace_)
        return;
    workspace_ = workspace;
    navigation_ = navigation;
    for (const auto& cmd : commands) {
        auto* shortcut = new QShortcut(window);
        auto keys = QKeySequence::keyBindings(cmd.key);
        const QKeySequence extra(QString::fromLatin1(cmd.extra));
        if (!extra.isEmpty() && !keys.contains(extra))
            keys.append(extra);
        shortcut->setKeys(keys);
        shortcuts_.insert(cmd.id, shortcut);
        connect(shortcut, &QShortcut::activated, this, [this, id = QString(cmd.id)] { invoke(id); });
    }
    connect(workspace, &Workspace::changed, this, &CommandRegistry::update);
    connect(workspace->points(), &PointModel::revised, this, &CommandRegistry::update);
    connect(navigation, &Navigation::changed, this, &CommandRegistry::update);
    if (auto* engine = qmlEngine(this))
        connect(engine, &QQmlEngine::uiLanguageChanged, this, &CommandRegistry::changed);
    update();
}
bool CommandRegistry::available(const QString& id) const
{
    if (!workspace_)
        return false;
    const bool overview = navigation_->page() == Navigation::Overview;
    if (id == "back")
        return !overview;
    if (id == "find")
        return overview && workspace_->signedIn() && workspace_->points()->count() > 0;
    if (id == "refresh" || id == "sign-out")
        return overview && workspace_->signedIn() && workspace_->sessionActions() &&
               workspace_->state() != Workspace::SigningOut &&
               (id != "refresh" || workspace_->state() != Workspace::Loading);
    return shortcuts_.contains(id);
}
void CommandRegistry::update()
{
    for (auto it = shortcuts_.cbegin(); it != shortcuts_.cend(); ++it)
        it.value()->setEnabled(available(it.key()));
    emit changed();
}
void CommandRegistry::invoke(const QString& id)
{
    if (!available(id))
        return;
    if (id == "refresh")
        workspace_->refresh();
    else if (id == "sign-out")
        workspace_->signOut();
    else
        emit triggered(id);
}
QVariantMap CommandRegistry::entries() const
{
    QVariantMap result;
    for (const auto& cmd : commands) {
        QStringList bindings;
        if (auto* shortcut = shortcuts_.value(cmd.id))
            for (const auto& key : shortcut->keys())
                bindings.append(key.toString(QKeySequence::PortableText));
        result.insert(cmd.id, QVariantMap{{"id", cmd.id},
                                          {"name", QCoreApplication::translate("Shell", cmd.name)},
                                          {"icon", cmd.icon},
                                          {"bindings", bindings},
                                          {"available", available(cmd.id)}});
    }
    return result;
}
} // namespace bacuri::vms::client
