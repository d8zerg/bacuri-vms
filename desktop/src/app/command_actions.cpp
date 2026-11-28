#include "app/window_commands.hpp"
#include "commands/command_time.hpp"
#include <QJsonDocument>

namespace bacuri::vms::client
{
namespace
{
QString message(const char* source)
{
    return QCoreApplication::translate("Shell", source);
}
} // namespace
void WindowCommands::setFrame(QQuickItem* item)
{
    if (frame_ == item)
        return;
    if (frame_)
        frame_->removeEventFilter(this);
    disconnect(frameDestroyed_);
    frame_ = item;
    framePoint_ = item && navigation_ ? navigation_->pointId() : QString{};
    frameRevision_ = workspace_ ? workspace_->points()->lookup(framePoint_).value("access_revision") : QVariant{};
    if (item)
        item->installEventFilter(this);
    if (item)
        frameDestroyed_ = connect(item, &QObject::destroyed, this, &WindowCommands::frameChanged);
    emit frameChanged();
}
QString WindowCommands::intervalEnd(const QString& start, const QString& id) const
{
    return commandTime(start, workspace_->timeZone(id), QDateTime::currentDateTimeUtc())
        .addSecs(60)
        .toUTC()
        .toString(Qt::ISODateWithMs);
}
QString WindowCommands::now() const
{
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
}
void WindowCommands::pointAction(const QString& action, const QString& id, const QString& start,
                                 const QString& end, const QString& argument)
{
    const auto* command = catalog_ ? catalog_->find(action) : nullptr;
    if (!command || !command->submit || !pointAvailable(action, workspace_->points()->lookup(id))) {
        emit feedback(message("This action is unavailable."));
        return;
    }
    command->submit(*this, {id, start, end, argument});
}
} // namespace bacuri::vms::client
