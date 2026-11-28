#pragma once
#include "commands/window_context.hpp"
#include <QNetworkReply>
#include <QPointer>
#include <QSaveFile>
#include <memory>

namespace bacuri::vms::client
{
class ExportJobs final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList rows READ rows NOTIFY changed FINAL)
  public:
    explicit ExportJobs(SiteAccess* workspace);
    bool enqueue(const QVariantMap& point, const QString& start, const QString& end);
    bool busy() const;
    QVariantList rows() const;
    Q_INVOKABLE void cancel(const QString& id);
    Q_INVOKABLE void retry(const QString& id);
  signals:
    void changed();
    void feedback(QString text);

  private:
    struct Job {
        QString id, point, name, profile, start, end, state, path, error;
        QVariant revision;
        qint64 received{}, total{-1};
        QPointer<QNetworkReply> reply;
    };
    bool allowed(const Job& job) const;
    void next();
    void start(const std::shared_ptr<Job>& job);
    SiteAccess* workspace_;
    QList<std::shared_ptr<Job>> jobs_;
};
void installExportCommands(CommandRegistry&, ContextAccess, FeatureAccess<ExportJobs>);
} // namespace bacuri::vms::client
