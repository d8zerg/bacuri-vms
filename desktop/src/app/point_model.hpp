#pragma once
#include <QAbstractListModel>
#include <QQmlEngine>
#include <QSortFilterProxyModel>
#include <QVariantMap>

namespace bacuri::vms::client
{
class SearchMatcher
{
  public:
    explicit SearchMatcher(const QString& query = {});
    bool matches(const QString& foldedText) const;

  private:
    QList<QStringList> alternatives_;
};

class PointModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by the workspace")
    Q_PROPERTY(int count READ count NOTIFY revised FINAL)
    Q_PROPERTY(quint64 version READ version NOTIFY revised FINAL)
  public:
    enum Role { Point = Qt::UserRole + 1, SearchText };
    using QAbstractListModel::QAbstractListModel;
    int rowCount(const QModelIndex& parent = {}) const override
    {
        return parent.isValid() ? 0 : count();
    }
    int count() const
    {
        return static_cast<int>(records_.size());
    }
    quint64 version() const
    {
        return version_;
    }
    QVariant data(const QModelIndex&, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    Q_INVOKABLE QVariantMap lookup(const QString& id) const;
    int rowFor(const QString& id) const
    {
        return rows_.value(id, -1);
    }
    void apply(const QVariantList& snapshot);
  signals:
    void revised();
    void statusChanged(QString id, QString name, QString connection, QString recording);

  private:
    struct Record {
        QString id;
        QVariantMap point;
        QString search;
    };
    QList<Record> records_;
    QHash<QString, int> rows_;
    quint64 version_{};
};

class PointFilterModel : public QSortFilterProxyModel
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString query READ query WRITE setQuery NOTIFY filtersChanged FINAL)
    Q_PROPERTY(QVariantMap criteria READ criteria WRITE setCriteria NOTIFY filtersChanged FINAL)
    Q_PROPERTY(int count READ count NOTIFY countChanged FINAL)
  public:
    explicit PointFilterModel(QObject* parent = nullptr);
    int count() const
    {
        return rowCount();
    }
    QString query() const
    {
        return query_;
    }
    QVariantMap criteria() const
    {
        return criteria_;
    }
    void setQuery(const QString&);
    void setCriteria(const QVariantMap&);
    Q_INVOKABLE QString idAt(int row) const;
    Q_INVOKABLE int rowFor(const QString& id) const;
  signals:
    void filtersChanged();
    void countChanged();

  protected:
    bool filterAcceptsRow(int row, const QModelIndex& parent) const override;

  private:
    QString query_;
    SearchMatcher matcher_;
    QVariantMap criteria_;
};
} // namespace bacuri::vms::client
