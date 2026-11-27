#pragma once

#include "site/pkce.hpp"

#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QObject>
#include <QSslConfiguration>
#include <QTcpServer>
#include <QTimer>
#include <QUrl>

namespace bacuri::vms::client
{
struct DesktopSession {
    QString token;
    QString user_id;
    QJsonArray site_roles;
    QString expires_at;
};

struct SignInOptions {
    QUrl site;       // Origin of the site, such as https://vms.example:8443.
    QString ca_file; // Certificates trusted for the site, or the system store when empty.
    QString browser; // Command that opens a URL, or the default browser when empty.
    int timeout_seconds{300};
};

// TLS settings of requests to the site: the given certificates, or the system store.
QSslConfiguration site_tls(const QString& ca_file);

// Sign-in of the desktop client through the system browser. The client listens on a random
// port of 127.0.0.1, opens the confirmation page of the site with its PKCE challenge, receives
// the one-time code on the loopback address and exchanges it with its verifier for a session.
// Failures name a code of the site error catalog or one of the local causes browser_unavailable,
// access_denied, sign_in_timeout, site_unreachable and certificate_untrusted.
class SignIn : public QObject
{
    Q_OBJECT

  public:
    explicit SignIn(SignInOptions options, QObject* parent = nullptr);
    void start();
    QUrl confirmation_url() const;

  signals:
    void signed_in(const bacuri::vms::client::DesktopSession& session);
    void failed(const QString& cause);

  private:
    void accept();
    void exchange(const QString& code);
    void finish_with(const QString& cause);

    SignInOptions options_;
    Pkce pkce_;
    QString redirect_;
    QTcpServer server_;
    QNetworkAccessManager network_;
    QTimer timeout_;
    bool done_{};
};
} // namespace bacuri::vms::client
