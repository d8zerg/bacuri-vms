#include "archive/archive_commands.hpp"

#include <QCoreApplication>
#include <QUuid>

namespace bacuri::vms::client
{
using W = WindowCommands;
using K = QKeySequence;
using S = CommandScope;
using P = ParameterType;

void ArchiveCommands::install(CommandRegistry& registry, ContextAccess context,
                              FeatureAccess<ArchiveCommands> archive)
{
    const auto timeline = [context, archive](const W& w) {
        return !context(w).camera().isEmpty() && !archive(w).time().isEmpty();
    };
    const auto step = [context, archive](int seconds) {
        return [context, archive, seconds](W& w) {
            auto& feature = archive(w);
            const auto at = QDateTime::fromString(feature.time(), Qt::ISODateWithMs).addSecs(seconds);
            feature.open({context(w).focusedCamera(), at.toString(Qt::ISODateWithMs), {}, {}}, false);
        };
    };
    registry.add({{"archive-earlier",
                   "Previous archive minute",
                   "archive",
                   {K(Qt::Key_Left)},
                   S::Timeline,
                   P::None,
                   timeline,
                   step(-60)},
                  {"archive-later",
                   "Next archive minute",
                   "archive",
                   {K(Qt::Key_Right)},
                   S::Timeline,
                   P::None,
                   timeline,
                   step(60)}});
    addCameraCommand(registry, context, "archive", "Open archive", "archive", {}, P::Time, {},
                     [archive](W& w, const CommandInput& input) { archive(w).open(input); });
    addCameraCommand(registry, context, "bookmark", "Temporary mark (this window)", "map-pin", {}, P::Time, {},
                     [archive](W& w, const CommandInput& input) { archive(w).mark(input); });
}
void ArchiveCommands::mark(const CommandInput& input)
{
    const auto& [id, start, end, argument] = input;
    const auto from = context_.parseTime(start, id);
    if (!from.isValid())
        return;
    const auto point = context_.site().point(id);
    marks_.append(QVariantMap{{"key", "bookmark/" + QUuid::createUuid().toString(QUuid::WithoutBraces)},
                              {"id", id},
                              {"name", argument.trimmed().isEmpty() ? point.value("name") : argument.trimmed()},
                              {"at", from.toUTC().toString(Qt::ISODateWithMs)}});
    emit marksChanged();
    context_.submitted();
    context_.feedback(QCoreApplication::translate("Shell", "Temporary mark saved for this window."));
}
} // namespace bacuri::vms::client
