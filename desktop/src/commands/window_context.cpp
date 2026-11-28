#include "commands/window_context.hpp"
#include "commands/command_time.hpp"
#include <QCoreApplication>

namespace bacuri::vms::client
{
QDateTime WindowContext::parseTime(const QString& input, const QString& point)
{
    auto value = commandTime(input, site().timeZone(point), QDateTime::currentDateTimeUtc());
    if (!value.isValid())
        feedback(QCoreApplication::translate(
            "Shell", "Enter a valid time. For an ambiguous local time, include its UTC offset."));
    return value;
}
void addCameraCommand(CommandRegistry& registry, ContextAccess context, QString id, const char* name, QString icon,
                      QList<QKeySequence> keys, ParameterType type, std::function<void(WindowCommands&)> run,
                      CommandHandler submit, CameraAccess permits)
{
    registry.add({id, name, icon, keys, CommandScope::Video, type,
                  [context, permits](const WindowCommands& window) {
                      const auto point = context(window).camera();
                      return !point.isEmpty() && (!permits || permits(window, point));
                  },
                  run ? std::move(run) : [context, id](WindowCommands& window) {
                      auto& view = context(window);
                      view.tool(id, view.camera());
                  },
                  false, std::move(submit)});
}
} // namespace bacuri::vms::client
