#include "app/workspace.hpp"
#include "app/window_commands.hpp"

#include <QFile>
#include <QCryptographicHash>
#include <QLocale>
#include <QJsonDocument>
#include <QJsonObject>
#include <QWebSocketHandshakeOptions>
#include <QNetworkReply>
#include <QPointer>
#include <QSettings>
#include <QUuid>
#include <utility>

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
Workspace::Workspace(SignInOptions options, QObject* parent) : SiteAccess{parent}, options_{std::move(options)}
{
    WindowCommands::registerCommands(registry_);
    connect(this, &Workspace::changed, this, &SiteAccess::accessChanged);
    connect(&points_, &PointModel::revised, this, &SiteAccess::pointsChanged);
    connect(&points_, &PointModel::revised, this, [this] {
        QVariantList rows;
        rows.reserve(points_.count());
        for (int i = 0; i < points_.count(); ++i)
            rows.append(points_.data(points_.index(i), PointModel::Point));
        index_.setPoints(rows);
    });
    connect(this, &Workspace::changed, this, &Workspace::messageChanged);
    if (options_.site.isEmpty())
        options_.site = QUrl(QSettings().value("site/origin").toString());
}

QVariantMap Workspace::siteRole(const QString& pointId) const
{
    if (session_.site_roles.isEmpty())
        return {};
    const auto site = points_.lookup(pointId).value("site_id").toString();
    if (site.isEmpty())
        return session_.site_roles.first().toObject().toVariantMap();
    for (const auto role : session_.site_roles)
        if (role.toObject().value("site_id").toString() == site)
            return role.toObject().toVariantMap();
    return {};
}
void Workspace::publishKeymap(const QString& pointId, const QVariantMap& overrides)
{
    const auto membership = siteRole(pointId);
    if (membership.value("role") != "administrator" || !membership.contains("keymap_revision"))
        return;
    const auto site = membership.value("site_id").toString(), profileId = profile(pointId);
    auto bindings = membership.value("keymap").toMap();
    for (auto it = overrides.begin(); it != overrides.end(); ++it)
        bindings.insert(it.key(), it.value());
    auto req = request("/api/v1/sites/" + QUrl::toPercentEncoding(site) + "/keyboard-profile");
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    auto* reply =
        network_.put(req, QJsonDocument(QJsonObject{{"revision", membership.value("keymap_revision").toString()},
                                                    {"bindings", QJsonObject::fromVariantMap(bindings)}})
                              .toJson());
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, profileId, site, bindings, token = session_.token] {
                reply->deleteLater();
                if (session_.token != token || !signedIn())
                    return;
                const auto data = QJsonDocument::fromJson(reply->readAll()).object();
                if (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != 200) {
                    emit commandIndexFailed(errorMessage(replyCause(*reply, data)));
                    return;
                }
                for (qsizetype i = 0; i < session_.site_roles.size(); ++i) {
                    auto role = session_.site_roles.at(i).toObject();
                    if (role.value("site_id").toString() != site)
                        continue;
                    role["keymap"] = QJsonObject::fromVariantMap(bindings);
                    role["keymap_revision"] = data.value("revision");
                    session_.site_roles[i] = role;
                }
                emit registry_.bindingsChanged(profileId);
            });
}
QString Workspace::profile(const QString& pointId) const
{
    if (session_.user_id.isEmpty() || session_.site_roles.isEmpty())
        return {};
    const auto site = points_.lookup(pointId)
                          .value("site_id", session_.site_roles.first().toObject().value("site_id").toString())
                          .toString();
    return profileForSite(site);
}
QString Workspace::profileForSite(const QString& site) const
{
    if (session_.user_id.isEmpty() || site.isEmpty())
        return {};
    const auto origin = options_.site.adjusted(QUrl::RemovePath).toEncoded();
    return QString::fromLatin1(
        QCryptographicHash::hash(origin + '/' + site.toUtf8() + '/' + session_.user_id.toUtf8(),
                                 QCryptographicHash::Sha256)
            .toHex());
}
QTimeZone Workspace::timeZone(const QString& pointId) const
{
    const auto site = points_.lookup(pointId).value("site_id").toString();
    for (const auto role : session_.site_roles) {
        const auto row = role.toObject();
        if (row.value("site_id").toString() == site) {
            const QTimeZone zone(row.value("time_zone").toString().toUtf8());
            if (zone.isValid())
                return zone;
        }
    }
    return QTimeZone::systemTimeZone();
}

