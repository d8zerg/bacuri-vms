#pragma once

#include "site/sign_in.hpp"
#include "app/point_model.hpp"

#include <QQmlEngine>
#include <QPointer>
#include <QVariantList>

namespace bacuri::vms::client
{
class Workspace : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Shared by the windows of the application")
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
    virtual bool signedIn() const
    {
        return !session_.token.isEmpty();
    }
    virtual bool sessionActions() const
    {
        return true;
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

  signals:
    void messageChanged();
    void changed();

  protected:
    void update(State state, const QString& cause = {});

  private:
    void fetchSite();
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
