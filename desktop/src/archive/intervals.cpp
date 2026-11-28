#include "archive/archive_commands.hpp"
#include <QJsonDocument>
#include <QJsonArray>
#include <QCoreApplication>

namespace bacuri::vms::client
{
namespace
{
QString message(const char* source)
{
    return QCoreApplication::translate("Shell", source);
}
} // namespace
void ArchiveCommands::open(const CommandInput& input, bool showIntervals)
{
    const auto id = input.point;
    const auto from = context_.parseTime(input.start, id);
    if (!from.isValid())
        return;
    const auto point = context_.site().point(id);
    cancel();
    const auto at = from.toUTC().toString(Qt::ISODateWithMs);
    auto data = point;
    data["at"] = at;
    if (showIntervals)
        context_.tool("intervals", data);
    auto* reply = context_.site().pointRequest(
        id, "archive", QUrlQuery{{"start", at}, {"end", from.addSecs(60).toUTC().toString(Qt::ISODateWithMs)}});
    if (!reply) {
        context_.feedback(message("This action is unavailable."));
        return;
    }
    read_ = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply, id, at] {
        reply->deleteLater();
        if (read_ != reply)
            return;
        read_.clear();
        if (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute) != 200) {
            context_.feedback(context_.site().actionFailure(reply));
            return;
        }
        const auto rows = QJsonDocument::fromJson(reply->readAll()).object().value("items");
        if (!rows.isArray()) {
            context_.feedback(message("The site could not complete the action."));
            return;
        }
        setTime(at);
        emit loaded(id, at, rows.toArray().toVariantList());
    });
}
ArchiveCommands::~ArchiveCommands()
{
    cancel();
}
void ArchiveCommands::setTime(const QString& time)
{
    if (time_ != time) {
        time_ = time;
        emit timeChanged();
    }
}
void ArchiveCommands::clear()
{
    cancel();
    setTime({});
}
void ArchiveCommands::cancel()
{
    if (auto* reply = read_.data()) {
        read_.clear();
        reply->abort();
    }
}
} // namespace bacuri::vms::client
