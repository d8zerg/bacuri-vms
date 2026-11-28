#pragma once

#include <QQmlEngine>
#include <QSettings>
#include <QTranslator>

#include <QColor>
#include <QDBusVariant>
#include <QHash>

namespace bacuri::vms::client
{
class Preferences final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY languageChanged FINAL)
    Q_PROPERTY(QString theme READ theme WRITE setTheme NOTIFY themeChanged FINAL)
    Q_PROPERTY(bool dark READ dark NOTIFY appearanceChanged FINAL)
    Q_PROPERTY(bool highContrast READ highContrast NOTIFY appearanceChanged FINAL)
    Q_PROPERTY(QColor accent READ accent NOTIFY appearanceChanged FINAL)
    Q_PROPERTY(bool motion READ motion NOTIFY appearanceChanged FINAL)
    Q_PROPERTY(double textScale READ textScale WRITE setTextScale NOTIFY appearanceChanged FINAL)
    Q_PROPERTY(double systemTextScale READ systemTextScale NOTIFY appearanceChanged FINAL)

  public:
    static Preferences* create(QQmlEngine* engine, QJSEngine*);
    explicit Preferences(QQmlEngine* engine);
    ~Preferences() override;
    QString language() const;
    QString theme() const;
    bool dark() const;
    bool motion() const;
    bool highContrast() const;
    QColor accent() const;
    double textScale() const;
    double systemTextScale() const;
    void setLanguage(const QString& value);
    void setTheme(const QString& value);
    void setTextScale(double value);

  signals:
    void languageChanged();
    void themeChanged();
    void appearanceChanged();

  private:
    QQmlEngine* engine_;
    QSettings settings_;
    QTranslator translator_;
    bool eventFilter(QObject*, QEvent*) override;
    void readPortal();
    void applyLanguage(const QString& value);
    QVariantMap portal_;
    QHash<QString, quint64> revisions_;
    quint64 generation_{};
  private slots:
    void settingChanged(const QString& group, const QString& key, const QDBusVariant& value);

  private:
    QString language_;
    QString theme_;
    double text_scale_{1};
};
} // namespace bacuri::vms::client
