// Signs the desktop client in to a site through the system browser, reads the session with the
// bearer token and signs out. Writes a JSON report without the token.

#include "site/sign_in.hpp"

#include <QCommandLineParser>
#include <QFile>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>

#include <cstdio>

namespace
{
using namespace bacuri::vms::client;

QNetworkRequest authorized(const SignInOptions& options, const QString& path, const QString& token)
{
    QNetworkRequest request{options.site.resolved(QUrl{path})};
    request.setRawHeader("Authorization", "Bearer " + token.toLatin1());
    request.setSslConfiguration(site_tls(options.ca_file));
    return request;
}
} // namespace

int main(int argc, char* argv[])
{
    QGuiApplication application(argc, argv);
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOptions({
        {"site", "Origin of the site.", "url"},
        {"ca", "Certificates trusted for the site.", "file"},
        {"browser", "Command that opens the confirmation page instead of the default browser.", "command"},
        {"timeout", "Seconds to wait for the confirmation.", "seconds", "300"},
        {"report", "Path of the JSON report.", "file"},
    });
    parser.process(application);
    const SignInOptions options{QUrl{parser.value("site")}, parser.value("ca"), parser.value("browser"),
                                parser.value("timeout").toInt()};
    if (!options.site.isValid() || options.site.scheme() != "https") {
        std::fputs("Expected --site with an https origin\n", stderr);
        return 2;
    }

    QNetworkAccessManager network;
    QJsonObject report;
    const auto finish = [&](int status) {
        const auto json = QJsonDocument{report}.toJson();
        if (parser.isSet("report")) {
            QFile file{parser.value("report")};
            if (!file.open(QIODevice::WriteOnly) || file.write(json) != json.size())
                status = 1;
        } else {
            std::fwrite(json.constData(), 1, static_cast<std::size_t>(json.size()), stdout);
        }
        QGuiApplication::exit(status);
    };

    SignIn sign_in{options};
    QObject::connect(&sign_in, &SignIn::failed, &application, [&](const QString& cause) {
        report.insert("error", cause);
        finish(1);
    });
    QObject::connect(&sign_in, &SignIn::signed_in, &application, [&](const DesktopSession& session) {
        report.insert("user_id", session.user_id);
        report.insert("site_roles", session.site_roles);
        report.insert("expires_at", session.expires_at);
        report.insert("token_format", session.token.size() == 67 ? "d1 and 64 hexadecimal digits" : "unexpected");
        auto* current = network.get(authorized(options, "/api/v1/session", session.token));
        QObject::connect(current, &QNetworkReply::finished, &application, [&, current, token = session.token] {
            current->deleteLater();
            const auto status = current->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const auto body = QJsonDocument::fromJson(current->readAll()).object();
            report.insert("session_status", status);
            report.insert("session_user_matches",
                          body.value("user_id").toString() == report.value("user_id").toString());
            auto* revoke = network.deleteResource(authorized(options, "/api/v1/session", token));
            QObject::connect(revoke, &QNetworkReply::finished, &application, [&, revoke, token] {
                revoke->deleteLater();
                report.insert("sign_out_status",
                              revoke->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt());
                auto* after = network.get(authorized(options, "/api/v1/session", token));
                QObject::connect(after, &QNetworkReply::finished, &application, [&, after] {
                    after->deleteLater();
                    const auto ended = after->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                    report.insert("status_after_sign_out", ended);
                    finish(report.value("session_status").toInt() == 200 && ended == 401 ? 0 : 1);
                });
            });
        });
    });
    sign_in.start();
    return QGuiApplication::exec();
}
