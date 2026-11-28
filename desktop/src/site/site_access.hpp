#pragma once

#include <QJsonObject>
#include <QNetworkReply>
#include <QTimeZone>
#include <QUrlQuery>
#include <QVariantMap>

namespace bacuri::vms::client
{
class SiteAccess : public QObject
{
    Q_OBJECT
  public:
    using QObject::QObject;
    virtual bool signedIn() const = 0;
    virtual bool ready() const = 0;
    virtual bool signingOut() const = 0;
    virtual bool sessionActions() const = 0;
    virtual QVariantMap point(const QString& id) const = 0;
    virtual QString profile(const QString& id) const = 0;
    virtual QTimeZone timeZone(const QString& id) const = 0;
    virtual quint64 accessGeneration() const = 0;
    virtual QNetworkReply* pointRequest(const QString& id, const QString& action, const QUrlQuery& query,
                                        const QJsonObject& body = {}) = 0;
    virtual void releaseMedia(const QString& session) = 0;
    virtual QNetworkReply* offerMedia(const QString& session, const QByteArray& sdp) = 0;
    virtual QString describeError(const QString& code) const = 0;
    QString actionFailure(QNetworkReply* reply) const;

  signals:
    void accessChanged();
    void pointsChanged();
};
} // namespace bacuri::vms::client
