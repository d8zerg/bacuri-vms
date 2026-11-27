#pragma once

#include <QNetworkAccessManager>
#include <QObject>
#include <QTcpServer>
#include <QUrl>

#include <string>

namespace bacuri::vms::client
{
// Local gateway for components that load site resources themselves, such as the MapLibre map.
// They cannot add the bearer token of the desktop session, so their requests to the site are
// rewritten to http://127.0.0.1:<port>/<secret>/... and the gateway forwards each GET request to
// the site with the token and the trusted certificates of the site. The random secret keeps other
// local processes from using the session through the gateway.
class SiteProxy : public QObject
{
    Q_OBJECT

  public:
    SiteProxy(QUrl site, QString token, QString ca_file, QObject* parent = nullptr);
    bool start();
    // Rewrites a URL of the site to the gateway and leaves any other URL unchanged. Safe to call
    // from other threads after start.
    std::string local(const std::string& url) const;
    QString base() const
    {
        return QString::fromStdString(base_);
    }
    // Requests forwarded to the site and those the site answered with an error.
    int forwarded() const
    {
        return forwarded_;
    }
    int failed() const
    {
        return failed_;
    }

  private:
    void accept();
    void forward(class QTcpSocket* socket, const QByteArray& path);

    QUrl site_;
    QString token_;
    QString ca_file_;
    std::string origin_;
    std::string base_;
    QByteArray secret_;
    QTcpServer server_;
    QNetworkAccessManager network_;
    int forwarded_{};
    int failed_{};
};
} // namespace bacuri::vms::client
