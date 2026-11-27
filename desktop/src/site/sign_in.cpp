#include "site/sign_in.hpp"

#include <QDesktopServices>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QProcess>
#include <QSslCertificate>
#include <QTcpSocket>
#include <QUrlQuery>

namespace bacuri::vms::client
{
namespace
{
constexpr auto callback_path = "/callback";
constexpr qsizetype request_limit = 8192;

// The page the browser shows after it delivered the code to the application.
QByteArray answer(int status, const QString& text)
{
    const auto body =
        QStringLiteral("<!doctype html><meta charset=\"utf-8\"><title>Bacuri Desktop</title><p>%1</p>")
            .arg(text.toHtmlEscaped())
            .toUtf8();
    return QByteArray("HTTP/1.1 ") + QByteArray::number(status) + (status == 200 ? " OK" : " Bad Request") +
           "\r\nContent-Type: text/html; charset=utf-8\r\nCache-Control: no-store\r\nConnection: close\r\n"
           "Content-Length: " +
           QByteArray::number(body.size()) + "\r\n\r\n" + body;
}
} // namespace

QSslConfiguration site_tls(const QString& ca_file)
{
    auto tls = QSslConfiguration::defaultConfiguration();
    tls.setPeerVerifyMode(QSslSocket::VerifyPeer);
    if (!ca_file.isEmpty())
        tls.setCaCertificates(QSslCertificate::fromPath(ca_file));
    return tls;
}

SignIn::SignIn(SignInOptions options, QObject* parent)
    : QObject{parent}, options_{std::move(options)}, pkce_{new_pkce()}
{
    connect(&server_, &QTcpServer::newConnection, this, &SignIn::accept);
    timeout_.setSingleShot(true);
    connect(&timeout_, &QTimer::timeout, this, [this] { finish_with(QStringLiteral("sign_in_timeout")); });
}

QUrl SignIn::confirmation_url() const
{
    auto url = options_.site.resolved(QUrl{QStringLiteral("/desktop-sign-in")});
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("code_challenge"), pkce_.challenge);
    query.addQueryItem(QStringLiteral("code_challenge_method"), QStringLiteral("S256"));
    query.addQueryItem(QStringLiteral("redirect_uri"), redirect_);
    query.addQueryItem(QStringLiteral("state"), pkce_.state);
    url.setQuery(query);
    return url;
}

void SignIn::start()
{
    if (!server_.listen(QHostAddress::LocalHost, 0)) {
        finish_with(QStringLiteral("browser_unavailable"));
        return;
    }
    redirect_ = QStringLiteral("http://127.0.0.1:%1%2").arg(server_.serverPort()).arg(callback_path);
    timeout_.start(options_.timeout_seconds * 1000);
    const auto url = confirmation_url();
    const bool opened = options_.browser.isEmpty()
                            ? QDesktopServices::openUrl(url)
                            : QProcess::startDetached(options_.browser, {url.toString(QUrl::FullyEncoded)});
    if (!opened)
        finish_with(QStringLiteral("browser_unavailable"));
}

void SignIn::accept()
{
    while (auto* socket = server_.nextPendingConnection()) {
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
            if (socket->bytesAvailable() > request_limit || !socket->canReadLine()) {
                if (socket->bytesAvailable() > request_limit)
                    socket->abort();
                return;
            }
            const auto callback =
                read_callback(socket->readLine(), QString::fromLatin1(callback_path), pkce_.state);
            if (!callback || done_) {
                // A stray request does not end the sign-in.
                socket->write(answer(400, tr("This address belongs to the sign-in of Bacuri Desktop.")));
                socket->disconnectFromHost();
                return;
            }
            socket->write(answer(200, tr("Bacuri Desktop received the sign-in. You can close this tab.")));
            // The browser gets the page before the sign-in may end the application.
            socket->waitForBytesWritten(2000);
            socket->disconnectFromHost();
            server_.close();
            if (!callback->error.isEmpty())
                finish_with(QStringLiteral("access_denied"));
            else
                exchange(callback->code);
        });
    }
}

void SignIn::exchange(const QString& code)
{
    QNetworkRequest request{options_.site.resolved(QUrl{QStringLiteral("/api/v1/session/desktop")})};
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setSslConfiguration(site_tls(options_.ca_file));
    const QJsonObject body{{"code", code}, {"code_verifier", pkce_.verifier}, {"redirect_uri", redirect_}};
    auto* reply = network_.post(request, QJsonDocument{body}.toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const auto json = QJsonDocument::fromJson(reply->readAll()).object();
        if (status == 201 && json.value("token").toString().startsWith("d1.")) {
            done_ = true;
            timeout_.stop();
            emit signed_in({json.value("token").toString(), json.value("user_id").toString(),
                            json.value("site_roles").toArray(), json.value("expires_at").toString()});
            return;
        }
        if (reply->error() == QNetworkReply::SslHandshakeFailedError)
            finish_with(QStringLiteral("certificate_untrusted"));
        else if (status == 0)
            finish_with(QStringLiteral("site_unreachable"));
        else
            finish_with(json.value("code").toString(QStringLiteral("internal_error")));
    });
}

void SignIn::finish_with(const QString& cause)
{
    if (done_)
        return;
    done_ = true;
    timeout_.stop();
    server_.close();
    emit failed(cause);
}
} // namespace bacuri::vms::client
