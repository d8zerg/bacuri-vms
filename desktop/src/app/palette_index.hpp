#pragma once
#include <QVariantList>
#include <QHash>
#include <QDateTime>
namespace bacuri::vms::client
{
struct PaletteEntry {
    QString kind, key, name, icon, folded, id, point, site;
    qint64 observedAt{};
};
class PaletteQuery
{
  public:
    explicit PaletteQuery(const QString& text);
    int score(const QString& folded, QList<int>* positions = nullptr) const;
    bool empty() const
    {
        return terms_.isEmpty();
    }

  private:
    QList<QStringList> terms_;
};
class PaletteIndex
{
  public:
    void rebuild(const QVariantList& objects);
    void upsert(const QVariantMap& object);
    void remove(const QString& key);
    void removeSiteAlerts(const QString& site);
    QVariantMap object(const QString& key) const
    {
        return data_.value(key);
    }
    void setPoints(const QVariantList& points);
    const QHash<QString, PaletteEntry>& objects() const
    {
        return objects_;
    }
    const QList<PaletteEntry>& points() const
    {
        return points_;
    }

  private:
    QHash<QString, PaletteEntry> objects_;
    QHash<QString, QVariantMap> data_;
    QList<PaletteEntry> points_;
};
} // namespace bacuri::vms::client
