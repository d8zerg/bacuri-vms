#include "site/site_access.hpp"

#include <QCoreApplication>
#include <QJsonDocument>

namespace bacuri::vms::client
{
QString SiteAccess::actionFailure(QNetworkReply* reply) const
{
    const auto code = QJsonDocument::fromJson(reply->readAll()).object().value("code").toString();
    if (!code.isEmpty())
        return describeError(code);
    if (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 403)
        return describeError("forbidden");
    return QCoreApplication::translate("Shell", "The site could not complete the action.");
}
} // namespace bacuri::vms::client
