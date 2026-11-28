#pragma once

#include "site/sign_in.hpp"
#include "site/site_access.hpp"
#include "commands/command_registry.hpp"
#include "app/palette_index.hpp"
#include "export/export_jobs.hpp"
#include "app/point_model.hpp"

#include <QQmlEngine>
#include <QPointer>
#include <QSet>
#include <QTimeZone>
#include <QWebSocket>
#include <QUrlQuery>
#include <QJsonObject>
#include <QVariantList>

namespace bacuri::vms::client
{
class Workspace : public SiteAccess
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Shared by the windows of the application")
    Q_PROPERTY(ExportJobs* exports READ exports CONSTANT FINAL)
    Q_PROPERTY(State state READ state NOTIFY changed FINAL)
    Q_PROPERTY(QString address READ address NOTIFY changed FINAL)
    Q_PROPERTY(QString cause READ cause NOTIFY changed FINAL)
    Q_PROPERTY(QString failureMessage READ failureText NOTIFY messageChanged FINAL)
    Q_PROPERTY(bacuri::vms::client::PointModel* points READ points CONSTANT FINAL)
    Q_PROPERTY(bool sessionActions READ sessionActions CONSTANT FINAL)
    Q_PROPERTY(bool retrySignOut READ retrySignOut NOTIFY changed FINAL)
    Q_PROPERTY(bool signedIn READ signedIn NOTIFY changed FINAL)

  public:
    enum State { Disconnected, Authorizing, Loading, Ready, Failed, SigningOut };
    Q_ENUM(State)
    explicit Workspace(SignInOptions options = {}, QObject* parent = nullptr);
    State state() const
    {
        return state_;
    }
    QString address() const
    {
        return options_.site.toString();
    }
    QString cause() const
    {
        return cause_;
    }
    PointModel* points()
    {
        return &points_;
    }
    bool signedIn() const override
    {
        return !session_.token.isEmpty();
    }
    bool sessionActions() const override
    {
        return true;
    }
    bool ready() const override
    {
        return state_ == Ready;
    }
    bool signingOut() const override
    {
        return state_ == SigningOut;
    }
    QVariantMap point(const QString& id) const override
    {
        return points_.lookup(id);
    }
    QString describeError(const QString& code) const override
    {
        return errorMessage(code);
    }
    bool retrySignOut() const
    {
        return retry_sign_out_;
    }
    Q_INVOKABLE void signIn(const QString& address);
    Q_INVOKABLE virtual void refresh();
    Q_INVOKABLE void signOut();
    Q_INVOKABLE void cancel();
    QString failureText() const;
    ExportJobs* exports()
    {
        return &exports_;
    }
    const PaletteIndex& index() const
    {
        return index_;
    }
    CommandRegistry* registry()
    {
        return &registry_;
    }
    QVariantMap siteRole(const QString& pointId) const;
    void publishKeymap(const QString& pointId, const QVariantMap& overrides);
    QString profileForSite(const QString& site) const;
    QString profile(const QString& pointId) const override;
    quint64 accessGeneration() const override
    {
        return indexGeneration_;
    }
    QTimeZone timeZone(const QString& pointId) const override;
    static QString errorMessage(const QString& code);
    virtual void loadCommandObjects();
    void releaseMedia(const QString& session) override;
    QNetworkReply* offerMedia(const QString& session, const QByteArray& sdp) override;
    QNetworkReply* pointRequest(const QString& pointId, const QString& action, const QUrlQuery& query,
                                const QJsonObject& body = {}) override;

  signals:
    void changed();
    void leavingReady();
    void layoutsChanged();
    void commandObjectsChanged();
    void commandIndexFailed(QString message);
    void messageChanged();

  protected:
    void setCommandObjects(QVariantList objects);
    void update(State state, const QString& cause = {});

  private:
    PaletteIndex index_;
    CommandRegistry registry_{this};
    ExportJobs exports_{this};
    void clearCommandObjects();
    void fetchSite();
    void followAlerts(const QString& site, const QString& cursor);
    QVariantMap indexRow(const QString& site, const QString& kind, const QJsonObject& row) const;
    QSet<QString> loaded_;
    QHash<QString, QWebSocket*> feeds_;
    quint64 indexGeneration_{};

    QSet<QNetworkReply*> index_requests_;
    QNetworkRequest request(const QString& path) const;
    SignInOptions options_;
    State state_{Disconnected};
    QString cause_;
    DesktopSession session_;
    PointModel points_;
    QStringList sites_;
    QVariantList received_;
    bool retry_sign_out_{};
    QPointer<SignIn> sign_in_;
    QPointer<QNetworkReply> pending_;
    QNetworkAccessManager network_;
};

} // namespace bacuri::vms::client
