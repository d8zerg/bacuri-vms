#pragma once
#include "app/navigation.hpp"
#include "app/workspace.hpp"
#include <QQuickWindow>
#include <QShortcut>

namespace bacuri::vms::client
{
class CommandRegistry : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QVariantMap entries READ entries NOTIFY changed FINAL)
  public:
    using QObject::QObject;
    Q_INVOKABLE void attach(QQuickWindow* window, Navigation* navigation, Workspace* workspace);
    Q_INVOKABLE void invoke(const QString& id);
    QVariantMap entries() const;
  signals:
    void triggered(QString id);
    void changed();

  private:
    bool available(const QString& id) const;
    void update();
    Workspace* workspace_{};
    Navigation* navigation_{};
    QHash<QString, QShortcut*> shortcuts_;
};
} // namespace bacuri::vms::client
