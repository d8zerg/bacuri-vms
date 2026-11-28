#pragma once
#include <QQmlEngine>
#include <QQuickItem>
#include <QVariantList>

namespace bacuri::vms::client
{
class Navigation : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool containsPoint READ containsPoint NOTIFY changed FINAL)
    Q_PROPERTY(Page page READ page NOTIFY changed FINAL)
    Q_PROPERTY(QString pointId READ pointId NOTIFY changed FINAL)
    Q_PROPERTY(QVariantMap focusTarget READ focusTarget NOTIFY changed FINAL)
  public:
    enum Page { Overview, Point, Settings };
    Q_ENUM(Page)
    using QObject::QObject;
    Page page() const;
    bool containsPoint() const;
    Q_INVOKABLE QQuickItem* findTarget(QQuickItem* parent, const QString& id) const;
    QString pointId() const;
    QVariantMap focusTarget() const
    {
        return stack_.last().toMap().value("focus").toMap();
    }
    Q_INVOKABLE QVariantList save(const QVariantMap& currentFocus = {}) const;
    Q_INVOKABLE bool restore(const QVariantList& intents);
    Q_INVOKABLE void push(Page page, const QString& id, const QVariantMap& returnFocus);
    Q_INVOKABLE void back();
    Q_INVOKABLE void reset();
  signals:
    void changed();

  private:
    static QVariantMap intent(Page page, const QString& id = {});
    QVariantList stack_{intent(Overview)};
};
} // namespace bacuri::vms::client
