#include "app/workspace.hpp"

#include <QFile>
#include <QLocale>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QPointer>
#include <QSettings>

namespace bacuri::vms::client
{
namespace
{
QString replyCause(const QNetworkReply& reply, const QJsonObject& problem)
{
    if (reply.error() == QNetworkReply::SslHandshakeFailedError)
        return QStringLiteral("certificate_untrusted");
    const int status = reply.attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (!status)
        return QStringLiteral("site_unreachable");
    const auto code = problem.value("code").toString();
    if (!code.isEmpty())
        return code;
    if (status == 401)
        return QStringLiteral("unauthenticated");
    if (status == 403)
        return QStringLiteral("forbidden");
    return QStringLiteral("dependency_unavailable");
}
} // namespace
Workspace::Workspace(SignInOptions options, QObject* parent) : QObject{parent}, options_{std::move(options)}
{
    connect(this, &Workspace::changed, this, &Workspace::messageChanged);
    if (options_.site.isEmpty())
        options_.site = QUrl(QSettings().value("site/origin").toString());
}

void Workspace::update(State state, const QString& cause)
{
    state_ = state;
    cause_ = cause;
    emit changed();
}

void Workspace::signIn(const QString& address)
{
    if (signedIn() || sign_in_)
        return;
    const QUrl url(address.trimmed(), QUrl::StrictMode);
    if (!url.isValid() || url.scheme() != "https" || url.host().isEmpty() || !url.userInfo().isEmpty() ||
        url.hasQuery() || url.hasFragment() || (!url.path().isEmpty() && url.path() != "/")) {
        update(Failed, "invalid_site");
        return;
    }
    options_.site = url;
    QSettings().setValue("site/origin", url.toString());
    auto* attempt = new SignIn(options_, this);
    sign_in_ = attempt;
    connect(attempt, &SignIn::signed_in, this, [this, attempt](const DesktopSession& session) {
        attempt->deleteLater();
        sign_in_.clear();
        session_ = session;
        refresh();
    });
    connect(attempt, &SignIn::failed, this, [this, attempt](const QString& cause) {
        attempt->deleteLater();
        sign_in_.clear();
        update(Failed, cause);
    });
    update(Authorizing);
    attempt->start();
}

void Workspace::cancel()
{
    if (!sign_in_)
        return;
    sign_in_->disconnect(this);
    sign_in_->deleteLater();
    sign_in_.clear();
    update(Disconnected);
}

QNetworkRequest Workspace::request(const QString& path) const
{
    auto req = site_request(options_.site.resolved(QUrl(path)), options_.ca_file, session_.token);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    req.setTransferTimeout(15000);
    return req;
}

void Workspace::refresh()
{
    if (!signedIn() || pending_)
        return;
    retry_sign_out_ = false;
    sites_.clear();
    received_.clear();
    for (const auto role : session_.site_roles) {
        const auto id = role.toObject().value("site_id").toString();
        if (!id.isEmpty() && !sites_.contains(id))
            sites_.append(id);
    }
    update(Loading);
    fetchSite();
}

void Workspace::fetchSite()
{
    if (sites_.isEmpty()) {
        points_.apply(received_);
        received_.clear();
        update(Ready);
        return;
    }
    const auto site = sites_.takeFirst();
    auto* reply = network_.get(request("/api/v1/sites/" + QUrl::toPercentEncoding(site) + "/points"));
    pending_ = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply, site] {
        reply->deleteLater();
        if (pending_ != reply)
            return;
        pending_.clear();
        const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const auto json = QJsonDocument::fromJson(reply->readAll());
        if (status == 200 && json.isArray()) {
            for (const auto point : json.array()) {
                auto row = point.toObject().toVariantMap();
                row.insert("site_id", site);
                received_.append(row);
            }
            fetchSite();
            return;
        }
        if (status == 401)
            session_ = {};
        if (status == 401 || status == 403)
            points_.apply({});
        received_.clear();
        update(Failed, replyCause(*reply, json.object()));
    });
}

void Workspace::signOut()
{
    if (!signedIn() || state_ == SigningOut)
        return;
    if (auto* previous = pending_.data()) {
        pending_.clear();
        previous->abort();
    }
    points_.apply({});
    received_.clear();
    retry_sign_out_ = false;
    auto* reply = network_.deleteResource(request("/api/v1/session"));
    pending_ = reply;
    update(SigningOut);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        pending_.clear();
        const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 204 || status == 401) {
            session_ = {};
            update(Disconnected);
        } else {
            retry_sign_out_ = true;
            update(Failed, replyCause(*reply, QJsonDocument::fromJson(reply->readAll()).object()));
        }
    });
}

QString Workspace::failureText() const
{
    if (cause_ == "invalid_site")
        return tr("Enter the HTTPS address of the site.");
    if (cause_ == "certificate_untrusted")
        return tr("The site certificate is not trusted.");
    if (cause_ == "browser_unavailable")
        return tr("The system browser could not be opened.");
    if (cause_ == "sign_in_timeout")
        return tr("Sign-in timed out.");
    if (cause_ == "site_unreachable")
        return tr("Could not connect to the site.");
    if (cause_.isEmpty())
        return {};
    static const QJsonObject catalog = [] {
        QFile file(":/site/messages.json");
        if (!file.open(QIODevice::ReadOnly))
            qFatal("Missing site error catalog");
        return QJsonDocument::fromJson(file.readAll()).object();
    }();
    const auto code = cause_ == "access_denied" ? QStringLiteral("forbidden") : cause_;
    const auto entry = catalog.value(catalog.contains(code) ? code : QStringLiteral("internal_error")).toObject();
    return entry.value(QLocale().name().section('_', 0, 0)).toString(entry.value("en").toString());
}

} // namespace bacuri::vms::client
