#pragma once

#include "app/workspace.hpp"

#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQuickWindow>

namespace bacuri::vms::client
{
class WindowGroup final : public QObject
{
    Q_OBJECT
  public:
    WindowGroup(SignInOptions options, bool catalog);
    QQuickWindow* open(const QVariantList& intents = {});
    void finish();
    QQmlApplicationEngine& engine()
    {
        return engine_;
    }

  private slots:
    void openWindow()
    {
        open();
    }

  private:
    Workspace workspace_;
    QQmlApplicationEngine engine_;
    QQmlComponent shell_;
    bool show_catalog_;
    bool waitingForExports_{};
};
} // namespace bacuri::vms::client
