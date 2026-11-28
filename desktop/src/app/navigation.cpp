#include "app/navigation.hpp"
namespace bacuri::vms::client
{
QString Navigation::pointId() const
{
    for (auto it = stack_.crbegin(); it != stack_.crend(); ++it)
        if (it->toMap().value("page") == "point")
            return it->toMap().value("id").toString();
    return {};
}
bool Navigation::containsPoint() const
{
    return !pointId().isEmpty();
}
QQuickItem* Navigation::findTarget(QQuickItem* parent, const QString& id) const
{
    if (!parent)
        return nullptr;
    if (parent->objectName() == id && parent->isVisible())
        return parent;
    for (auto* child : parent->childItems())
        if (auto* found = findTarget(child, id))
            return found;
    return nullptr;
}
Navigation::Page Navigation::page() const
{
    const auto p = stack_.last().toMap().value("page").toString();
    return p == "settings" ? Settings : p == "point" ? Point : Overview;
}
QVariantMap Navigation::intent(Page page, const QString& id)
{
    static const QStringList pages{"overview", "point", "settings"};
    QVariantMap target{{"role", "control"}, {"id", page == Overview ? "settings" : "back"}};
    return {{"page", pages.at(page)}, {"id", id}, {"focus", target}};
}
QVariantList Navigation::save(const QVariantMap& currentFocus) const
{
    auto result = stack_;
    if (!currentFocus.isEmpty()) {
        auto current = result.last().toMap();
        current["focus"] = currentFocus;
        result.last() = current;
    }
    return result;
}
bool Navigation::restore(const QVariantList& intents)
{
    if (intents.isEmpty() || intents.first().toMap().value("page") != "overview")
        return false;
    for (qsizetype index = 0; index < intents.size(); ++index) {
        const auto& intent = intents[index];
        const auto map = intent.toMap();
        const auto p = map.value("page");
        if (index > 0 && p == "overview")
            return false;
        if (map.contains("focus")) {
            const auto focus = map.value("focus").toMap();
            if (focus.value("id").toString().isEmpty() ||
                (focus.value("role") != "point" && focus.value("role") != "control"))
                return false;
        }
        if (p != "overview" && p != "settings" && !(p == "point" && !map.value("id").toString().isEmpty()))
            return false;
    }
    stack_ = intents;
    emit changed();
    return true;
}
void Navigation::push(Page next, const QString& id, const QVariantMap& focus)
{
    if ((next != Settings && next != Point) || (next == Point && id.isEmpty()) ||
        (next == page() && (next != Point || id == pointId())))
        return;
    auto current = stack_.last().toMap();
    current["focus"] = focus;
    stack_.last() = current;
    stack_.append(intent(next, id));
    emit changed();
}
void Navigation::back()
{
    if (stack_.size() > 1) {
        stack_.removeLast();
        emit changed();
    }
}
void Navigation::reset()
{
    restore({intent(Overview)});
}
} // namespace bacuri::vms::client
