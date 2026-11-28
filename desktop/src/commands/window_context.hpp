#pragma once

#include "commands/command_registry.hpp"
#include "site/site_access.hpp"

class QQuickItem;

namespace bacuri::vms::client
{
class WindowContext
{
  public:
    virtual ~WindowContext() = default;
    virtual SiteAccess& site() const = 0;
    virtual QString focusedCamera() const = 0;
    virtual QQuickItem* frameFor(const QVariantMap& point) const = 0;
    virtual void feedback(QString message) = 0;
    virtual void tool(QString kind, QVariantMap data) = 0;
    virtual void submitted() = 0;

    QVariantMap camera() const
    {
        const auto id = focusedCamera();
        return id.isEmpty() ? QVariantMap{} : site().point(id);
    }
    QDateTime parseTime(const QString& input, const QString& point);
};

template <class T> using FeatureAccess = T& (*)(const WindowCommands&);
using ContextAccess = FeatureAccess<WindowContext>;
using CameraAccess = std::function<bool(const WindowCommands&, const QVariantMap&)>;
void addCameraCommand(CommandRegistry&, ContextAccess, QString id, const char* name, QString icon,
                      QList<QKeySequence> keys, ParameterType, std::function<void(WindowCommands&)> run = {},
                      CommandHandler submit = {}, CameraAccess permits = {});
} // namespace bacuri::vms::client
