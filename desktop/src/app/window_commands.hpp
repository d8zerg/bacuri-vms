#pragma once
#include "app/navigation.hpp"
#include "commands/window_context.hpp"
#include "archive/archive_commands.hpp"
#include "live/live_commands.hpp"
#include "app/workspace.hpp"
#include <QDateTime>
#include <QNetworkReply>
#include <QQuickWindow>
#include <QShortcut>
#include <QTimer>
#include <QScreen>

namespace bacuri::vms::client
{
class WindowCommands : public QObject, private WindowContext
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QVariantList radialActions READ radial NOTIFY changed FINAL)
    Q_PROPERTY(QStringList commandIds READ commandIds NOTIFY changed FINAL)
    Q_PROPERTY(QVariantMap entries READ entries NOTIFY changed FINAL)
    Q_PROPERTY(bool modal MEMBER modal_ NOTIFY modalChanged FINAL)
    Q_PROPERTY(QQuickItem* frame READ frame WRITE setFrame NOTIFY frameChanged FINAL)
    Q_PROPERTY(QVariantList ptzOptions READ ptzOptions NOTIFY changed FINAL)
    Q_PROPERTY(QVariantList presets READ presets NOTIFY changed FINAL)
    Q_PROPERTY(bool hints READ hints NOTIFY hintsChanged FINAL)
    Q_PROPERTY(QString archiveTime READ archiveTime NOTIFY archiveTimeChanged FINAL)
    Q_PROPERTY(QVariantMap layout READ layout NOTIFY layoutChanged FINAL)
  public:
    using QObject::QObject;
    static void registerCommands(CommandRegistry&);
    QQuickItem* frame() const
    {
        return frame_;
    }
    Q_INVOKABLE void publishKeymap();
    bool hints() const
    {
        return hints_;
    }
    Q_INVOKABLE bool hasPresets() const
    {
        return !live_.presets().isEmpty();
    }
    QVariantList presets() const
    {
        return live_.presets();
    }
    QVariantList ptzOptions() const
    {
        return live_.ptzOptions();
    }

    Q_INVOKABLE void confirmPreset(int number)
    {
        live_.confirmPreset(number);
    }
    QVariantMap gestureCounts() const;
    Q_INVOKABLE void recordGesture(const QString& action, bool stroke);
    Q_INVOKABLE bool temporaryLayout(const QStringList& keys, int screen);
    Q_INVOKABLE void startMotion(const QString& id, const QString& command)
    {
        live_.startMotion(id, command);
    }
    Q_INVOKABLE void stopMotion();
    void setFrame(QQuickItem* item);
    Q_INVOKABLE void attach(QQuickWindow* window, Navigation* navigation, Workspace* workspace);
    Q_INVOKABLE void invoke(const QString& id);
    Q_INVOKABLE void openPalette();
    Q_INVOKABLE QVariantList search(const QString& query) const;
    Q_INVOKABLE QVariantMap argument(const QString& key, const QString& query) const;
    Q_INVOKABLE void choose(const QString& key, const QString& parameter = {});
    Q_INVOKABLE QString setBinding(const QString& id, const QString& sequence, bool restoreDefault = false);
    Q_INVOKABLE QString keyName(int key, int modifiers) const;
    Q_INVOKABLE QVariantList radial() const;
    Q_INVOKABLE QString direction(double x, double y) const;
    Q_INVOKABLE void pointAction(const QString& action, const QString& id, const QString& start,
                                 const QString& end = {}, const QString& argument = {});
    Q_INVOKABLE void applyLayout(const QString& key, const QString& screen);
    Q_INVOKABLE QVariantList screens() const;
    Q_INVOKABLE QString ptzName(const QString& command) const
    {
        return LiveCommands::ptzName(command);
    }
    Q_INVOKABLE QString intervalEnd(const QString& start, const QString& id) const;
    Q_INVOKABLE QString now() const;
    QStringList commandIds() const;
    QVariantMap entries() const;
    QVariantMap layout() const;
    QString archiveTime() const
    {
        return archive_.time();
    }
    Q_INVOKABLE void cancelRead()
    {
        archive_.cancel();
    }
  signals:
    void hintsChanged();
    void triggered(QString id);
    void tool(QString kind, QVariantMap data) override;
    void pointChosen(QString id, bool live);
    void submitted() override;
    void layoutApplied(QString id);
    void layoutChanged();
    void archiveTimeChanged();
    void modalChanged();
    void frameChanged();
    void changed();
    void resultsChanged();
    void feedback(QString message) override;
    void archiveLoaded(QString id, QString at, QVariantList intervals);

  private:
    friend void installShellCommands(CommandRegistry&);
    SiteAccess& site() const override
    {
        return *workspace_;
    }
    QString focusedCamera() const override;
    QQuickItem* frameFor(const QVariantMap& point) const override;
    bool eventFilter(QObject* obj, QEvent* event) override;
    void setHints(bool show);
    void saveView();
    void undoView();
    struct View {
        QVariantList navigation;
        QPointer<QScreen> screen;
        QRect geometry;
        QWindow::Visibility visibility;
        QVariantMap layout;
        QString archiveTime;
        QString alert;
    };
    QList<View> history_;
    QVariantMap temporaryLayout_;
    QTimer ctrlTimer_;
    bool hints_{};
    CommandScope focusedScope() const;
    int motionKey_{};
    bool available(const QString& id) const;
    void syncBindings();
    void update();
    void updateShortcuts();
    bool pointAvailable(const QString& action, const QVariantMap& point) const;
    QVariantMap object(const QString& key) const;
    void remember(const QString& key, const QString& site = {});
    QString focusedAlert_;
    Workspace* workspace_{};
    Navigation* navigation_{};
    QQuickWindow* window_{};
    QPointer<QQuickItem> frame_;
    QString framePoint_;
    QVariant frameRevision_;
    QMetaObject::Connection frameDestroyed_;
    bool modal_{};
    QHash<QString, QShortcut*> shortcuts_;
    QSet<QString> customized_;
    CommandRegistry* catalog_{};
    QString profile_;
    QWindow::Visibility previousVisibility_{QWindow::Windowed};
    mutable ArchiveCommands archive_{*this};
    mutable LiveCommands live_{*this};
};
} // namespace bacuri::vms::client