void Workspace::update(State state, const QString& cause)
{
    state_ = state;
    if (state != Ready)
        clearCommandObjects();
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
    if (state_ == Ready)
        emit leavingReady();
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
    if (state_ == Ready)
        emit leavingReady();
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
    return errorMessage(cause_ == "access_denied" ? QStringLiteral("forbidden") : cause_);
}

QString Workspace::errorMessage(const QString& code)
{
    static const QJsonObject catalog = [] {
        QFile file(":/site/messages.json");
        if (!file.open(QIODevice::ReadOnly))
            qFatal("Missing site error catalog");
        return QJsonDocument::fromJson(file.readAll()).object();
    }();
    const auto entry = catalog.value(catalog.contains(code) ? code : QStringLiteral("internal_error")).toObject();
    return entry.value(QLocale().name().section('_', 0, 0)).toString(entry.value("en").toString());
}

void Workspace::setCommandObjects(QVariantList objects)
{
    index_.rebuild(objects);
    emit layoutsChanged();
    emit commandObjectsChanged();
}

void Workspace::clearCommandObjects()
{
    ++indexGeneration_;
    loaded_.clear();
    const auto feeds = std::exchange(feeds_, {});
    for (auto* socket : feeds) {
        socket->disconnect(this);
        socket->abort();
        socket->deleteLater();
    }
    const auto pending = std::exchange(index_requests_, {});
    for (auto* reply : pending)
        reply->abort();
    if (!index_.objects().isEmpty())
        setCommandObjects({});
}

void Workspace::loadCommandObjects()
{
    if (!signedIn() || state() != Ready)
        return;
    QSet<QString> sites;
    for (const auto role : session_.site_roles)
        sites.insert(role.toObject().value("site_id").toString());
    for (const QString& site : sites) {
        if (site.isEmpty())
            continue;
        for (const QString kind : {"layout", "alert", "case"}) {
            const auto slot = site + "/" + kind;
            if (loaded_.contains(slot))
                continue;
            loaded_.insert(slot);
            const QString path = kind == "layout"  ? "monitor-layouts"
                                 : kind == "alert" ? "alerts"
                                                   : "investigations";
            auto* reply = network_.get(request("/api/v1/sites/" + QUrl::toPercentEncoding(site) + "/" + path));
            index_requests_.insert(reply);
            connect(reply, &QNetworkReply::finished, this, [this, reply, kind, site] {
                reply->deleteLater();
                if (!index_requests_.remove(reply))
                    return;
                int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                auto json = QJsonDocument::fromJson(reply->readAll());
                if (status != 200) {
                    if (status == 401) {
                        session_ = {};
                        points_.apply({});
                        update(Failed, "unauthenticated");
                    } else if (status != 403 && status != 404) {
                        loaded_.remove(site + "/" + kind);
                        emit commandIndexFailed(errorMessage(replyCause(*reply, json.object())));
                    }
                    return;
                }
                const auto rows = kind == "alert" ? json.object().value("items").toArray() : json.array();
                for (const auto row : rows) {
                    const auto entry = indexRow(site, kind, row.toObject());
                    if (!entry.isEmpty())
                        index_.upsert(entry);
                }
                const auto cursor = json.object().value("cursor").toString();
                if (kind == "alert" && !cursor.isEmpty())
                    followAlerts(site, cursor);
                if (kind == "layout")
                    emit layoutsChanged();
                emit commandObjectsChanged();
            });
        }
    }
}

