#pragma once

#include "commands/window_context.hpp"

#include <QPointer>

namespace bacuri::vms::client
{
class ArchiveCommands final : public QObject
{
    Q_OBJECT
  public:
    explicit ArchiveCommands(WindowContext& context) : context_(context) {}
    ~ArchiveCommands() override;
    static void install(CommandRegistry&, ContextAccess, FeatureAccess<ArchiveCommands>);
    QString time() const
    {
        return time_;
    }
    void setTime(const QString&);
    QVariantList marks() const
    {
        return marks_;
    }
    void clearMarks()
    {
        marks_.clear();
    }
    void clear();
    void cancel();

  signals:
    void timeChanged();
    void marksChanged();
    void loaded(QString id, QString at, QVariantList intervals);

  private:
    void open(const CommandInput&, bool showIntervals = true);
    void mark(const CommandInput&);
    WindowContext& context_;
    QString time_;
    QVariantList marks_;
    QPointer<QNetworkReply> read_;
};
} // namespace bacuri::vms::client
