#include "app/palette_index.hpp"
#include "app/point_model.hpp"
#include <QRegularExpression>
#include <algorithm>
namespace bacuri::vms::client
{
namespace
{
QString transliterate(QString word)
{
    static const QList<QPair<QString, QString>> groups{{"shch", "щ"}, {"sch", "щ"}, {"zh", "ж"}, {"kh", "х"},
                                                       {"ts", "ц"},   {"ch", "ч"},  {"sh", "ш"}, {"yu", "ю"},
                                                       {"ya", "я"},   {"yo", "ё"}};
    for (const auto& pair : groups)
        word.replace(pair.first, pair.second);
    static const QString latin = "abvgdezijklmnoprstufhyc";
    static const QString ru = "абвгдезийклмнопрстуфхыц";
    for (auto& ch : word) {
        const auto at = latin.indexOf(ch);
        if (at >= 0)
            ch = ru.at(at);
    }
    return word;
}
} // namespace
PaletteQuery::PaletteQuery(const QString& text)
{
    for (const auto& word : text.toCaseFolded().simplified().split(' ', Qt::SkipEmptyParts)) {
        QStringList choices{word, otherLayout(word), transliterate(word)};
        choices.removeDuplicates();
        terms_.append(choices);
    }
}
int PaletteQuery::score(const QString& text, QList<int>* positions) const
{
    int total = 0;
    for (const auto& choices : terms_) {
        int best = -1;
        QList<int> bestPositions;
        for (const auto& choice : choices) {
            int rank = -1;
            QList<int> found;
            const auto at = text.indexOf(choice);
            if (at >= 0) {
                rank = text == choice ? 100 : at == 0 ? 80 : !text.at(at - 1).isLetterOrNumber() ? 65 : 50;
                if (positions)
                    for (int i = 0; i < choice.size(); ++i)
                        found.append(int(at) + i);
            } else if (choice.size() > 1) {
                qsizetype pos = 0;
                int bonus = 0;
                for (auto ch : choice) {
                    const auto next = text.indexOf(ch, pos);
                    if (next < 0) {
                        pos = -1;
                        break;
                    }
                    if (!next || !text.at(next - 1).isLetterOrNumber())
                        bonus += 3;
                    if (next == pos)
                        ++bonus;
                    pos = next + 1;
                    if (positions)
                        found.append(int(next));
                }
                if (pos >= 0)
                    rank = 10 + std::min(30, bonus);
                else if (choice.size() >= 4) {
                    qsizetype begin = 0;
                    while (begin < text.size()) {
                        while (begin < text.size() && !text[begin].isLetterOrNumber())
                            ++begin;
                        auto end = begin;
                        while (end < text.size() && text[end].isLetterOrNumber())
                            ++end;
                        if (end > begin && oneEdit(QStringView(text).sliced(begin, end - begin), choice)) {
                            rank = 5;
                            found.clear();
                            break;
                        }
                        begin = end;
                    }
                }
            }
            if (rank > best) {
                best = rank;
                bestPositions = std::move(found);
            }
        }
        if (best < 0)
            return -1;
        total += best;
        if (positions)
            positions->append(bestPositions);
    }
    return total;
}
void PaletteIndex::rebuild(const QVariantList& objects)
{
    objects_.clear();
    data_.clear();
    objects_.reserve(objects.size());
    data_.reserve(objects.size());
    for (const auto& row : objects)
        upsert(row.toMap());
}
void PaletteIndex::upsert(const QVariantMap& row)
{
    const auto kind = row.value("kind").toString(), name = row.value("name").toString(),
               id = row.value("id").toString(), site = row.value("site").toString();
    const auto key = kind + "/" + site + "/" + id;
    const auto data = row.value("data").toMap();
    const auto at = QDateTime::fromString(data.value("observed_at").toString(), Qt::ISODateWithMs);
    objects_.insert(key, {kind, key, name,
                          kind == "layout"  ? "layers"
                          : kind == "alert" ? "bell-ring"
                                            : "clipboard-list",
                          name.toCaseFolded(), id, data.value("point_id").toString(), site,
                          at.isValid() ? at.toSecsSinceEpoch() : 0});
    data_.insert(key, row);
}
void PaletteIndex::remove(const QString& key)
{
    objects_.remove(key);
    data_.remove(key);
}
void PaletteIndex::removeSiteAlerts(const QString& site)
{
    for (auto it = objects_.begin(); it != objects_.end();) {
        if (it->kind == "alert" && it->site == site) {
            data_.remove(it.key());
            it = objects_.erase(it);
        } else
            ++it;
    }
}
void PaletteIndex::setPoints(const QVariantList& points)
{
    points_.clear();
    points_.reserve(points.size());
    for (const auto& value : points) {
        const auto m = value.toMap();
        const auto name = m.value("name").toString(), id = m.value("id").toString();
        points_.append(
            {"point", "point/" + id, name, "cctv", name.toCaseFolded(), id, id, m.value("site_id").toString()});
    }
}
} // namespace bacuri::vms::client
