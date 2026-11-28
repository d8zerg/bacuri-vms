#pragma once

#include "commands/window_context.hpp"

#include <QPointer>
#include <QSet>
#include <QTimer>

namespace bacuri::vms::client
{
class LiveCommands final : public QObject
{
    Q_OBJECT
  public:
    explicit LiveCommands(WindowContext& context);
    ~LiveCommands() override;
    static void install(CommandRegistry&, ContextAccess, FeatureAccess<LiveCommands>);
    void readCameraControls();
    void confirmPreset(int number);
    QVariantList ptzOptions() const;
    QVariantList presets() const
    {
        return presets_;
    }
    void startMotion(const QString& id, const QString& direction);
    void stopMotion();
    bool moving() const
    {
        return !motionPoint_.isEmpty();
    }
    void cancelCommands();
    static QString ptzName(const QString& command);

  signals:
    void changed();
    void motionStopped();

  private:
    bool canMove(const QString& direction) const;
    bool canCapture(const QVariantMap& point) const;
    void snapshot(const QString& point);
    void sendPtz(const CommandInput& input);
    void sendMotion();
    WindowContext& context_;
    QString controlsKey_;
    QPointer<QNetworkReply> controlsRead_;
    QVariantList cameraActions_, presets_;
    QTimer motionTimer_;
    quint64 motionGeneration_{};
    QString motionDirection_, motionPoint_, motionDevice_;
    QSet<QNetworkReply*> jobs_;
};
} // namespace bacuri::vms::client
