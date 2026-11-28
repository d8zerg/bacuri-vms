#include "commands/command_registry.hpp"
#include <QSettings>
#include <QDateTime>
#include <QUrl>

namespace bacuri::vms::client
{

const Command* CommandRegistry::find(const QString& id) const
{
    const auto it = std::ranges::find(commands_, id, &Command::id);
    return it == commands_.end() ? nullptr : &*it;
}
QVariantMap CommandRegistry::bindings(const QString& profile) const
{
    return profile.isEmpty() ? guestBindings_ : QSettings().value("profiles/" + profile + "/bindings").toMap();
}
void CommandRegistry::bind(const QString& profile, const QString& id, const QString& value)
{
    auto map = bindings(profile);
    if (value.isEmpty())
        map.remove(id);
    else
        map.insert(id, value);
    if (profile.isEmpty())
        guestBindings_ = map;
    else
        QSettings().setValue("profiles/" + profile + "/bindings", map);
    emit bindingsChanged(profile);
}
void CommandRegistry::load(const QString& profile) const
{
    if (usage_.contains(profile))
        return;
    auto& data = usage_[profile];
    if (profile.isEmpty())
        return;
    QSettings settings;
    settings.beginGroup("profiles/" + profile + "/usage");
    for (const auto& encoded : settings.childKeys()) {
        const auto values = settings.value(encoded).toList();
        if (values.size() == 2)
            data.insert(QUrl::fromPercentEncoding(encoded.toLatin1()),
                        {values[0].toLongLong(), values[1].toLongLong()});
    }
}
CommandRegistry::Usage CommandRegistry::usage(const QString& profile, const QString& key) const
{
    load(profile);
    return usage_.constFind(profile)->value(key);
}
void CommandRegistry::remember(const QString& profile, const QString& key)
{
    load(profile);
    auto& item = usage_[profile][key];
    ++item.count;
    item.last = QDateTime::currentMSecsSinceEpoch();
    if (!profile.isEmpty() && !key.startsWith("bookmark/"))
        QSettings().setValue("profiles/" + profile + "/usage/" + QString::fromLatin1(QUrl::toPercentEncoding(key)),
                             QVariantList{item.count, item.last});
}
} // namespace bacuri::vms::client
