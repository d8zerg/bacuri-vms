#include "export/export_jobs.hpp"
#include "commands/command_time.hpp"
#include <QCoreApplication>

namespace bacuri::vms::client
{
using W = WindowCommands;
using S = CommandScope;
using P = ParameterType;

void installExportCommands(CommandRegistry& registry, ContextAccess context, FeatureAccess<ExportJobs> jobs)
{
    registry.add({"exports",
                  "Export jobs",
                  "file-down",
                  {},
                  S::Global,
                  P::None,
                  [context](const W& w) { return context(w).site().signedIn(); },
                  [context](W& w) { context(w).tool("exports", {}); }});
    addCameraCommand(
        registry, context, "export", "Export interval", "file-down", {}, P::Interval, {},
        [context, jobs](W& w, const CommandInput& input) {
            auto& window = context(w);
            const auto interval =
                commandInterval(input.start + "/" + input.end, window.site().timeZone(input.point),
                                QDateTime::currentDateTimeUtc());
            if (!interval.start.isValid() || interval.end <= interval.start) {
                window.feedback(QCoreApplication::translate("Shell", "The end must be later than the start."));
                return;
            }
            if (jobs(w).enqueue(window.site().point(input.point), interval.start.toString(Qt::ISODateWithMs),
                                interval.end.toString(Qt::ISODateWithMs))) {
                window.submitted();
                window.feedback(QCoreApplication::translate("Shell", "Export queued."));
            } else
                window.feedback(QCoreApplication::translate("Shell", "This action is unavailable."));
        },
        [](const W&, const QVariantMap& point) { return point.value("can_export").toBool(); });
}
} // namespace bacuri::vms::client
