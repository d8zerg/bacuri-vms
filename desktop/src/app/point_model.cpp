#include "app/point_model.hpp"
#include <QRegularExpression>
#include <QSet>
#include <algorithm>

namespace bacuri::vms::client
{
QString otherLayout(const QString& word)
{
    static const QString en = "`qwertyuiop[]asdfghjkl;'zxcvbnm,.";
    static const QString ru = "ёйцукенгшщзхъфывапролджэячсмитьбю";
    QString result;
    for (auto ch : word) {
        auto i = en.indexOf(ch);
        if (i >= 0)
            result += ru[i];
        else if ((i = ru.indexOf(ch)) >= 0)
            result += en[i];
        else
            result += ch;
    }
    return result;
}
bool oneEdit(QStringView a, QStringView b)
{
    if (a.size() > b.size())
        std::swap(a, b);
    if (b.size() - a.size() > 1)
        return false;
    qsizetype i = 0;
    while (i < a.size() && a[i] == b[i])
        ++i;
    if (i == a.size())
        return true;
    if (a.size() != b.size())
        return a.sliced(i) == b.sliced(i + 1);
    if (a.sliced(i + 1) == b.sliced(i + 1))
        return true;
    return i + 1 < a.size() && a[i] == b[i + 1] && a[i + 1] == b[i] && a.sliced(i + 2) == b.sliced(i + 2);
}
SearchMatcher::SearchMatcher(const QString& query)
{
    for (const auto& term : query.simplified().toCaseFolded().split(' ', Qt::SkipEmptyParts))
        alternatives_.append(QStringList{term, otherLayout(term)});
}
bool SearchMatcher::matches(const QString& text) const
{
    static const QRegularExpression separator(QStringLiteral("[^\\p{L}\\p{N}]+"));
    QStringList words;
    for (const auto& alternatives : alternatives_) {
        if (std::ranges::any_of(alternatives, [&](const QString& s) { return text.contains(s); }))
            continue;
        if (alternatives.first().size() < 4)
            return false;
        if (words.isEmpty())
            words = text.split(separator, Qt::SkipEmptyParts);
        if (!std::ranges::any_of(words, [&](const QString& word) {
                return std::ranges::any_of(alternatives, [&](const QString& term) { return oneEdit(term, word); });
            }))
            return false;
    }
    return true;
}
QHash<int, QByteArray> PointModel::roleNames() const
{
    return {{Point, "point"}, {SearchText, "searchText"}};
}
QVariant PointModel::data(const QModelIndex& idx, int role) const
{
    if (!checkIndex(idx, CheckIndexOption::IndexIsValid | CheckIndexOption::ParentIsInvalid))
        return {};
    const auto& rec = records_[idx.row()];
    if (role == Point)
        return rec.point;
    if (role == SearchText)
        return rec.search;
    return {};
}
QVariantMap PointModel::lookup(const QString& id) const
{
    const int row = rowFor(id);
    return row < 0 ? QVariantMap{} : records_[row].point;
}
void PointModel::apply(const QVariantList& snapshot)
{
    QSet<QString> ids;
    for (const auto& value : snapshot)
        ids.insert(value.toMap().value("id").toString());
    bool changed = false;
    for (int end = count(); end > 0;) {
        if (ids.contains(records_[end - 1].id)) {
            --end;
            continue;
        }
        int first = end - 1;
        while (first > 0 && !ids.contains(records_[first - 1].id))
            --first;
        beginRemoveRows({}, first, end - 1);
        for (int i = first; i < end; ++i)
            rows_.remove(records_[i].id);
        records_.remove(first, end - first);
        for (int i = first; i < count(); ++i)
            rows_[records_[i].id] = i;
        endRemoveRows();
        changed = true;
        end = first;
    }
    QList<Record> added;
    QSet<QString> seen;
    for (const auto& value : snapshot) {
        const auto point = value.toMap();
        const auto id = point.value("id").toString();
        if (id.isEmpty() || seen.contains(id))
            continue;
        seen.insert(id);
        const int row = rowFor(id);
        if (row >= 0 && records_[row].point == point)
            continue;
        Record rec{id, point, (point.value("name").toString() + " " + id).toCaseFolded()};
        if (row < 0)
            added.append(std::move(rec));
        else {
            const auto previous = records_[row].point;
            records_[row] = std::move(rec);
            const auto difference = [&](const QString& key) {
                return previous.value(key) == point.value(key) ? QString{}
                                                               : point.value(key, "unknown").toString();
            };
            const auto connection = difference("connection"), recording = difference("recording");
            if (!connection.isEmpty() || !recording.isEmpty())
                emit statusChanged(id, point.value("name").toString(), connection, recording);
            emit dataChanged(index(row), index(row), {Point, SearchText});
        }
        changed = true;
    }
    if (!added.isEmpty()) {
        const int first = count();
        beginInsertRows({}, first, first + static_cast<int>(added.size()) - 1);
        records_.append(std::move(added));
        for (int i = first; i < count(); ++i)
            rows_.insert(records_[i].id, i);
        endInsertRows();
    }
    if (changed) {
        ++version_;
        emit revised();
    }
}
PointFilterModel::PointFilterModel(QObject* parent) : QSortFilterProxyModel(parent)
{
    connect(this, &QAbstractItemModel::rowsInserted, this, &PointFilterModel::countChanged);
    connect(this, &QAbstractItemModel::rowsRemoved, this, &PointFilterModel::countChanged);
    connect(this, &QAbstractItemModel::modelReset, this, &PointFilterModel::countChanged);
    setFilterRole(PointModel::Point);
}
void PointFilterModel::setQuery(const QString& q)
{
    if (q == query_)
        return;
    query_ = q;
    matcher_ = SearchMatcher(q);
    invalidateFilter();
    emit filtersChanged();
}
void PointFilterModel::setCriteria(const QVariantMap& c)
{
    if (c == criteria_)
        return;
    criteria_ = c;
    invalidateFilter();
    emit filtersChanged();
}
bool PointFilterModel::filterAcceptsRow(int row, const QModelIndex& parent) const
{
    const auto idx = sourceModel()->index(row, 0, parent);
    if (!criteria_.isEmpty()) {
        const auto point = idx.data(PointModel::Point).toMap();
        for (auto it = criteria_.cbegin(); it != criteria_.cend(); ++it)
            if (point.value(it.key(), "unknown") != it.value())
                return false;
    }
    return query_.isEmpty() || matcher_.matches(idx.data(PointModel::SearchText).toString());
}
QString PointFilterModel::idAt(int row) const
{
    return index(row, 0).data(PointModel::Point).toMap().value("id").toString();
}
int PointFilterModel::rowFor(const QString& id) const
{
    const auto* src = qobject_cast<PointModel*>(sourceModel());
    return src ? mapFromSource(src->index(src->rowFor(id))).row() : -1;
}
} // namespace bacuri::vms::client