QVariantMap Workspace::indexRow(const QString& site, const QString& kind, const QJsonObject& row) const
{
    const auto id = row.value("id").toString();
    if (id.isEmpty())
        return {};
    QString name;
    if (kind == "alert") {
        const auto point = points_.lookup(row.value("point_id").toString());
        if (point.value("site_id") != site)
            return {};
        name = point.value("name").toString() + " " + row.value("observed_at").toString();
    } else
        name = row.value(kind == "case" ? "number" : "name").toString();
    return {{"kind", kind}, {"id", id}, {"site", site}, {"name", name}, {"data", row.toVariantMap()}};
}
void Workspace::followAlerts(const QString& site, const QString& cursor)
{
    auto* socket = new QWebSocket(QString{}, QWebSocketProtocol::VersionLatest, this);
    feeds_.insert(site, socket);
    auto req = request("/api/v1/sites/" + QUrl::toPercentEncoding(site) + "/alerts/stream");
    auto url = req.url();
    url.setScheme("wss");
    req.setUrl(url);
    socket->setSslConfiguration(req.sslConfiguration());
    socket->setMaxAllowedIncomingMessageSize(1024 * 1024);
    connect(socket, &QWebSocket::connected, this, [socket, cursor] {
        socket->sendTextMessage(QString::fromUtf8(
            QJsonDocument(QJsonObject{{"type", "subscribe"}, {"after", cursor}}).toJson(QJsonDocument::Compact)));
    });
    connect(socket, &QWebSocket::textMessageReceived, this, [this, site, socket](const QString& text) {
        const auto frame = QJsonDocument::fromJson(text.toUtf8()).object();
        const auto type = frame.value("type").toString();
        if (type == "resync_required" && frame.value("reason").toString() == "scope_changed") {
            refresh();
            return;
        }
        if (type != "upsert" && type != "remove") {
            socket->close();
            return;
        }
        const auto alert = frame.value("alert").toObject();
        const auto id = type == "remove" ? frame.value("alert_id").toString() : alert.value("id").toString();
        if (id.isEmpty() || frame.value("cursor").toString().isEmpty()) {
            socket->close();
            return;
        }
        index_.remove("alert/" + site + "/" + id);
        if (type == "upsert") {
            const auto entry = indexRow(site, "alert", alert);
            if (!entry.isEmpty())
                index_.upsert(entry);
        }
        emit commandObjectsChanged();
    });
    auto reconnect = [this, site, socket, generation = indexGeneration_] {
        if (feeds_.value(site) != socket)
            return;
        feeds_.remove(site);
        socket->disconnect(this);
        socket->deleteLater();
        index_.removeSiteAlerts(site);
        emit commandObjectsChanged();
        QTimer::singleShot(1500, this, [this, site, generation] {
            if (generation == indexGeneration_ && state() == Ready) {
                loaded_.remove(site + "/alert");
                loadCommandObjects();
            }
        });
    };
    connect(socket, &QWebSocket::disconnected, this, reconnect);
    connect(socket, &QWebSocket::errorOccurred, this, reconnect);
    QWebSocketHandshakeOptions options;
    options.setSubprotocols({"bacuri.vms.alerts.v1"});
    socket->open(req, options);
}

void Workspace::releaseMedia(const QString& session)
{
    if (!signedIn() || session.isEmpty())
        return;
    auto* reply = network_.deleteResource(request("/api/v1/media-sessions/" + QUrl::toPercentEncoding(session)));
    connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
}
QNetworkReply* Workspace::offerMedia(const QString& session, const QByteArray& sdp)
{
    if (!signedIn() || state() != Ready || session.isEmpty())
        return nullptr;
    auto req = request("/api/v1/media-sessions/" + QUrl::toPercentEncoding(session) + "/offer");
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/sdp");
    return network_.post(req, sdp);
}
QNetworkReply* Workspace::pointRequest(const QString& id, const QString& action, const QUrlQuery& query,
                                       const QJsonObject& body)
{
    const auto point = points_.lookup(id);
    const auto site = point.value("site_id").toString();
    if (!signedIn() || state() != Ready || site.isEmpty())
        return nullptr;
    if (action != "archive" && action != "ptz" && action != "exports" && action != "media-sessions" &&
        action != "snapshot-receipts")
        return nullptr;
    if (action == "snapshot-receipts" && !point.value("can_snapshot").toBool())
        return nullptr;
    if (action == "exports" && !point.value("can_export").toBool())
        return nullptr;
    if (action == "ptz" && point.value("ptz").toMap().isEmpty())
        return nullptr;
    auto req = request("/api/v1/sites/" + QUrl::toPercentEncoding(site) + "/points/" +
                       QUrl::toPercentEncoding(id) + "/" + action);
    auto url = req.url();
    url.setQuery(query);
    req.setUrl(url);
    QNetworkReply* reply{};
    if (body.isEmpty())
        reply = network_.get(req);
    else {
        req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        if (action == "ptz" || action == "media-sessions" || action == "snapshot-receipts")
            req.setRawHeader("Idempotency-Key", QUuid::createUuid().toString(QUuid::WithoutBraces).toLatin1());
        reply = network_.post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    }
    connect(reply, &QNetworkReply::finished, this, [this, reply, token = session_.token] {
        if (session_.token != token || state_ != Ready)
            return;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 401) {
            session_ = {};
            points_.apply({});
            update(Failed, "unauthenticated");
        } else if (status == 403)
            refresh();
    });
    return reply;
}

} // namespace bacuri::vms::client
