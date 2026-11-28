#include "site/site_proxy.hpp"

#include "site/pkce.hpp"
#include "site/sign_in.hpp"

#include <QNetworkReply>
#include <QRandomGenerator>
#include <QTcpSocket>

namespace bacuri::vms::client
{
namespace
{
constexpr qsizetype request_limit = 8192;

QByteArray response(int status, const QByteArray& type, const QByteArray& body)
{
    return "HTTP/1.1 " + QByteArray::number(status) + " Status\r\nContent-Type: " + type +
           "\r\nCache-Control: no-store\r\nConnection: close\r\nContent-Length: " +
           QByteArray::number(body.size()) + "\r\n\r\n" + body;
}
} // namespace

SiteProxy::SiteProxy(QUrl site, QString token, QString ca_file, QObject* parent)
    : QObject{parent}, site_{std::move(site)}, token_{std::move(token)}, ca_file_{std::move(ca_file)}
{
    QByteArray random(32, Qt::Uninitialized);
    for (auto& byte : random)
        byte = static_cast<char>(QRandomGenerator::system()->bounded(256));
    secret_ = base64url(random).toLatin1();
    origin_ = site_.adjusted(QUrl::RemovePath | QUrl::RemoveQuery | QUrl::RemoveFragment)
                  .toString(QUrl::FullyEncoded)
                  .toStdString();
    connect(&server_, &QTcpServer::newConnection, this, &SiteProxy::accept);
}

bool SiteProxy::start()
{
    if (!server_.listen(QHostAddress::LocalHost, 0))
        return false;
    base_ = "http://127.0.0.1:" + std::to_string(server_.serverPort()) + "/" + secret_.toStdString();
    return true;
}

std::string SiteProxy::local(const std::string& url) const
{
    if (base_.empty() || !url.starts_with(origin_ + "/"))
        return url;
    return base_ + url.substr(origin_.size());
}

void SiteProxy::accept()
{
    while (auto* socket = server_.nextPendingConnection()) {
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
            if (socket->property("forwarded").toBool())
                return;
            if (socket->bytesAvailable() > request_limit) {
                socket->abort();
                return;
            }
            if (!socket->canReadLine())
                return;
            const auto parts = socket->readLine().trimmed().split(' ');
            const QByteArray prefix = "/" + secret_ + "/";
            if (parts.size() != 3 || parts[0] != "GET" || !parts[1].startsWith(prefix)) {
                socket->write(response(404, "text/plain", "Not found"));
                socket->disconnectFromHost();
                return;
            }
            socket->setProperty("forwarded", true);
            forward(socket, parts[1].mid(prefix.size() - 1));
        });
    }
}

void SiteProxy::forward(QTcpSocket* socket, const QByteArray& path)
{
    const auto request =
        site_request(QUrl{QString::fromStdString(origin_) + QString::fromLatin1(path)}, ca_file_, token_);
    auto* reply = network_.get(request);
    ++forwarded_;
    connect(reply, &QNetworkReply::finished, socket, [this, socket, reply] {
        reply->deleteLater();
        const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status != 200)
            ++failed_;
        const auto type = reply->header(QNetworkRequest::ContentTypeHeader).toString().toLatin1();
        socket->write(response(status == 0 ? 502 : status, type.isEmpty() ? "application/octet-stream" : type,
                               reply->readAll()));
        socket->disconnectFromHost();
    });
}
} // namespace bacuri::vms::client
