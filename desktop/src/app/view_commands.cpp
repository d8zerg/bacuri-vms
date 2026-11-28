#include "app/window_commands.hpp"
#include <QGuiApplication>
#include <QKeyEvent>
#include <QScreen>
#include <QSettings>
#include <QUuid>
#include "media/native_frame.hpp"
namespace bacuri::vms::client
{
void WindowCommands::publishKeymap()
{
    if (available("publish-keymap"))
        workspace_->publishKeymap(navigation_->pointId(), catalog_->bindings(profile_));
}
void WindowCommands::saveView()
{
    if (history_.size() >= 20)
        history_.removeFirst();
    history_.append({navigation_->save(), window_->screen(), window_->geometry(), window_->visibility(),
                     temporaryLayout_, archive_.time(), focusedAlert_});
    emit changed();
    updateShortcuts();
}
void WindowCommands::undoView()
{
    if (history_.isEmpty())
        return;
    const auto view = history_.takeLast();
    if (view.screen)
        window_->setScreen(view.screen);
    window_->setVisibility(view.visibility);
    if (view.visibility == QWindow::Windowed)
        window_->setGeometry(view.geometry);
    temporaryLayout_ = view.layout;
    navigation_->restore(view.navigation);
    archive_.setTime(view.archiveTime);
    focusedAlert_ = view.alert;
    emit layoutChanged();
    update();
}
bool WindowCommands::temporaryLayout(const QStringList& keys, int screen)
{
    auto* target = QGuiApplication::screens().value(screen - 1);
    if (!target || keys.isEmpty() || keys.size() > 4 || workspace_->state() != Workspace::Ready)
        return false;
    QVariantList points;
    for (const auto& key : keys) {
        const auto id = key.mid(6);
        if (!key.startsWith("point/") || workspace_->points()->lookup(id).isEmpty() || points.contains(id))
            return false;
        points.append(id);
    }
    saveView();
    temporaryLayout_ = {{"temporary", true},
                        {"name", QCoreApplication::translate("Shell", "Temporary layout")},
                        {"data", QVariantMap{{"points", points}}}};
    if (window_->screen() != target) {
        window_->setScreen(target);
        window_->setPosition(target->availableGeometry().topLeft());
    }
    emit layoutApplied("temporary");
    emit layoutChanged();
    return true;
}
void WindowCommands::recordGesture(const QString& action, bool stroke)
{
    if (profile_.isEmpty())
        return;
    QSettings settings;
    const auto key = "profiles/" + profile_ + "/gestures/" + action + (stroke ? "/stroke" : "/menu");
    settings.setValue(key, settings.value(key).toLongLong() + 1);
}
QVariantMap WindowCommands::gestureCounts() const
{
    QSettings settings;
    settings.beginGroup("profiles/" + profile_ + "/gestures");
    QVariantMap counts;
    for (const auto& id : settings.childGroups())
        counts.insert(id, QVariantMap{{"stroke", settings.value(id + "/stroke", 0)},
                                      {"menu", settings.value(id + "/menu", 0)}});
    return counts;
}
bool WindowCommands::eventFilter(QObject* obj, QEvent* event)
{
    if (obj == frame_ && event->type() == QEvent::DynamicPropertyChange)
        emit frameChanged();
    if (obj != window_)
        return false;
    if (event->type() == QEvent::WindowDeactivate || event->type() == QEvent::FocusOut) {
        ctrlTimer_.stop();
        setHints(false);
        stopMotion();
    }
    if (event->type() != QEvent::KeyPress && event->type() != QEvent::KeyRelease)
        return false;
    auto* key = static_cast<QKeyEvent*>(event);
    if (motionKey_ && key->key() == motionKey_) {
        if (event->type() == QEvent::KeyRelease && !key->isAutoRepeat())
            stopMotion();
        return true;
    }
    if (key->isAutoRepeat())
        return false;
    if (!modal_ && event->type() == QEvent::KeyPress && focusedScope() == CommandScope::Video) {
        for (const auto& cmd : catalog_->commands()) {
            if (cmd.hold && shortcuts_.value(cmd.id)->keys().contains(QKeySequence(key->keyCombination())) &&
                available(cmd.id)) {
                cmd.run(*this);
                if (live_.moving())
                    motionKey_ = key->key();
                return true;
            }
        }
    }
    if (key->key() == Qt::Key_Control && event->type() == QEvent::KeyPress && !modal_)
        ctrlTimer_.start(500);
    else {
        ctrlTimer_.stop();
        setHints(false);
    }
    return false;
}
void WindowCommands::setHints(bool show)
{
    if (hints_ == show)
        return;
    hints_ = show;
    emit hintsChanged();
}
} // namespace bacuri::vms::client
