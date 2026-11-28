#include "export/export_jobs.hpp"
#include "site/site_access.hpp"
#include <QDir>
#include <QCoreApplication>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QUuid>

namespace bacuri::vms::client
{
namespace
{
QString label(const char* text)
{
    return QCoreApplication::translate("Shell", text);
}
} // namespace
ExportJobs::ExportJobs(SiteAccess* workspace) : QObject(workspace), workspace_(workspace)
{
    connect(workspace, &SiteAccess::accessChanged, this, [this] {
        if (!workspace_->signedIn() || workspace_->signingOut()) {
            const auto jobs = std::exchange(jobs_, {});
            for (const auto& job : jobs) {
                job->state = "cancelled";
                if (job->reply)
                    job->reply->abort();
            }
            emit changed();
        } else if (workspace_->ready()) {
            for (const auto& job : jobs_)
                if (job->reply && !allowed(*job))
                    cancel(job->id);
            next();
        }
    });
}
bool ExportJobs::allowed(const Job& job) const
{
    const auto p = workspace_->point(job.point);
    return workspace_->signedIn() && workspace_->profile(job.point) == job.profile &&
           p.value("can_export").toBool() && p.value("access_revision") == job.revision;
}
bool ExportJobs::busy() const
{
    return std::ranges::any_of(jobs_, [](const auto& j) { return j->state == "queued" || j->state == "running"; });
}
QVariantList ExportJobs::rows() const
{
    QVariantList result;
    for (const auto& j : jobs_)
        result.append(QVariantMap{{"id", j->id},
                                  {"point", j->name},
                                  {"start", j->start},
                                  {"end", j->end},
                                  {"state", j->state},
                                  {"received", j->received},
                                  {"total", j->total},
                                  {"path", j->path},
                                  {"error", j->error}});
    return result;
}
bool ExportJobs::enqueue(const QVariantMap& point, const QString& startTime, const QString& endTime)
{
    auto j = std::make_shared<Job>();
    j->id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    j->point = point.value("id").toString();
    j->name = point.value("name").toString();
    j->profile = workspace_->profile(j->point);
    j->revision = point.value("access_revision");
    j->start = startTime;
    j->end = endTime;
    j->state = "queued";
    if (!allowed(*j))
        return false;
    jobs_.append(j);
    next();
    return true;
}
void ExportJobs::cancel(const QString& id)
{
    for (const auto& j : jobs_)
        if (j->id == id && (j->state == "queued" || j->state == "running")) {
            j->state = "cancelled";
            if (j->reply)
                j->reply->abort();
            emit changed();
            return;
        }
}
void ExportJobs::retry(const QString& id)
{
    for (const auto& j : jobs_)
        if (j->id == id && (j->state == "failed" || j->state == "cancelled") && allowed(*j)) {
            j->state = "queued";
            j->error.clear();
            j->received = 0;
            j->total = -1;
            next();
            return;
        }
}
void ExportJobs::next()
{
    if (workspace_->ready() && !std::ranges::any_of(jobs_, [](const auto& j) { return !j->reply.isNull(); })) {
        for (const auto& j : jobs_)
            if (j->state == "queued") {
                if (!allowed(*j)) {
                    j->state = "cancelled";
                    continue;
                }
                start(j);
                break;
            }
    }
    emit changed();
}
void ExportJobs::start(const std::shared_ptr<Job>& j)
{
    const auto dir = qEnvironmentVariable("BACURI_EXPORT_DIRECTORY",
                                          QStandardPaths::writableLocation(QStandardPaths::DownloadLocation));
    const auto path = QDir(dir).filePath("Bacuri-" + j->id + ".tar");
    auto file = std::make_shared<QSaveFile>(path);
    if (dir.isEmpty() || !QDir().mkpath(dir) || !file->open(QIODevice::WriteOnly)) {
        j->state = "failed";
        j->error = label("Could not save the file.");
        QMetaObject::invokeMethod(this, &ExportJobs::next, Qt::QueuedConnection);
        return;
    }
    auto* reply = workspace_->pointRequest(
        j->point, "exports", {},
        {{"mode", "selection"}, {"interval", QJsonObject{{"start", j->start}, {"end", j->end}}}});
    if (!reply) {
        j->state = "failed";
        j->error = label("This action is unavailable.");
        QMetaObject::invokeMethod(this, &ExportJobs::next, Qt::QueuedConnection);
        return;
    }
    j->state = "running";
    j->reply = reply;
    reply->setReadBufferSize(256 * 1024);
    auto drain = [reply, file, j] {
        if (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != 200)
            return;
        while (reply->bytesAvailable() > 0) {
            const auto bytes = reply->read(64 * 1024);
            if (bytes.isEmpty() || file->write(bytes) != bytes.size()) {
                j->error = label("Could not save the file.");
                file->cancelWriting();
                reply->abort();
                break;
            }
        }
    };
    connect(reply, &QIODevice::readyRead, this, drain);
    connect(reply, &QNetworkReply::downloadProgress, this, [this, j](qint64 received, qint64 total) {
        j->received = received;
        j->total = total;
        emit changed();
    });
    connect(reply, &QNetworkReply::finished, this, [this, j, reply, file, drain, path] {
        j->reply.clear();
        reply->deleteLater();
        if (j->state == "running" && allowed(*j)) {
            const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (status == 200 && reply->error() == QNetworkReply::NoError)
                drain();
            if (j->error.isEmpty() && status == 200 && reply->error() == QNetworkReply::NoError &&
                reply->header(QNetworkRequest::ContentTypeHeader).toString().section(';', 0, 0) ==
                    "application/x-tar" &&
                file->size() > 0 && file->commit()) {
                j->state = "complete";
                j->path = path;
                emit feedback(label("Saved to %1").arg(path));
            } else {
                if (j->error.isEmpty()) {
                    auto code = QJsonDocument::fromJson(reply->readAll()).object().value("code").toString();
                    j->error = code.isEmpty() ? label("The site could not complete the action.")
                                              : workspace_->describeError(code);
                }
                j->state = "failed";
                emit feedback(j->error);
            }
        } else if (j->state != "queued")
            j->state = "cancelled";
        next();
    });
}
} // namespace bacuri::vms::client
